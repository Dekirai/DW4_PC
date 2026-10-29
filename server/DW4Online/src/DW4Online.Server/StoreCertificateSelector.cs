using System.Runtime.Versioning;
using System.Security.Cryptography.X509Certificates;

namespace DW4Online.Server;

/// <summary>
/// Section "Tls": with a host name set, HTTPS endpoints take their certificate from the Windows certificate store
/// (default LocalMachine\My, where win-acme puts renewals). The store is re-read every few minutes, so a renewed
/// certificate is used without restarting the service (a restart would drop running relay sessions).
/// </summary>
public sealed class TlsOptions
{
    /// <summary>
    /// The certificate's DNS name (or IP address for an IP certificate): served to every connection that names no
    /// other configured host (an address like https://88.198.52.221 sends no name). Empty: Kestrel's own configuration.
    /// </summary>
    public string StoreHostName { get; set; } = "";
    /// <summary>
    /// More host names with their own certificates, chosen by the name the client asks for (SNI): e.g. the domain
    /// dekirai.crygod.de with a Let's Encrypt certificate while clients pinned to the IP certificate keep working.
    /// </summary>
    public List<string> AdditionalHostNames { get; set; } = [];
    public string StoreName { get; set; } = "My";
    public string StoreLocation { get; set; } = "LocalMachine";
    public int RefreshMinutes { get; set; } = 10;
    /// <summary>A folder whose files are served at /.well-known/acme-challenge/ (win-acme filesystem validation); empty = off.</summary>
    public string AcmeChallengeDirectory { get; set; } = "";
}

/// <summary>
/// Where the TLS selectors read their certificates instead of the Windows store: registered only by tests (a
/// self-signed certificate in memory, so no test touches a certificate store). Each read must return new objects,
/// like <see cref="X509Store.Certificates"/>; the selector disposes the ones it does not keep.
/// </summary>
public sealed class TlsCertificateSource(Func<X509Certificate2Collection> read)
{
    public X509Certificate2Collection Read() => read();
}

/// <summary>
/// Picks the HTTPS certificate from the store. Every read of the store hands out new certificate objects (each holds a
/// native handle), so the ones not chosen are disposed at once, an unchanged certificate keeps its first object, and a
/// replaced one stays alive until the next replacement (TLS handshakes that picked it may still be running). While
/// there is no certificate at all (before new-selfsigned-cert.ps1 ran, say) the store is read at most every
/// <see cref="MissingRetry"/> and the error is logged once per refresh period: port 443 gets handshakes from internet
/// scanners all the time, and none of them may cost a store read and a log line.
/// </summary>
[SupportedOSPlatform("windows")]
internal sealed class StoreCertificateSelector(TlsOptions options, ILogger log, Func<X509Certificate2Collection>? source = null, TimeProvider? time = null,
    string? host = null)
{
    internal static readonly TimeSpan MissingRetry = TimeSpan.FromSeconds(10);

    private readonly Lock _gate = new();
    private readonly TimeProvider _time = time ?? TimeProvider.System;
    private readonly string _host = host ?? options.StoreHostName;
    private X509Certificate2? _current;
    private X509Certificate2? _retired;
    private DateTime _nextRefresh;
    private DateTime _nextMissingRead;
    private DateTime _nextMissingLog;

    public X509Certificate2? Select()
    {
        lock (_gate)
        {
            var now = _time.GetUtcNow().UtcDateTime;
            if (now < (_current is not null ? _nextRefresh : _nextMissingRead)) return _current;
            _nextRefresh = now.AddMinutes(Math.Max(1, options.RefreshMinutes));
            _nextMissingRead = now + MissingRetry;
            var found = PickBest(source?.Invoke() ?? ReadStore(), _host, now);
            if (found is null)
            {
                if (_current is null && now >= _nextMissingLog)
                {
                    log.LogError("No valid certificate for {Host} in {Location}\\{Store}", _host, options.StoreLocation, options.StoreName);
                    _nextMissingLog = _nextRefresh;
                }
                return _current;
            }
            if (_current is not null && found.Thumbprint == _current.Thumbprint)
            {
                found.Dispose(); // the same certificate again: keep the object in use
                return _current;
            }
            log.LogInformation("TLS certificate for {Host}: {Thumbprint}, valid until {NotAfter:u}, SPKI pin {Pin}", _host, found.Thumbprint,
                found.NotAfter.ToUniversalTime(), SpkiPin.Of(found));
            _retired?.Dispose();
            _retired = _current;
            _current = found;
            return _current;
        }
    }

    private X509Certificate2Collection ReadStore()
    {
        using var store = new X509Store(Enum.Parse<StoreName>(options.StoreName, true), Enum.Parse<StoreLocation>(options.StoreLocation, true));
        store.Open(OpenFlags.ReadOnly | OpenFlags.OpenExistingOnly);
        return store.Certificates;
    }

    /// <summary>The newest valid certificate with a private key for <paramref name="host"/>; every other one is disposed.</summary>
    internal static X509Certificate2? PickBest(X509Certificate2Collection certificates, string host, DateTime nowUtc)
    {
        X509Certificate2? best = null;
        foreach (var cert in certificates)
        {
            var usable = cert.HasPrivateKey && cert.NotBefore.ToUniversalTime() <= nowUtc && cert.NotAfter.ToUniversalTime() > nowUtc &&
                Matches(cert, host);
            if (usable && (best is null || cert.NotAfter > best.NotAfter))
            {
                best?.Dispose();
                best = cert;
            }
            else
            {
                cert.Dispose();
            }
        }
        return best;
    }

    private static bool Matches(X509Certificate2 cert, string host)
    {
        if (string.Equals(cert.GetNameInfo(X509NameType.DnsName, false), host, StringComparison.OrdinalIgnoreCase)) return true;
        foreach (var ext in cert.Extensions)
        {
            if (ext is X509SubjectAlternativeNameExtension san)
            {
                foreach (var dns in san.EnumerateDnsNames())
                    if (string.Equals(dns, host, StringComparison.OrdinalIgnoreCase)) return true;
                foreach (var ip in san.EnumerateIPAddresses())
                    if (string.Equals(ip.ToString(), host, StringComparison.OrdinalIgnoreCase)) return true;
            }
        }
        return false;
    }
}

/// <summary>
/// The certificate per requested host name (SNI): a configured additional host gets its own certificate, every other
/// connection (an IP address sends no name) the one of Tls:StoreHostName. So the domain's certificate and the pinned IP
/// certificate can be served side by side while clients move from https://88.198.52.221 to the domain.
/// </summary>
[SupportedOSPlatform("windows")]
internal sealed class HostCertificateSelector
{
    private readonly StoreCertificateSelector _primary;
    private readonly Dictionary<string, StoreCertificateSelector> _byName = new(StringComparer.OrdinalIgnoreCase);

    public HostCertificateSelector(TlsOptions options, ILogger log, Func<X509Certificate2Collection>? source = null, TimeProvider? time = null)
    {
        _primary = new StoreCertificateSelector(options, log, source, time, options.StoreHostName.Trim());
        _byName[options.StoreHostName.Trim()] = _primary;
        foreach (var name in options.AdditionalHostNames.Select(n => n.Trim()).Where(n => n.Length > 0))
            _byName.TryAdd(name, new StoreCertificateSelector(options, log, source, time, name));
    }

    /// <summary>The certificate for <paramref name="requestedName"/> (the client's SNI, may be null).</summary>
    public X509Certificate2? Select(string? requestedName) =>
        requestedName is { Length: > 0 } && _byName.TryGetValue(requestedName, out var named) && !ReferenceEquals(named, _primary)
            ? named.Select() ?? _primary.Select()
            : _primary.Select();
}
