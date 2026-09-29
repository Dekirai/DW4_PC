# Original player stat recalculation

`GameStatsRuntime` implements the ordered `8036FDE8` invocation through its
return at `803714AC`, including health, attributes, resistances, six technique
groups and the enabled equipment tail. It uses actual typed player wrappers,
shared database caches and the existing messenger. This is one original
routine in player initialization; it does not establish complete GAMESTART,
save loading, combat growth, manager startup or a running original game.

The supported equipment provider is the verified `GameDataWeapon` path.
A different non-null item type retains an explicit service boundary instead
of supplying a guessed database or modifier. The generated player rows and
their empty technique list remain unchanged until this routine is invoked.

## Invocation and continuation

Construct `GameStatsRuntime(source_ram, modifier_byte)` for RAM zero through
three and call:

```cpp
auto result = stats.resume(data, caches, messenger, events);
```

`result.completed` means this invocation reached its original return. A false
result carries the required service diagnostic. Resume the same invocation
with the same four owners after supplying that service. Its returned handled
value is a host integration convention, not an inferred return register from
the original void stat routine. Completed invocations do not repeat writes.

The runtime is move-only and privately retains one coroutine frame containing
the original local rows, values, loop positions and item queries. Each actual
nested database dispatch remains owned by its query across suspension. There
is no public coroutine scheduler or alternative background execution model.
The public `completed()` and `faulted()` queries are read-only. Reentrant
resume and different-owner resume are rejected before effects; an exception
during execution faults the invocation and forbids replay.

## Arithmetic and health publication

Base helpers `8036F808` and `8036FB80` read the selected row's species, level
and UPCNT list before their lazy `shinka` lookup. The captured list's element
is read after the database queries. All original integer additions,
subtractions and multiplications use defined 32-bit wrapping; division by100
is signed and truncates toward zero. The first signed level threshold at or
above the current level selects the table row:

| Upper level | Origin | HP/MP base | HP/MP slope | Attribute base | Attribute slope |
|---:|---:|---:|---:|---:|---:|
|5|1|0|10|0|10|
|10|5|40|20|40|9|
|20|10|140|50|85|8|
|30|20|640|80|165|7|
|50|30|1440|120|235|6|
|70|50|3840|180|355|5|
|110|70|7440|260|455|4|
|150|110|17840|180|615|3|
|230|150|25040|120|735|2|
|999|230|34640|80|895|1|

The operation order is wrapped `level-origin`, wrapped slope multiplication,
wrapped addition of the table base, wrapped multiplication by the database
growth value, signed division by100, wrapped addition of UPCNT, then wrapped
addition of the database initial value. The base helper applies only an upper
bound:99,999 for maxima and9,999 for attributes. The outer routine applies
its later bonuses and clamps separately. The tables come from
`804EAD24..804EAE10`; names at `804EAE14/28` select the five attribute pairs.

The health sequence preserves these visible steps:

1. Compute the HP base, add live outer-row UPDATA[6], apply enabled MAXHP
   modifiers, upper-cap99,999 and publish MHP.
2. Read current HP, retain whether it equals zero, and write MHP into HP only
   when HP is zero or greater than MHP.
3. Compute the MP base, apply enabled MAXMP modifiers, upper-cap99,999 and
   publish MMP. This path has no outer UPDATA addition.
4. Write MMP into MP only when the earlier HP read was zero or current MP
   exceeds MMP. MP zero alone does not trigger a refill.

MHP and MMP have no lower clamp. Negative HP is not generally refilled. Later
database callbacks can observe the earlier completed publications and can
change live data without replacing the already-captured HP-zero decision.

Each base helper independently reloads the manager's current player row. The
outer routine retains its initial row for later publication and its initial
species/level for resistance and technique calculations. A nested player reset
can therefore make a later base calculation read a new row while the outer
routine still publishes to the old row. The implementation preserves that
distinction and the captured UPCNT list versus its live element.

## Modifiers and techniques

The low modifier byte gates the original item modifiers and equipment tail.
The five helper paths retain their distinct behavior:

- POWERSOCKET and MAINSLOT inspect element zero only, when the list is
  nonempty. They do not search past a null first slot.
- POWERPOCKET and MAINPOCKET capture the initial count but resolve the live
  list storage for each later element. Null elements are skipped.
- SUBSLOT also inspects element zero. Its conditional revision addition
  performs a separate SHUSEINOURYOKU string query and requires exact equality
  with the current stat column.

Every Weapon item query performs a fresh DATABASE/weapon/0/GETDATABASE send.
It consumes current object zero and reads the selected item's live record
after delivery. Revision is read after the applicable query. The modifiers
execute in the original field-specific order; they are not combined into one
commutative bonus total. Five attributes and four resistances are each clamped
to1..9,999 and published before the next field begins.

After resistance publication, the same WAZALIST inner list is cleared before
any technique query. Six TECH groups run in authored order. Each resolves its
shinka base, reads pcTech.SKILL at that base and the live player skill before
testing the level gate. Thresholds are tested from rank4 down to rank0. The
first passing rank creates a distinct item with kind9, record `base+rank`,
equipped byte0 and base number `base`. Duplicate bases and their order remain
intact. Appends and completed groups remain visible across later pauses.

## Tail and equipment names

NEXTEXP is published after the six technique groups. Level999 yields zero;
other levels use the original wrapped sequence `level*level`,
`(level+140)*square`, `level*200`, then addition.

With modifiers enabled, the routine continues through ATTACKDELAY, hit and
status rates, first matching POWERPOCKET ZOKUSEI, HPDRAIN/MPDRAIN and the
original KAIHIRITSU-to-CHOHATURITSU copy. Tail clamps are field-specific;
the attribute clamp is not applied to all rates.

The first POWERSOCKET item's record is captured before its fresh SKILL query.
WEAPONRECNO receives that captured record; WEAPONSKILLNO uses the query's
post-callback live record. The routine then captures the socket and name
lists, clears the same POWERSOCKETNAME list and appends a fresh MODEL query
result for each non-null item in the captured-count socket traversal. Neither
the preceding equipment names nor item-query results suppress these sends.

## Checked profile and validation

Existing lists and indexed elements are required, with a 65,536-element bound.
Missing typed fields, invalid indices, a level beyond the verified table's
last threshold999 and uncertain callback outcomes stop explicitly. The
original unchecked table or pointer access is not presented as a safe default.
Shared C++ owners represent the relevant object/list/string lifetimes;
original heap selection, allocator internals and arbitrary external destructor
effects are not implemented by this module.

Synthetic tests cover 127 checks, including full modifier/tail order with
69 distinct item sends, an actual paused delivery, caught recursive rejection,
old-row versus current-row accesses, captured UPCNT ownership, saved HP-zero,
Waza duplicates/ranks, first-null slots, live item records and fault/no-replay
boundaries. Optional original-data testing adds 16 checks across four prepared
RAMs using the actual shinka, pcTech and weapon definitions, for 143 total.
Those explicitly prepared rows/items are not a claim that GAMESTART ran.

Independent ignored instruction comparisons against the linked core passed:

- 884 numeric cases, 333,378 original instructions: original base helpers,
  database integer queries, caller clamp and NEXTEXP; 56 cases separately
  identify the checked level-table stop.
- 21 complete outer-flow cases, 434,115 original instructions and 516 item
  queries: all 31 published fields, ordered Waza payloads, final names and
  query counts match. Distinct signed values in every weapon column exercise
  column selection and the different tail clamps. Original base/modifier/query/Waza-initializer/tail
  instructions execute; bounded named-owner, item virtual database,
  allocation/list/string/refcount and strcmp hooks are explicit.

The full-flow comparison does not independently execute the hook bodies,
native suspension protocol or original hardware. Native tests separately
check continuation behavior. Reproduction from a built core:

```text
dw4_game_stats_tests
dw4_game_stats_tests EXTRACTED_ROOT
```

The ignored `work/v31-stats-oracle.py` uses the test executable's `--oracle`
protocol. `work/v31-stats-flow-oracle.py` uses its isolated linked-core runner;
original bytes, decoded instructions and reference results remain outside the
tracked project. Source evidence is USA GDJEB2 revision0 main.dol SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
