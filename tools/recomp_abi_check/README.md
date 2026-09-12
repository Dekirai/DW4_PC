# Recompiled module ABI check

This small Windows/MSVC tool checks the runtime facade, its pinned Dolphin
chassis and the generated DW4 DLL before a startup attempt. It has no dependency
on the manually recovered DW4 core.

Run it through the script, or let `build-recomp.ps1` run it after a module build:

```powershell
./scripts/test-recomp-abi.ps1 -Module "C:/path/to/gGDJEB2_recomp.dll"
```

`-RuntimeSource` defaults to `work/moderngekko`. Compiler output, the executable,
`latest.json` and the input/hash record stay in ignored `work/recomp-abi`.
Compilation uses `/W4 /WX`; a compiler failure or mismatch fails the script.

The host and chassis headers are compiled in separate translation units. The
check compares their CPU and module ABI versions, object size/alignment, and
every listed field's offset, size, alignment and compiler RTTI type. The script
also verifies that both CPU declarations contain exactly the checked field
inventory, so an added field cannot escape detection by fitting into padding.
Nested range and REL records are checked before normalizing their deliberately
different facade/chassis type names.

The executable loads the specified DLL and calls only
`staticrecomp_get_module`. It never invokes dispatch or the state-loaded hook.
Normal DLL initialization still occurs. Before reading a descriptor tail, it
checks the module ABI prefix. It then verifies the CPU ABI/size, USA game ID
`GDJEB2`, entry point `0x80005240` and nonempty dispatch/coverage metadata.

This guard addresses a concrete upstream integration mismatch: the public
facade originally declared CPU ABI 4 with an extra `cycle_budget` field, while
the pinned chassis and C module template used ABI 3. On MSVC x64 their sizes
were 3,536 and 3,528 bytes, and five fields after `downcount` had different
offsets. Comparing only the generated C helpers against GXRuntime could not
detect that separate public-facade mismatch.

Passing proves the checked declarations and descriptor agree. It does not
prove instruction equivalence, boot, rendering or gameplay. Actual startup
validation remains a separate bounded step.
