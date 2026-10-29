using System.Security.Cryptography.X509Certificates;

namespace DW4Online.Server;

/// <summary>
/// "DW4Online.Server tls-pin": prints the SHA-256 SPKI pin the game client needs while the server uses a self-signed
/// certificate (deploy/new-selfsigned-cert.ps1 calls it). Reads only public certificate data, so it needs no
/// elevation and no database.
/// <code>
/// tls-pin --host 88.198.52.221 [--store My] [--location LocalMachine]   the certificate the service would choose
/// tls-pin --thumbprint &lt;hex&gt; [--store My] [--location LocalMachine]
/// tls-pin --file &lt;certificate.cer | .crt | .pem | .pfx&gt; [--password-env NAME]
/// </code>
/// A PFX with a password (the spare of deploy/new-selfsigned-cert.ps1 always has one) is opened with the password in
/// the environment variable NAME (never on the command line, where other processes could read it).
/// </summary>
public static class TlsPinCommand
{
    public static int Run(IReadOnlyList<string> args, TextWriter output, TextWriter error)
    {
        string? Value(string name)
        {
            var at = args.ToList().IndexOf(name);
            return at >= 0 && at + 1 < args.Count ? args[at + 1] : null;
        }

        X509Certificate2? cert;
        try
        {
            cert = Value("--file") is { } file ? Load(file, Value("--password-env") is { } name ? Environment.GetEnvironmentVariable(name) ?? "" : null)
                : Value("--thumbprint") is { } thumbprint ? FromStore(Value("--store"), Value("--location"), c =>
                    string.Equals(c.Thumbprint, thumbprint.Replace(" ", "", StringComparison.Ordinal), StringComparison.OrdinalIgnoreCase))
                : Value("--host") is { } host ? FromStoreForHost(Value("--store"), Value("--location"), host)
                : null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or System.Security.Cryptography.CryptographicException)
        {
            error.WriteLine(e.Message);
            return 1;
        }
        if (cert is null)
        {
            error.WriteLine(args.Any(a => a is "--file" or "--thumbprint" or "--host")
                ? "No such certificate (for --host: valid now, with a private key, the name or IP address in its subject alternative names)."
                : "Use: tls-pin --host <name or IP> | --thumbprint <hex> | --file <certificate> [--password-env NAME] [--store My] [--location LocalMachine]");
            return 2;
        }
        using (cert) Write(cert, output);
        return 0;
    }

    public static void Write(X509Certificate2 cert, TextWriter output)
    {
        output.WriteLine($"Certificate: {cert.Subject} (thumbprint {cert.Thumbprint})");
        output.WriteLine($"Valid:       {cert.NotBefore.ToUniversalTime():yyyy-MM-dd HH:mm} to {cert.NotAfter.ToUniversalTime():yyyy-MM-dd HH:mm} UTC");
        output.WriteLine($"Names:       {string.Join(", ", Names(cert))}");
        // What a browser shows as the certificate's SHA-256 fingerprint (to compare by hand before accepting the warning).
        output.WriteLine($"Certificate fingerprint (SHA-256): {cert.GetCertHashString(System.Security.Cryptography.HashAlgorithmName.SHA256)}");
        // What the game client pins: it stays the same for every certificate made with this key.
        output.WriteLine($"SPKI pin (SHA-256, base64): {SpkiPin.Of(cert)}");
        output.WriteLine($"SPKI pin (SHA-256, hex):    {SpkiPin.HexOf(cert)}");
    }

    private static IEnumerable<string> Names(X509Certificate2 cert)
    {
        foreach (var san in cert.Extensions.OfType<X509SubjectAlternativeNameExtension>())
        {
            foreach (var ip in san.EnumerateIPAddresses()) yield return "IP " + ip;
            foreach (var dns in san.EnumerateDnsNames()) yield return "DNS " + dns;
        }
    }

    private static X509Certificate2 Load(string path, string? password)
    {
        var extension = Path.GetExtension(path).ToLowerInvariant();
        if (extension is not (".pfx" or ".p12")) return X509CertificateLoader.LoadCertificateFromFile(path);
        try
        {
            return X509CertificateLoader.LoadPkcs12FromFile(path, password, X509KeyStorageFlags.EphemeralKeySet);
        }
        catch (System.Security.Cryptography.CryptographicException e) when (password is null)
        {
            throw new System.Security.Cryptography.CryptographicException(
                e.Message + " A PFX with a password: put it into an environment variable and pass --password-env <its name>.", e);
        }
    }

    private static X509Certificate2? FromStore(string? store, string? location, Func<X509Certificate2, bool> match)
    {
        using var opened = Open(store, location);
        X509Certificate2? found = null;
        foreach (var cert in opened.Certificates)
        {
            if (found is null && match(cert)) found = cert;
            else cert.Dispose();
        }
        return found;
    }

    private static X509Certificate2? FromStoreForHost(string? store, string? location, string host)
    {
        if (!OperatingSystem.IsWindows()) return null;
        using var opened = Open(store, location);
        return StoreCertificateSelector.PickBest(opened.Certificates, host, DateTime.UtcNow);
    }

    private static X509Store Open(string? store, string? location)
    {
        var opened = new X509Store(Enum.Parse<StoreName>(store ?? "My", true), Enum.Parse<StoreLocation>(location ?? "LocalMachine", true));
        opened.Open(OpenFlags.ReadOnly | OpenFlags.OpenExistingOnly);
        return opened;
    }
}
