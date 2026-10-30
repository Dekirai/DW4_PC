#pragma once

// The SHA-256 SPKI pins of the DW4Online server's self-signed certificate for
// https://88.198.52.221 (docs/SEAMLESS.md 10, server/DW4Online/deploy/DEPLOY.md
// 6.1): base64 of SHA-256 over the DER SubjectPublicKeyInfo, as
// "DW4Online.Server tls-pin" and deploy/print-pins.ps1 print it. The first is
// the key in use, the second the spare kept offline.
//
// They are filled by a build step, never written here: CMake takes
// DW4_ONLINE_PINS="<in use>,<spare>" or, when that is empty, the pins in
// local/online_pins.txt (gitignored; `print-pins.ps1 -Out` writes it), checks
// their form and passes them to this library only (DW4_SERVER_PIN_IN_USE /
// DW4_SERVER_PIN_SPARE). The pins are public values (hashes of public keys);
// the private keys stay with the server's operator, never in the repository.
// Without pins the client refuses to talk to the server (fail closed) and
// says so; DW4_ONLINE_REQUIRE_PINS=ON makes a release configure fail instead.
#ifndef DW4_SERVER_PIN_IN_USE
#define DW4_SERVER_PIN_IN_USE ""
#endif
#ifndef DW4_SERVER_PIN_SPARE
#define DW4_SERVER_PIN_SPARE ""
#endif

namespace dw4::game::online {

inline constexpr const char* kServerPinInUse = DW4_SERVER_PIN_IN_USE;
inline constexpr const char* kServerPinSpare = DW4_SERVER_PIN_SPARE;

} // namespace dw4::game::online
