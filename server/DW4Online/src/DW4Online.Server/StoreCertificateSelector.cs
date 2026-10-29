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
    /// <summary>The certificate's DNS name (or IP address for an IP certificate). Empty: Kestrel's own configuration applies.</summary>
    public string StoreHostName { get; set; } = "";
    public string StoreName { get; set; } = "My";
    public string StoreLocation { get; set; } = "LocalMachine";
    public int RefreshMinutes { get; set; } = 10;
    /// <summary>A folder whose files are served at /.well-known/acme-challenge/ (win-acme filesystem validation); empty = off.</summary>
    public string AcmeChallengeDirectory { get; set; } = "";
}

/// <summary>
/// Picks the HTTPS certificate from the store. Every read of the store hands out new certificate objects (each holds a
/// native handle), so the ones not chosen are disposed at once, an unchanged certificate keeps its first object, and a
/// replaced one stays alive until the next replacement (TLS handshakes that picked it may still be running).
/// </summary>
[SupportedOSPlatform("windows")]
internal sealed class StoreCertificateSelector(TlsOptions options, ILogger log, Func<X509Certificate2Collection>? source = null, TimeProvider? time = null)
{
    private readonly Lock _gate = new();
    private readonly TimeProvider _time = time ?? TimeProvider.System;
    private X509Certificate2? _current;
    private X509Certificate2? _retired;
    private DateTime _nextRefresh;

    public X509Certificate2? Select()
    {
        lock (_gate)
        {
            var now = _time.GetUtcNow().UtcDateTime;
            if (_current is not null && now < _nextRefresh) return _current;
            _nextRefresh = now.AddMinutes(Math.Max(1, options.RefreshMinutes));
            var found = PickBest(source?.Invoke() ?? ReadStore(), options.StoreHostName, now);
            if (found is null)
            {
                if (_current is null) log.LogError("No valid certificate for {Host} in {Location}\\{Store}", options.StoreHostName, options.StoreLocation, options.StoreName);
                return _current;
            }
            if (_current is not null && found.Thumbprint == _current.Thumbprint)
            {
                found.Dispose(); // the same certificate again: keep the object in use
                return _current;
            }
            log.LogInformation("TLS certificate for {Host}: {Thumbprint}, valid until {NotAfter:u}", options.StoreHostName, found.Thumbprint, found.NotAfter.ToUniversalTime());
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
