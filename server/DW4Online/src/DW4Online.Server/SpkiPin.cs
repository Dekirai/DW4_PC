using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace DW4Online.Server;

/// <summary>
/// The SHA-256 pin of a certificate's public key (SubjectPublicKeyInfo, as in HPKP and most pinning libraries): what
/// the game client compares while the server uses a self-signed certificate (https://88.198.52.221). It stays the same
/// for every certificate made with the same key; a new key means a new pin.
/// </summary>
public static class SpkiPin
{
    /// <summary>Base64 of SHA-256 over the DER SubjectPublicKeyInfo (44 characters).</summary>
    public static string Of(X509Certificate2 certificate) => Convert.ToBase64String(Hash(certificate));

    /// <summary>The same hash as 64 hex digits.</summary>
    public static string HexOf(X509Certificate2 certificate) => Convert.ToHexString(Hash(certificate));

    private static byte[] Hash(X509Certificate2 certificate) => SHA256.HashData(certificate.PublicKey.ExportSubjectPublicKeyInfo());
}
