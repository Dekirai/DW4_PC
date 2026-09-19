# Original CHECKLAND floor response

`HitLandFloorInvocation` reconstructs the USA `HITLANDMODEL/CHECKLAND` leaf at
`80300444..80300940`. It prepares the original four rays, invokes the existing
native collision traversal, selects actual candidates and publishes the floor
response. `make_hitland_floor_receiver` connects this invocation to a resolved
native messenger delivery. Neither operation executes guest code or depends on
the set-aside recompilation runtime.

## Query and selection

The original guard returns without changing the packet when a boolean argument
already exists, the vector list is empty, or the float list is empty. Otherwise
the call captures vector zero and floats zero through two: position, half-width,
upper range and lower range. A short nonempty float list faults at its reached
missing read before query reset. Input integers do not select query flags.

The current query is reset and retained. Four vertical segments run from
absolute Y `1000` to `-100`, at X/Z offsets `(+,+)`, `(-,+)`, `(+,-)`, `(-,-)`.
The original arithmetic and corner order are preserved, including negative
width; bounds are not normalized. Query flags are exactly `1`. The retained
common traversal produces candidates through typed node dispatch, mapped
geometry and the original triangle operations.

`original_hitland_floor_selection` captures the signed query candidate count.
Nonpositive counts consume neither candidate storage nor unused numeric inputs.
Positive counts use physical candidate slots independently of the list's logical
count and capacity. Selection consists of three ordered passes:

1. Among heights strictly inside the upper/lower window, select the greatest
   rounded `height - position_y`. Equal rounded differences retain the first hit.
2. If none qualified, choose the strictly lower height with the smallest rounded
   `position_y - height`. Equal rounded distances again retain the first hit.
3. If none was lower, select the minimum height, retaining the last equal hit.

The two difference comparisons cannot be replaced by raw height comparisons:
distinct heights can have the same rounded difference. The final response
rechecks the strict window and computes adjustment from the rounded subtraction
`position_y - height <= 0`. Empty results publish height `-1` and three false
bytes. A selected candidate sets the third byte even if outside the window.

Arithmetic covers finite values under the established NI=1, RN=00 profile,
including subnormal inputs, signed zero and result flushing with tininess before
rounding. Reached nonfinite arithmetic raises `HitLandQueryError`; the code does
not claim full original nonfinite recovery or FPSCR exception flags. Candidate
counts above the native allocation bound of 65,536 are explicitly rejected.
The source uses strict floating-point compiler options.

## Publication and suspension

On a hit, clear only integers, reload the selected candidate, capture its name,
append its child index and append its nullable name. Existing strings remain.
On no hit, preserve both integers and strings. Both paths then clear booleans,
clear vectors, append the three response bytes individually and append the
captured X/Z with the selected Y. Floats, 64-bit integers, matrices, objects and
the enabled-only gate remain untouched. A reached error keeps earlier effects;
invalid selected text, for example, fails after integer replacement but before
boolean/vector publication.

Input scalars and the query owner survive a suspended traversal. Resumption does
not reset the query or append the rays again. Publication uses the messenger's
current packet, which may differ from the packet that supplied the request.
Completed invocations do not replay their effects; recursion, a different
messenger and retries after a fault are rejected.

Construct `make_hitland_floor_receiver(manager, services)` inside the event
runtime's manager resolver. The messenger retains that resolved receiver for one
delivery, including suspension, then resolves a fresh one for the next delivery.
This floor-only helper implements CHECKLAND only. The later
`make_hitland_query_receiver` in [HITLAND_WALL.md](HITLAND_WALL.md) supports both
CHECKLAND and CHECKWALL; enabled CHECKWALLFULL remains pending. Original manager
creation/registration is still required.

## Evidence and remaining integration

The independent local reduction comparison executes actual instruction words
from the supplied USA DOL at `80300694..80300850`, plus the empty branch at
`80300890..80300894`. Python integer arithmetic supplies independently rounded
scalar addition/subtraction. The reference does not call the native selector.
Results: 7,689 cases, 1,533,032 instructions and all 114 addresses in that bounded
tail; zero mismatches. Of these, 7,652 compare every result field and all height
bits, while 37 reach the expected finite-input overflow boundary. All three
passes, rounded ties, strict endpoints and empty/negative counts are exercised.
This is a bounded instruction comparison, not physical-console execution.

The 79 permanent synthetic checks distinguish selector/continuation fixtures
from real geometry. The full local Release build passes 117 CTest suites.
The complete synthetic path maps an authored triangle at Y `8`, produces four
actual candidates, delivers the response through a registered native receiver
and lets the existing `ModelHitLandInvocation/HITFLOOR` update its shared node.
Optional supplied-data checks run twelve floor-center queries against
`ndmw.afs[179]`, using zero width and four coincident rays per center. A further
query through the same registered receiver applies a resulting height to an
explicitly prepared recipient node. Distinct-corner coverage comes from the
synthetic fixture. The supplied-data run passes 103 checks in total and records
seven floor responses from those twelve fixed-range center queries. These tests
do not execute original manager startup or the full player-frame scheduler.

Run the local build and optional original-data integration with:

```powershell
.\scripts\build.ps1 -Configuration Release -CoreOnly -Jobs 4
.\build\Release\dw4_hitland_floor_tests.exe 'PATH\Extracted'
```

Local ignored evidence is in `work/native-checkland-oracle-result.json`,
`work/native-checkland-oracle-report.md` and the corresponding build/integration
logs. Original DOL SHA256:
`EC61C010477C844781763A3041922E38719B3CED3B74BE5CE71D778CE458E471`.
No game assets or original instruction bytes are public repository content.

Dynamic node updates, missing-bound construction, other bound classes, complete
CHECKWALLFULL and original startup/frame scheduling remain required. The
walking preview still uses its provisional controller. See
[HITLAND_TRAVERSAL.md](HITLAND_TRAVERSAL.md),
[HITLAND_GEOMETRY.md](HITLAND_GEOMETRY.md),
[HITLAND_DELIVER.md](HITLAND_DELIVER.md) and [VALIDATION.md](VALIDATION.md).
