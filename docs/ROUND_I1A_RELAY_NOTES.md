# Round I1a, lane relay (relay world + story): progress notes

Branch `feat-i1a-relay` from main d0e005b0, worktree `DW4_PC-wt/i1a-relay`. Scope: SEAMLESS.md 19.25 "Round I1a",
lanes *relay world* and *story* (with round 72's additions), the wire of 19.18 as agreed in the lane contract
(`DW4_PC-wt/i1a-contract.md`, outside the repo). What was built and decided: SEAMLESS.md 19.31.

## Done

- Protocol 49 wire: every new and changed message of 19.18 that I1a needs, golden vectors
  (`WireGoldenTests.World_messages_equal_the_clients`, also in the contract file for the client lane).
- The world hub (`RoomHub`, not renamed, world parts in `server/DW4Online/src/DW4Online.Relay/World/`): member ids,
  the world admission and handshake (only protocol 49, only WORLD tickets, roles member / staff, World:DataHashes,
  World:MaxMembers), one world session per account (NewSession replaces), WorldInstance with seats, area, visited,
  starting window, visibility, settings and hash, history id; fresh arrivals matched by area with Visible() and
  decision 40's order (co-arrival with visibility, starting instances, I1a's automatic join of the first row, the
  guest's NEW by the gate and visited rules, Story not here yet, an own instance otherwise); decisions inside an
  instance among its own stage groups; area_escape; the lone host never matched with another instance; the DISSOLVE
  with the own story (replace / merge) and the redirects (stagML02 guests, ended instances, detached groups); END
  (every reason of 19.6.5) with HOME stamped sealed or through JOIN_REQ home, split at forks; INTENT leave;
  INSTANCE_OP (visibility, end, remove with bar); the host's grace (close or 4 s silence, neutral fill, no lagOut,
  member timeout margin, held story values, resume, end host_lost); M3 (defeated from the alive mask, give-up copy);
  lost instances (WELCOME wid 0, RESUME filing a guest that continues alone); INSTANCE_INFO, WORLD_NOTICE,
  MEMBER_INFO; presence server-wide in hub stages by the selection (off the lock, friends first, nearest, cap 30,
  hysteresis, tiers, budget), in fields only the own instance; staff role with live re-check, visibility bypass,
  staff_join audit; the history queue (IInstanceHistoryRepository, audit, remembered visibility); WorldGateway
  (IWorldRelay + IWorldNotifier, the default admission until web's WorldService sets its own, StoryReplaced).
- Story lane: `StoryKeeper` per (account, box) (owner / guest rules, the earned queue per keeper, Hold / Release,
  Replacing / Replace, SetDifficulty / RebasedCopy); the owner key and *owner* refusals (reason 12); STORY_ACK to
  the members playing on a keeper; round 59's not_started / epochs / OwnerSeen / dropped sets retired.
- M4: the PING margin cached per stream with a linear selection, pings spread over the passes, presence selection
  off the lock, fan-out by selection with tiers, presence frames built once per relation.
- Stub copies of web-owned files (the integration takes the web lane's): `Domain/Abstractions/WorldRelay.cs`,
  `Domain/Abstractions/World.cs`, `Domain/World/OnlinePrefs.cs`, `Domain/World/AreaTable.cs`.
- Client lane 13:30 (contract "relay 13:45"): story mode *replace* for arrivals from home into another's instance, a fresh
  joiner's fallback after its JOIN as a new stage group of the target instance, the DISSOLVE's modes, plan id 0xFFFF =
  *New instance*, seatless groups home with the redirect, the redirect's stage in lower case.
- The load gate at protocol 49, both harnesses (SEAMLESS.md 19.17.4): holds pass everywhere (p99 at most 20.4 us, p99.99
  0.42-1.63 ms), sealing lateness over sockets passes, the joins cell joins all ten (its groups of three now at fields of
  the area table).
- Tests: unit (WireGoldenTests, WorldLifecycleTests, StoryKeeperTests, HubStoryTests, HubPresenceTests and the
  adapted lockstep tests), integration (WorldRelayTests over loopback with the real server and MariaDB, the adapted
  LoadGateTests and RelayConnectionTests; RoomTests / RoomPageTests removed as the web lane did).

## Left (see also the final report)

- M4 (e): the connection path's CPU per frame (one socket write per frame, two reads per frame) is not cut; the relay's CPU
  over sockets is at the half core in *base* (0.48 / 0.66) and *fullhub* (0.52), passing elsewhere (SEAMLESS.md 19.17.4,
  "Measured again at protocol 49").
- Not built in I1a by design: OPTIONS / PLAN / CHOICE (I1b), go along (I1b), the mirror (I2), FRIENDS_STATE (I2),
  spectators and GOTO (I3), parties (I4), seats (I5).
- At the integration: take the web lane's versions of the four stub files; `RelayGateway.cs` conflicts with web's
  RoomSweeper removal (keep the relay lane's room no-ops, drop RoomSweeper); wire web's
  `WorldService.RememberVisibilityAsync` (through IWorldVisibilitySink, already in the contract) and its
  IInstanceHistoryRepository registration (the history queue picks it up from DI); the relay lane's integration tests need
  web's TestHost with `fakeWorld: false`; web's four skipped relay tests in OnlineStoryTests are replaced by
  `WorldRelayTests` and the story unit tests; the integration test that `Wire.WorldProtocol` equals
  `WorldProtocol.Version`; tests/winhttp_client and tests/e2e/relay_spike_test.py are still room-based.

## How to run the tests

- Unit: `"/c/Program Files/dotnet/dotnet.exe" test server/DW4Online/tests/DW4Online.Tests --disable-build-servers`
- Integration: a private MariaDB (XAMPP 10.4.32) on 127.0.0.1:3631 with its data in `server/DW4Online/.local`
  (gitignored; start from Bash: `mysqld.exe --defaults-file=<.local>\mysql-data\my.ini --bind-address=127.0.0.1
  --port=3631`, stop with SQL `SHUTDOWN`), then
  `DW4ONLINE_TEST_MYSQL="Server=127.0.0.1;Port=3631;User ID=root;Password=<.local/mysql-root.txt>"
  DW4ONLINE_TEST_PORTS=57000-57199 dotnet test server/DW4Online/tests/DW4Online.Integration.Tests`.
- Load gate: `DOTNET_gcServer=1 tests/DW4Online.FakeClient/bin/Release/net10.0/DW4Online.FakeClient.exe loadgate
  --seconds 30 --out <json>` and the same with `--sockets --port 57100` (`--cells joins` for one cell). Runs of this
  lane: `build/i1arelay/gate/` (loadgate49_r1, joins49_r2, loadgate_net49_r1, loadgate_net49_storm_r2).
- Last results: unit 448/448 (`build/i1arelay/unit_r2.log`); integration see the final report.
- No client code changed (the branch's diff outside `server/` and `docs/` is empty): no ctest, det.sh or game
  scenarios are needed for this lane.
