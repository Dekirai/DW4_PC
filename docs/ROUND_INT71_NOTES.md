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
  HTTPS endpoint (so TLS passthrough cannot name an address either). Each header's last value counts.
- `X-Forwarded-Prefix` (a plain path) becomes the path base; a request path that still starts with it (a proxy that
  kept the path and named the prefix too) has it cut once; then `Service:PathBase` applies as UsePathBase did. No
  doubling in any combination.
- `X-Forwarded-For`: the last entry is the address the local proxy saw; when it lies in `Service:TrustedProxyNetworks`
  (new: CIDR networks, e.g. Cloudflare's published ranges; only with BehindProxy; nothing wider than /8 or /16), the
  entry before it, which that edge appended, is the client's. One edge hop only; an edge address is never trusted as
  the connection's own peer. Traefik must keep Cloudflare's header:
  `entryPoints.<name>.forwardedHeaders.trustedIPs` with the same ranges (it replaces the header otherwise; then the
  edge stays the address, as before).
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

## Open

- The release: DW4Online 0.1.6 goes on the server first (a backup before migration 0012, DEPLOY.md 8 and 9.1;
  `Play:MinimumProtocol` / `CurrentProtocol` 48), the game client after it.
- The proxy settings on the server: Traefik's `forwardedHeaders.trustedIPs` (static configuration, a Traefik restart)
  and `Service:BehindProxy`, `PathBase`, `TrustedProxyNetworks` in DW4Online (DEPLOY.md 6.2, case 2).
- Requests that reach Traefik from Cloudflare's network without passing through the site's zone (another account's
  Worker) could name an address for the per-IP limits; Cloudflare's Authenticated Origin Pulls with an own
  certificate would close that (DEPLOY.md 6.2).
- Round I1a's design changes M1-M5 (SEAMLESS.md 19.25) are for its lanes; nothing of them is built here.
