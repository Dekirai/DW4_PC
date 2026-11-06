# Round I1a, lane relay (relay world + story): progress notes

Branch `feat-i1a-relay` from main d0e005b0, worktree `DW4_PC-wt/i1a-relay`. Scope: SEAMLESS.md 19.25 "Round I1a",
lanes *relay world* and *story* (with round 72's additions), the wire of 19.18 as agreed in the lane contract
(`DW4_PC-wt/i1a-contract.md`, outside the repo; its relay entries are copied into SEAMLESS.md 19.31 at the end).

## Done

- Protocol 49 wire (commit 1): `Wire.WorldProtocol` 49; WELCOME (member u32, wid, box), BARRIER (+ travel token,
  plan id, the member's own settings), BARRIER_OK (co-arrival members u32, no dropped blob, the world part:
  wid, host, visibility, arrival, settings + hash, owner key, story mode, redirect), JOIN_REQ kind (join, home,
  go), INTENT 3 leave (4 go along accepted, ignored), STATE flag 16, STORY / STORY_ACK owner key, RESUME + stage,
  PRESENCE / PRESENCE2 out (member u32, relation), the tick event HOME (6; GO 7 reserved), CLOSE 9 account; new
  INSTANCE_OP 0x1B, INSTANCE_INFO 0x95, WORLD_NOTICE 0x96, MEMBER_INFO 0x97; golden vectors in
  `WireGoldenTests` (`World_messages_equal_the_clients`), the type registry.
- Stub copies of web-owned files (so the branch compiles; the integration takes the web lane's):
  `Domain/Abstractions/WorldRelay.cs`, `Domain/Abstractions/World.cs`, `Domain/World/OnlinePrefs.cs`,
  `Domain/World/AreaTable.cs` (inline table of 19.30.2).

## Left

Everything else of the lane scope (see the list in the final report): the world hub, story keepers per (account,
box), the gateway, the history queue, presence selection, staff, M3, M4 and the load gate.

## How to run the tests

- Unit: `"/c/Program Files/dotnet/dotnet.exe" test server/DW4Online/tests/DW4Online.Tests --disable-build-servers`
- Integration: a private MariaDB on port 3631 (`DW4ONLINE_TEST_MYSQL`), test ports 57000-57199
  (`DW4ONLINE_TEST_PORTS`), then `dotnet test server/DW4Online/tests/DW4Online.Integration.Tests`.
