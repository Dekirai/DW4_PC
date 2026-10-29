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
}
