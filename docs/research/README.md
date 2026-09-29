# Research notes

These notes record behaviour recovered from the original USA `main.dol`
(SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`) and game data: function
addresses, data layouts, command tables and verified edge cases.

Most of them were written for an earlier reconstruction approach whose code
(the per-function "Invocation" classes of the former `dw4_core`, the walking
preview and the development workbench) has been removed from the source tree.
That code and its tests remain available in Git history. The notes are kept as
reference material for rebuilding the same behaviour inside `dw4_engine`; they
describe the original game, not the current engine API.

`CORE_ARCHITECTURE.md` and `CORE_PORTING_STRATEGY.md` are the former top-level
documents of that approach. `VALIDATION.md` holds its historical test records.
