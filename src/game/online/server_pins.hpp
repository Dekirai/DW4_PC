#pragma once

// The SHA-256 SPKI pins of the DW4Online server's self-signed certificate for
// https://88.198.52.221 (docs/SEAMLESS.md 10, server/DW4Online/deploy/DEPLOY.md
// 6.1): base64 of SHA-256 over the DER SubjectPublicKeyInfo, as
// "DW4Online.Server tls-pin" prints it. The first is the key in use, the
// second the spare kept offline. Both are empty until the certificate exists
// on the server: the client then refuses to talk to it (fail closed) and says
// so. Fill them in from new-selfsigned-cert.ps1's output before a release.
namespace dw4::game::online {

inline constexpr const char* kServerPinInUse = "";
inline constexpr const char* kServerPinSpare = "";

} // namespace dw4::game::online
