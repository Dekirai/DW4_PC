using System.Net;
using System.Runtime.Versioning;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using DW4Online.Server;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.Integration.Tests;

/// <summary>Self-signed test certificates (in memory, never in a certificate store).</summary>
public static class TestCertificates
{
    /// <summary>A PKCS#12 blob (no password) of a self-signed server certificate for an IP address or a DNS name.</summary>
    public static byte[] SelfSignedPfx(string host, DateTimeOffset notBefore, DateTimeOffset notAfter, RSA? key = null)
    {
        using var rsa = key is null ? RSA.Create(2048) : null;
        var request = new CertificateRequest("CN=" + host, key ?? rsa!, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        var san = new SubjectAlternativeNameBuilder();
        if (IPAddress.TryParse(host, out var ip)) san.AddIpAddress(ip);
        else san.AddDnsName(host);
        request.CertificateExtensions.Add(san.Build());
        request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension([new Oid("1.3.6.1.5.5.7.3.1")], false));
        request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment, true));
        using var cert = request.CreateSelfSigned(notBefore, notAfter);
        return cert.Export(X509ContentType.Pkcs12);
    }

    public static X509Certificate2 Load(byte[] pfx) => X509CertificateLoader.LoadPkcs12(pfx, null, X509KeyStorageFlags.EphemeralKeySet);
}

[SupportedOSPlatform("windows")]
public class CertificateTests
{
    private static readonly DateTimeOffset Now = new(2026, 11, 1, 12, 0, 0, TimeSpan.Zero);

    [Fact]
    public void PickBest_takes_the_newest_matching_certificate_and_disposes_the_rest()
    {
        var older = TestCertificates.Load(TestCertificates.SelfSignedPfx("88.198.52.221", Now.AddDays(-30), Now.AddDays(30)));
        var newer = TestCertificates.Load(TestCertificates.SelfSignedPfx("88.198.52.221", Now.AddDays(-1), Now.AddDays(300)));
        var otherHost = TestCertificates.Load(TestCertificates.SelfSignedPfx("dw4.example.org", Now.AddDays(-1), Now.AddDays(900)));
        var expired = TestCertificates.Load(TestCertificates.SelfSignedPfx("88.198.52.221", Now.AddDays(-60), Now.AddDays(-1)));
        var noKey = X509CertificateLoader.LoadCertificate(newer.RawData);
        var collection = new X509Certificate2Collection(new[] { older, newer, otherHost, expired, noKey });

        var best = StoreCertificateSelector.PickBest(collection, "88.198.52.221", Now.UtcDateTime);

        Assert.Same(newer, best);
        Assert.NotEqual(IntPtr.Zero, newer.Handle);
        foreach (var gone in new[] { older, otherHost, expired, noKey }) Assert.Equal(IntPtr.Zero, gone.Handle);
        best!.Dispose();
    }

    [Fact]
    public void Select_keeps_one_object_per_certificate_and_retires_a_replaced_one()
    {
        var first = TestCertificates.SelfSignedPfx("127.0.0.1", Now.AddDays(-1), Now.AddDays(60));
        var second = TestCertificates.SelfSignedPfx("127.0.0.1", Now.AddDays(-1), Now.AddDays(90));
        var third = TestCertificates.SelfSignedPfx("127.0.0.1", Now.AddDays(-1), Now.AddDays(120));
        var inStore = first;
        var handedOut = new List<X509Certificate2>();
        var clock = new ManualClock();
        var selector = new StoreCertificateSelector(new TlsOptions { StoreHostName = "127.0.0.1", RefreshMinutes = 10 }, NullLogger.Instance,
            () =>
            {
                // Like X509Store.Certificates: new objects on every read.
                var copy = TestCertificates.Load(inStore);
                handedOut.Add(copy);
                return new X509Certificate2Collection(copy);
            }, clock);

        var a = selector.Select();
        Assert.NotNull(a);
        Assert.Same(a, selector.Select()); // within the refresh period: no store read
        Assert.Single(handedOut);

        clock.Advance(TimeSpan.FromMinutes(11)); // the same certificate again: the new copy is disposed
        Assert.Same(a, selector.Select());
        Assert.Equal(2, handedOut.Count);
        Assert.Equal(IntPtr.Zero, handedOut[1].Handle);
        Assert.NotEqual(IntPtr.Zero, a!.Handle);

        inStore = second; // a renewal: the old certificate stays alive for running handshakes
        clock.Advance(TimeSpan.FromMinutes(11));
        var b = selector.Select();
        Assert.NotSame(a, b);
        Assert.NotEqual(IntPtr.Zero, a.Handle);

        inStore = third; // the next renewal frees the first one
        clock.Advance(TimeSpan.FromMinutes(11));
        var c = selector.Select();
        Assert.NotSame(b, c);
        Assert.Equal(IntPtr.Zero, a.Handle);
        Assert.NotEqual(IntPtr.Zero, b!.Handle);
        Assert.NotEqual(IntPtr.Zero, c!.Handle);
    }

    /// <summary>
    /// Without any certificate (before the deploy script ran) every handshake asks: the store is read at most every
    /// 10 seconds and the error is logged once per refresh period, and a certificate added later is found in time.
    /// </summary>
    [Fact]
    public void A_missing_certificate_costs_no_store_read_per_handshake()
    {
        var reads = 0;
        byte[]? inStore = null;
        var clock = new ManualClock();
        var log = new CountingLogger();
        var selector = new StoreCertificateSelector(new TlsOptions { StoreHostName = "88.198.52.221", RefreshMinutes = 10 }, log, () =>
        {
            reads++;
            return inStore is null ? new X509Certificate2Collection() : new X509Certificate2Collection(TestCertificates.Load(inStore));
        }, clock);

        for (var i = 0; i < 1000; ++i) Assert.Null(selector.Select()); // a burst of scanner handshakes
        Assert.Equal(1, reads);
        Assert.Equal(1, log.Errors);
        for (var s = 0; s < 60; ++s)
        {
            clock.Advance(TimeSpan.FromSeconds(1));
            selector.Select();
        }
        Assert.Equal(7, reads); // one at the start, then one per 10 seconds
        Assert.Equal(1, log.Errors);

        inStore = TestCertificates.SelfSignedPfx("88.198.52.221", Now.AddDays(-1), Now.AddYears(10));
        clock.Advance(TimeSpan.FromSeconds(10));
        var cert = selector.Select();
        Assert.NotNull(cert);
        Assert.Same(cert, selector.Select());
        Assert.Equal(8, reads);
        cert!.Dispose();
    }

    private sealed class CountingLogger : Microsoft.Extensions.Logging.ILogger
    {
        public int Errors;
        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;
        public bool IsEnabled(Microsoft.Extensions.Logging.LogLevel logLevel) => true;
        public void Log<TState>(Microsoft.Extensions.Logging.LogLevel logLevel, Microsoft.Extensions.Logging.EventId eventId, TState state,
            Exception? exception, Func<TState, Exception?, string> formatter)
        {
            if (logLevel == Microsoft.Extensions.Logging.LogLevel.Error) Errors++;
        }
    }

    /// <summary>
    /// The move from the pinned IP certificate to the domain: a connection to the IP sends no name and gets the IP
    /// certificate, a connection naming the domain gets the domain's, and an unknown name (or a domain whose
    /// certificate is missing) falls back to the IP certificate.
    /// </summary>
    [Fact]
    public void The_certificate_follows_the_requested_host_name()
    {
        var ip = TestCertificates.SelfSignedPfx("88.198.52.221", Now.AddDays(-1), Now.AddYears(10));
        var domain = TestCertificates.SelfSignedPfx("dekirai.crygod.de", Now.AddDays(-1), Now.AddDays(90));
        var withDomain = true;
        X509Certificate2Collection Read() => withDomain
            ? new X509Certificate2Collection(new[] { TestCertificates.Load(ip), TestCertificates.Load(domain) })
            : new X509Certificate2Collection(TestCertificates.Load(ip));
        var options = new TlsOptions { StoreHostName = "88.198.52.221", AdditionalHostNames = [" dekirai.crygod.de "] };

        var selector = new HostCertificateSelector(options, NullLogger.Instance, Read, new ManualClock());
        using var ipCert = TestCertificates.Load(ip);
        using var domainCert = TestCertificates.Load(domain);
        Assert.Equal(ipCert.Thumbprint, selector.Select(null)?.Thumbprint);
        Assert.Equal(ipCert.Thumbprint, selector.Select("")?.Thumbprint);
        Assert.Equal(domainCert.Thumbprint, selector.Select("dekirai.crygod.de")?.Thumbprint);
        Assert.Equal(domainCert.Thumbprint, selector.Select("DEKIRAI.crygod.de")?.Thumbprint);
        Assert.Equal(ipCert.Thumbprint, selector.Select("other.example.org")?.Thumbprint);

        withDomain = false;
        var noDomain = new HostCertificateSelector(options, NullLogger.Instance, Read, new ManualClock());
        Assert.Equal(ipCert.Thumbprint, noDomain.Select("dekirai.crygod.de")?.Thumbprint);
    }

    [Fact]
    public void Tls_pin_prints_the_pin_of_a_certificate_file()
    {
        var dir = Path.Combine(Path.GetTempPath(), "dw4online-pin-" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(dir);
        try
        {
            using var cert = TestCertificates.Load(TestCertificates.SelfSignedPfx("88.198.52.221", Now.AddDays(-1), Now.AddYears(10)));
            var der = Path.Combine(dir, "server.cer");
            File.WriteAllBytes(der, cert.Export(X509ContentType.Cert));
            var pem = Path.Combine(dir, "server.pem");
            File.WriteAllText(pem, cert.ExportCertificatePem());

            foreach (var file in new[] { der, pem })
            {
                var output = new StringWriter();
                Assert.Equal(0, TlsPinCommand.Run(["--file", file], output, TextWriter.Null));
                var text = output.ToString();
                Assert.Contains("SPKI pin (SHA-256, base64): " + SpkiPin.Of(cert), text);
                Assert.Contains("SPKI pin (SHA-256, hex):    " + SpkiPin.HexOf(cert), text);
                Assert.Contains("IP 88.198.52.221", text);
                Assert.Contains("Certificate fingerprint (SHA-256): " + cert.GetCertHashString(HashAlgorithmName.SHA256), text);
            }

            // SHA-256 over the DER SubjectPublicKeyInfo, 44 base64 characters, unchanged by a renewal with the same key.
            Assert.Equal(Convert.ToBase64String(SHA256.HashData(cert.PublicKey.ExportSubjectPublicKeyInfo())), SpkiPin.Of(cert));
            Assert.Equal(44, SpkiPin.Of(cert).Length);
            using var key = RSA.Create(2048);
            using var first = TestCertificates.Load(TestCertificates.SelfSignedPfx("88.198.52.221", Now.AddDays(-1), Now.AddYears(1), key));
            using var renewed = TestCertificates.Load(TestCertificates.SelfSignedPfx("88.198.52.221", Now.AddYears(1), Now.AddYears(10), key));
            Assert.NotEqual(first.Thumbprint, renewed.Thumbprint);
            Assert.Equal(SpkiPin.Of(first), SpkiPin.Of(renewed));
            Assert.NotEqual(SpkiPin.Of(cert), SpkiPin.Of(first));

            Assert.Equal(1, TlsPinCommand.Run(["--file", Path.Combine(dir, "missing.cer")], TextWriter.Null, TextWriter.Null));
            Assert.Equal(2, TlsPinCommand.Run([], TextWriter.Null, TextWriter.Null));
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }
}
