# Logical resource catalog

`dw4::MountedResources` reads the original `files/ndmw.igb` setup and mounts its
AFS archives. A logical name identifies bytes inside an archive; it does not
become a path opened on the host filesystem. Callers retain the archive, entry
and serialized object reference when identifying an individual texture.

The API accepts either the extracted root or its `files` directory. Archive
basenames and declared entry counts are validated before use. Reads are bounded
and fetch one selected payload; original files are never modified.

Limits are checked while assembling paths: 4,096 bytes per component, 16,384
bytes per logical path, 64 MiB of logical names in total and 128 archives.
The loader rejects drive prefixes, path traversal and conflicting locations.

## Observed USA setup

The info-list root contains one `beNDMWAfsSetupInfo` object. Its field 2 is the
root label, field 5 refers to the file list and field 6 to the archive list.
These are serialized slot numbers, not native C++ offsets.

| Type | Fields used |
| --- | --- |
| `beNDMWAfsSetupData` | 2: AFS basename; 3: declared entry count |
| `igAdxAfsFile` | 2: filename; 3: nullable directory list; 4: signed 16-bit archive index; 5: signed 16-bit entry index |
| `igNamedObject` in a directory list | 2: directory component |

Object lists are resolved through the checked [IGB document API](IGB_OBJECTS.md).
Directory components are joined in list order with `/`; lookup preserves case.
An entry index of `-1` means unavailable. Such a record remains visible and cannot
be read. Duplicate logical names are accepted only when their locations agree.

The verified original setup has label `GC`, two archives and 2,080 logical paths.
2,079 resolve to entries; the `NONE` record is unavailable. The archive counts
match the actual AFS indices: 1,382 in `area.afs` and 973 in `ndmw.afs`.
Not every archive entry is named by this setup; raw AFS access remains useful.

Verified examples:

| Logical name | Archive entry |
| --- | --- |
| `bectext.igb` | `ndmw.afs[79]` (original font atlas) |
| `_common/m_agumon.igb` | `ndmw.afs[834]` |

## Tools and validation

```powershell
.\build\Release\dw4_catalog.exe 'PATH\Extracted'
.\build\Release\dw4_catalog.exe 'PATH\Extracted' --find agumon
.\build\Release\dw4_catalog.exe 'PATH\Extracted' --read '_common/m_agumon.igb'
.\build\Release\dw4_resources_tests.exe 'PATH\Extracted'
```

The CLI lists the first 20 paths by default. `--find` lists all matching paths;
`--read` resolves and reads at most 64 MiB, then loads the object document for an
IGB payload. It writes no files. A read of `_common/m_agumon.igb` retrieved
265,305 bytes and parsed 2,044 object records and 277 memory records.

Synthetic tests cover lookup, unavailable records, unsafe path components,
conflicting aliases and archive/entry bounds. The optional local integration
test validates the actual setup and compares 20 logical reads byte-for-byte
against direct archive reads. CI does not require original data.

The texture viewer offers a catalog filter and resource picker, or accepts
`--resource bectext.igb`. Resource mounting does not implement cross-document
Alchemy object linking, scenes, asset replacement or a mod loader.
