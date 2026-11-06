# Integration r71 (branch `int-r71`)

Branch `int-r71` from main 236e26f3 (the Data Overflow forge round), worktree `DW4_PC-wt/int71`. Not pushed, not
merged into main.

## Merges (`--no-ff`, in this order)

| Branch | Tip | What | Conflicts |
| --- | --- | --- | --- |
| `feat-i2social` | e04fa064 | round I2-social: friends and blocks on the server and the website, migration `0012_friends` (schema 12) | none |
| `fix-j-ready` | b533c790 | round J: READY from a checkpoint at its check tick (protocol 48), tick-state layout 2 with every compiled Lua chunk's label, DW4Online 0.1.6 | `server/DW4Online/deploy/DEPLOY.md` section 10: round J's protocol bullet (48) and I2-social's new friends bullet, both kept |
| `dref-i1a-meas` | 8db7f07b | round I1a's first measurements: `dw4_world_probe`, the load gate harness, the relay's seal diagnostics, the ghost figure probe, SEAMLESS.md 19 M1-M5 | none |

All three branch from a475b98a; main's Data Overflow round touches none of their files. `RoomHub.cs` merged without a
conflict: round J's READY check (`OnNetFp`, `OnReady`, `RecheckWaitingReadies`) and I1a's `SealLateness` counter in
`Seal` (with `RelayHub.SealRoom`, `SealPasses`, `SealTryMisses`) sit in different places and both stay. `SEAMLESS.md`
merged without a conflict; section 19's status and I2-social's "At the merge" note in 19.25 now name the integration
(one server release for all three: DW4Online 0.1.6, protocol 48, migrations up to `0012_friends`, schema 12).

Checked after the merges: `0012_friends` is the next number after main's 0011 (the migrator numbers by the files,
`schema` = 12 after it; `MigratorTests` count 21 tables); `Version` 0.1.6 in `server/DW4Online/Directory.Build.props`;
protocol 48 in `kOnlineProtocol` (`src/game/online_run.hpp`), `Wire.ReadyCheckProtocol`, the example settings'
`Play:MinimumProtocol` / `CurrentProtocol` and DEPLOY.md sections 8 and 10.

## Behind a reverse proxy (new in this integration)

The user serves the website at `https://dekirai.crygod.de/dw4online` through Cloudflare -> Traefik (on the server)
-> DW4Online `http://127.0.0.1:5280`; Traefik strips `/dw4online`, so the pages linked to the root, and every browser
had a Cloudflare edge's address. `DW4Online.Server.ProxyHeaders` replaces ASP.NET's ForwardedHeaders and UsePathBase
middleware (the details, the settings for Traefik with and without `stripPrefix` and the check in HeidiSQL:
`server/DW4Online/deploy/DEPLOY.md` 6.2, case 2):

- Forwarded headers count only with `Service:BehindProxy` and only on plain-HTTP connections from this machine (the
  proxy's hop to 127.0.0.1:5280): never from a direct client (the game at `https://88.198.52.221:8443`), never on the
  HTTPS endpoint (so TLS passthrough cannot name an address either). `X-Forwarded-For`'s and `X-Forwarded-Proto`'s
  last value counts.
- `X-Forwarded-Prefix` counts only when it is `Service:PathBase` (letter case aside; changed in the fix stage, below):
  a stripping proxy's request gets that base back; a path that still starts with it (a non-stripping proxy, one that
  names the prefix too) has it cut once as UsePathBase did. No doubling in any combination; any other value (a
  browser's own header a proxy passed on) is ignored, and without `PathBase` the header counts for nothing.
- `X-Forwarded-For`: the last entry is the address the local proxy saw; when it lies in `Service:TrustedProxyNetworks`
  (new: Cloudflare's published ranges; only with BehindProxy; nothing wider than /8 or /16, no IPv4-mapped network,
  no host bits), Cloudflare's `CF-Connecting-IP` is the client's address, if it is one public address (fix stage: the
  first version took the `X-Forwarded-For` entry before the edge, which needed Traefik's
  `forwardedHeaders.trustedIPs` on the entry point, a setting that holds for every site on it). Traefik stays at its
  default; an edge address is never trusted as the connection's own peer.
- Why not ASP.NET's middleware: its KnownNetworks trust a network at every position (a Cloudflare address connecting
  to port 8443 directly would be believed), its XForwardedPrefix sets the path base and then UsePathBase doubles it on
  a non-stripping proxy that names the prefix, and it takes a prefix from deeper hops.

## Results

| Check | Result | Log (worktree `build/` unless noted) |
| --- | --- | --- |
| Release build without `DW4_ONLINE_DEV`, with tests | built | `bld_rel.log` |
| ctest (that build) | 56 of 56 | `ctest_rel.log` |
| `det.sh` (an exe copy of that build) | 4004f056 / 396cb219 in both runs | `det1.log` (runs in main's `work/dref/checks/det/det_int71a`) |
| DW4Online.Tests after the merges | 311 of 311 | `srv_merge_unit.log` |
| DW4Online.Integration.Tests after the merges (own mysqld 3627, ports 56800-56899) | 112 passed, 4 skipped (EndToEnd: no WinHTTP client, no Python, no certificate store) | `srv_merge_int.log` |
| DW4Online.Tests final | 378 of 378 (67 new `ProxyHeadersTests`) | `srv_final_unit.log` |
| DW4Online.Integration.Tests final (WinHTTP client built, `DW4ONLINE_PYTHON` set) | 124 passed, 1 skipped (`Deploy_kit_certificate_from_the_store...`: it writes to the user's certificate store) | `srv_final_int.log` |
| `ReverseProxyTests` against the server before `ProxyHeaders` (counter-proof, a copy) | 6 of 9 fail (the stripping cases link to the root, the edge is every browser's address) | `srv_counterproof_int.log` |
| Scenarios, DW4_ONLINE_DEV build, `--parallel 1`, the merge's server | 14 of 14 pass: J1, J5, JB, JR, JS, JS2, RGJ1, S3, JL (`J1@20,0,0`), S2, S17b, L1, L2, M1 | `scen_set1.log`, `tools/online_test/.work/run_20261105_195015` |
| J1 again with the final server (ProxyHeaders in the pipeline) | pass (checkpoint 3,583, READY 3,597 checked, decision to JOIN 314 ms) | `scen_final_j1.log`, `tools/online_test/.work/run_20261105_205400` |

The full integration run needed no second pass for the relay classes: with 100 ports (56800-56899) `TestPorts` did
not run out. No scenario was added by the three lanes (`tools/online_test/scenarios` as at a475b98a apart from round
J's changes to J1, J5, JS, JS2, RGJ1).

Joins from a checkpoint, the relay's `join_decided` to its `join` note (lane jready's `join_times.py`,
`join_times_set1.txt`); every READY from a checkpoint was checked (`checked=True`), at the check tick T + 14 or, in
L2, at the multiple of 60 after it:

| Scenario | Checkpoint T, READY tick | Decision to JOIN |
| --- | --- | --- |
| J1 | 3,606, 3,620 | 312 ms |
| JL (J1 behind 20 ms each way) | 3,589, 3,603 | 420 ms |
| J5 | 17,959, 17,973 | 342 ms |
| JS2 | 3,587, 3,601 | 324 ms |
| RGJ1 | 3,587, 3,601 | 328 ms |
| S3, first / second joiner | 1,311, 1,325 / 1,789, 1,803 | 321 / 309 ms |
| L1 | 1,311, 1,325 | 311 ms |
| L2 | 1,364, 1,380 | 414 ms |
| S17b | 1,386, 1,400 | 305 ms |
| S2 | 1,235, 1,249 | 487 ms |

Eleven joins from a checkpoint, 305-487 ms, median 324 ms (lane jready's review: 309-509 ms, median 349 ms). The
classic joins: JB 1,034 ms (the damaged checkpoint refused), JS 1,185 ms (`ready_mismatch` at 3,604, as the scenario
expects), M1 1,890 ms (the donor's status window open: no checkpoint in time).

## Not run

- The certificate-store end-to-end test (it adds and removes a certificate in the user's store).
- r25c's regression and the save state suite (no game code changed in the integration beyond the three lanes'
  merged code, which ctest, det.sh and the scenarios cover).

## Fix stage (the review of int-r71, 2026-11-06)

Own stack: mysqld 3627 (the integration tests; run.py's own for the scenarios), ports 56800-56899 (the load gate's
relay child 56880-56889). Logs in the worktree's `build/fix/`.

| Finding (lens) | Outcome | Commit |
| --- | --- | --- |
| The load gate ran protocol 47 rooms; 48's check-fingerprint pass in `OnNetFp` unmeasured (merge) | fixed: `LoadGate.Protocol` = `Wire.ReadyCheckProtocol` (`--protocol`), the 48 checkpoint join checked at its check tick; the gate again at 48 and 47 in one session: NETFP hold p99 7.8-8.4 us against 3.9-4.8 us, the rest within the spread (SEAMLESS.md 19.17.4) | e779e0c3, 0872b8f0 |
| SEAMLESS.md 19.18 / I1b / 19.22 still "MinimumProtocol 47" (merge) | fixed: the world's protocol is 49 after this integration, everywhere in 19 | 2b90dddb |
| Traefik's `trustedIPs` holds for every site on the entry point (security, should_fix) | fixed by design: DW4Online reads Cloudflare's `CF-Connecting-IP` behind a trusted edge; Traefik keeps its default; DEPLOY.md warns against the setting | 5bccc642, 2aef18bc |
| A browser's `X-Forwarded-Prefix` became the path base without stripPrefix (security, should_fix) | fixed: the header counts only as `Service:PathBase`; the RequireHttps redirect sends `Cache-Control: no-store` | 5bccc642 |
| The residual risk understated; loopback and unspecified addresses taken (security) | fixed: DEPLOY.md lists the real consequences (per-IP limits, the account-wide lock, a targeted name/address lock, the records); `CF-Connecting-IP` must be one public address (no loopback, unspecified, private, CGNAT, link-local, multicast, edge) | 5bccc642, 2aef18bc |
| `TrustedProxyNetworks` takes IPv4-mapped networks and host bits silently (security) | fixed: refused at the start, the message names the network meant | 5bccc642 |
| The privacy page does not name Cloudflare (security) | fixed: `Web:Processors` on the privacy page, DEPLOY.md 6.2 has Cloudflare's entry | 5bccc642, 2aef18bc |
| `FriendsChanged(a, b)` does not say who may learn of it (security, I1) | fixed: `FriendsChanged(account, other, otherSees)` | dc183083, 4f304646 |
| The RequireHttps redirect uses the raw Host header (security, optional) | not changed: the plain HTTP endpoint listens on 127.0.0.1 only, Traefik's `Host()` rule fixes the host, a browser sends the host of the URL it opened, and the redirect is `no-store` now, so no cache can keep another client's | - |
| Scenario timing worse in the reviewer's runs (rerun) | no code change (other load on the machine); J1, JS, JS2, JB, S3, M1 again below | - |
| run.py terminated its mysqld right after SHUTDOWN (rerun) | fixed: the clients and the server first, then SHUTDOWN and up to 120 s waiting | f270e343 |
| No real-client scenario for friends and blocks (rerun) | not now: the client has no friends code before round I1 | - |

Each new test was checked against the old code (`build/fix/counter`, the merge's `ProxyHeaders`, `WebSetup`, privacy
page and options with only the new header constant added): 23 of 110 `ProxyHeadersTests` (as then) and 10 of 11
`ReverseProxyTests` fail there (`counter/srv_c_unit.log`, `counter/srv_c_int.log`).

Checks after the fixes (HEAD 0872b8f0 for the code; the C++ sources are unchanged since the merges, so `build/rel` and
`build/dev` rebuilt incrementally with nothing to do):

| Check | Result | Log (`build/fix/`) |
| --- | --- | --- |
| ctest (Release without `DW4_ONLINE_DEV`, with tests) | 56 of 56 | `ctest_rel.log` |
| `det.sh` (an exe copy of that build, name `int71fix`) | 4004f056 / 396cb219 in both runs | `det.log` |
| DW4Online.Tests | 423 of 423 (`ProxyHeadersTests` 112, `SocialServiceTests` 24, `HubLogicTests` 80) | `srv_final_unit.log`, `.trx` |
| DW4Online.Integration.Tests, one run (mysqld 3627, ports 56800-56899, WinHTTP client, `DW4ONLINE_PYTHON`) | 127 passed, 1 skipped (the certificate-store test); social (`SocialApiTests` 7, `FriendPageTests` 4, `AccountScrubTests` 3, `MigratorTests` 10), proxy (`ReverseProxyTests` 11, `PathBaseTests` 4), relay (`RelayConnectionTests` 5, `OnlineStoryTests` 8, `RoomTests` 5) and `LoadGateTests` 4 all passed; no port shortage, no second pass needed | `srv_final_int.log`, `.trx` |
| The load gate at 48 and 47 (in-process all cells, sockets, idle-hub joins) | every run valid; SEAMLESS.md 19.17.4 | `gate/gate_r1.log`, `gate/*.json` |
| Scenarios J1, JB, JS, JS2, M1, S3 (`--parallel 1`, the DEV build, this worktree's server) | 6 of 6 pass, 0 differing fingerprints outside JS2's injected drift; every client sealed at delay 2 with 0 dropped frames (JS2's drifting b 38, M1's b 2), backlogs 0-1 | `scen_final.log`, `tools/online_test/.work/run_20261106_120039` |
| Joins (`join_times.py`) | J1 340 ms (checkpoint 3,609, READY 3,623 checked), JS2 307 ms, S3 324 / 305 ms; classic: JB 1,046 ms, JS 1,162 ms (`ready_mismatch` at 3,603), M1 1,831 ms | `join_times_final.txt` |
| run.py's mysqld after the scenarios | `mysqld.err` ends with "Shutdown complete", no `.pid` left | the run folder's `mysqld.err` |

The own mysqld of the integration tests stopped with SQL SHUTDOWN ("Shutdown complete"); no process of this stage is
left running.

## Open

- The release: DW4Online 0.1.6 goes on the server first (a backup before migration 0012, DEPLOY.md 8 and 9.1;
  `Play:MinimumProtocol` / `CurrentProtocol` 48), the game client after it.
- The proxy settings on the server, DW4Online only: `Service:BehindProxy`, `PathBase`, `TrustedProxyNetworks` and
  `Web:Processors` (DEPLOY.md 6.2, case 2); Traefik's entry point stays as it is (no `forwardedHeaders.trustedIPs`).
- Requests that reach Traefik from Cloudflare's network without passing through the site's zone (another account's
  Worker) could name an address (unlimited attempts past the per-IP limits, the account-wide lock from one machine, a
  targeted lock of a player's name and address); Cloudflare's Authenticated Origin Pulls with an own certificate
  would close that (DEPLOY.md 6.2).
- Round I1a's design changes M1-M5 (SEAMLESS.md 19.25) are for its lanes; nothing of them is built here.
