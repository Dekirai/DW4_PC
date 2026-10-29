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

[SupportedOSPlatform("windows")]
internal sealed class StoreCertificateSelector(TlsOptions options, ILogger log)
{
    private readonly Lock _gate = new();
    private X509Certificate2? _current;
    private DateTime _nextRefresh;

    public X509Certificate2? Select()
    {
        lock (_gate)
        {
            if (_current is not null && DateTime.UtcNow < _nextRefresh) return _current;
            _nextRefresh = DateTime.UtcNow.AddMinutes(Math.Max(1, options.RefreshMinutes));
            var found = Find();
            if (found is not null && found.Thumbprint != _current?.Thumbprint)
                log.LogInformation("TLS certificate for {Host}: {Thumbprint}, valid until {NotAfter:u}", options.StoreHostName, found.Thumbprint, found.NotAfter.ToUniversalTime());
            if (found is null && _current is null) log.LogError("No valid certificate for {Host} in {Location}\\{Store}", options.StoreHostName, options.StoreLocation, options.StoreName);
            _current = found ?? _current;
            return _current;
        }
    }

    private X509Certificate2? Find()
    {
        using var store = new X509Store(Enum.Parse<StoreName>(options.StoreName, true), Enum.Parse<StoreLocation>(options.StoreLocation, true));
        store.Open(OpenFlags.ReadOnly | OpenFlags.OpenExistingOnly);
        var now = DateTime.Now;
        X509Certificate2? best = null;
        foreach (var cert in store.Certificates)
        {
            if (!cert.HasPrivateKey || cert.NotBefore > now || cert.NotAfter <= now) continue;
            if (!Matches(cert, options.StoreHostName)) continue;
            if (best is null || cert.NotAfter > best.NotAfter) best = cert;
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
