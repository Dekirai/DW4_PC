# Original language data and selection

The supplied USA disc contains seven named language-pack variants, but it does
not establish seven translated game languages. The named text buffers in every
inspected French, German, Italian and Spanish pack are byte-identical to their
English counterparts. Japanese has substantially different text, including
Japanese script, and is a candidate for an optional language mode after runtime
and font validation. The current recompiled game retains its original USA
selection; an ImGui language selector is not implemented yet.

This audit covers the owner's extracted `GDJEB2` data and the USA executable
with SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. It does not establish
the contents of other regional releases or the availability of translations
from other sources. Original text buffers, font tables and local probes stay
under ignored `work/`; no game text or atlas payload is distributed here.

## Corpus evidence

The original logical catalog has 96 pack paths for each of `usa`, `eng`, `fra`,
`ger`, `ita`, `jpn` and `spn`: 672 paths in total. Each variant has 94 packs
containing named `beUnicodeObj` records. The remaining two are under `stagis20`
and `stagml14` and have no named text objects in this scan. Across each variant,
the 94 text-bearing packs contain 15,132 objects when keyed by their name within
each pack. This is a per-pack total, not a count of globally unique sentences.

The scan reads the original IGB graph through `MountedResources` and
`IgbDocument`. For each `beUnicodeObj`, it compares the original slot-3 memory
buffer under its slot-2 name. Conflicting duplicate names are rejected. The
comparison includes all buffer bytes, including terminator/padding bytes; it
does not merely compare filenames or a few selected messages. Text statistics
decode those buffers as UTF-16LE with trailing zero units removed.

| Variant | Text-bearing packs | Named text objects | Packs with exactly the same named buffers as ENG | Interpretation for the supplied USA data |
| --- | ---: | ---: | ---: | --- |
| USA | 94 | 15,132 | 73 | Original US English selection; differs from ENG in 21 packs |
| ENG | 94 | 15,132 | 94 | English comparison variant |
| FRA | 94 | 15,132 | 94 | English text duplicate; not verified French translation |
| GER | 94 | 15,132 | 94 | English text duplicate; not verified German translation |
| ITA | 94 | 15,132 | 94 | English text duplicate; not verified Italian translation |
| SPN | 94 | 15,132 | 94 | English text duplicate; not verified Spanish translation |
| JPN | 94 | 15,132 | 1 | Distinct Japanese text in 93 packs; gameplay support unverified |

The one JPN pack equal to ENG is `stag0000/separate/jpn_pack.igb`. The Japanese
buffers contain 276,630 occurrences in the Hiragana/Katakana and CJK ranges
used by the audit (`U+3040..U+30FF`, `U+3400..U+9FFF`), versus 3,192 in ENG.
These counts demonstrate substantial Japanese content; they do not measure
translation completeness. The equalities above concern named Unicode objects,
not whole IGB files, embedded images, voices or every possible text path.

## Original selector

The USA DOL's `SYSgetCountry` implementation at `0x802EBC90` returns literal
integer `1`. A fresh bounded read of the original instructions confirms this.
It does not read the host locale or a console-language preference. Changing a
runtime-wide language preference alone therefore cannot establish game-language
switching for this executable.

The inspected original boot Lua country helper adds one to that zero-based
value before indexing its prefix table:

| Country value | Prefix selected by the inspected boot helper |
| ---: | --- |
| 0 | `JPN_` |
| 1 | `USA_` |
| 2 | `ENG_` |
| 3 | `FRA_` |
| 4 | `ITA_` |
| 5 | `GER_` |
| 6 | `SPN_` |

With the original pack-only result, the helper queues the active folder's
`separate/<prefix>pack.igb`. The native resolver converts ASCII uppercase to
lowercase before matching the logical catalog, so the original `USA_pack.igb`
request resolves to `usa_pack.igb`. The original script's prefix order matters:
it differs from alphabetical order. See [STARTUP_HOST_RESEARCH.md](STARTUP_HOST_RESEARCH.md)
for the verified queue, registration and resolver behavior.

This identifies a concrete game-specific selection point. It does not prove
that overriding this one function changes every menu, movie, memory-card
message or later resource load. A localization implementation must inspect
those additional consumers and preserve the original default for this revision.

## Font-table coverage

The font-info records in the 658 text-bearing packs refer to the font names
`MINIMUM` and `NORMAL`. The shared `bectext.igb` is `ndmw.afs` entry 79. Its
two `beFontImage` objects refer to `igCharMetricsList` records 13 and 21.
Each list declares 484 entries backed by 19,360 bytes, or 40 bytes per entry.
Reading the leading little-endian 16-bit character value of each entry gives
484 unique, sorted values; the two lists contain the same character set.
That set includes 224 Hiragana/Katakana or CJK values in the audit's ranges.

Comparing decoded text values against those tables gives:

| Text variant | Distinct text values, including controls | Non-control values absent from both font tables |
| --- | ---: | ---: |
| USA | 365 | 55 |
| ENG, FRA, GER, ITA, SPN | 366 each | 56 each |
| JPN | 384 | 56 |

Carriage return and line feed are counted as controls and excluded from the
last column. The common 55 absent values occur only in five named objects of
each variant's `stag0000` pack. Their use during ordinary play has not been
established. Outside that pack, the only absent value is `U+3007`: it appears
in JPN `stagml12` object `mswk200932` and in the ENG-equivalent variants'
`stagml12` object `mswk11011`. No other non-control text value in this scanned
corpus is absent from the two inspected tables.

This is a static character-table comparison, not a rendered-glyph test. It does
not resolve the original renderer's fallback or substitution behavior, prove
that each table rectangle has the correct visible glyph, or establish whether
all scanned strings are reachable. Japanese therefore remains unverified for
normal gameplay even though the existing atlas contains much of its character
set. ImGui's own font atlas is separate: displaying a language name there does
not supply glyphs to the original game's text renderer.

## ImGui integration boundary

The planned selector should show only language modes whose content and runtime
behavior have been validated for the supplied revision. For the current USA
revision, retain English (USA) as the original default. Do not advertise French,
German, Italian or Spanish translations based on these pack names. An English
regional variant should be named as such if eventually exposed. Japanese can
be offered as supported only after the remaining runtime checks pass, or as an
explicitly experimental option during development.

Before promoting a language mode, verify the selected pack across startup,
main menus, character selection, dialogue, item/weapon descriptions, saving,
loading and stage transitions. Check visible glyphs, line wrapping, truncation,
button prompts and remaining English text. Resolve the `U+3007` case and
classify the `stag0000` strings rather than silently discarding them from
coverage. Apply changes at a verified reload/restart boundary so an already
loaded resource graph cannot retain a mixture of languages.

Keep language choice separate from game region and from developer-documentation
language. Future translation mods should declare compatible revision and
required font assets. Online compatibility must distinguish presentation-only
translations from packs that also alter gameplay data; a pack-name change alone
does not establish that distinction.

## Local audit artifacts

The audit reused the completed corpus scan and independently checked its
counts/equalities; it did not rescan every original pack. Among the original IGB
files, only the shared font file was read again to inspect its metric tables.
The bounded DOL selector read and DOL hash check were also repeated. No game or
runtime build was launched for this audit.

| Ignored local artifact | Purpose |
| --- | --- |
| `work/localization-catalog.txt` | Original logical paths and archive entries |
| `work/localization-probe.cpp` | Corpus field extraction through checked resource/IGB APIs |
| `work/localization-all-packs.tsv` | Original named text/font fields; never distribute |
| `work/localization-payload-summary.json` | Per-variant comparison and text statistics |
| `work/localization-font-probe.cpp` | Bounded shared-font field inspection |
| `work/localization-font-metrics.tsv` | Original character tables; never distribute |
| `work/localization-font-coverage.py` | Independent summary checks and static font comparison |
| `work/localization-font-coverage.json` | Counts, missing-value locations and input hashes |

The reused corpus TSV SHA-256 is
`532eed629630b9092d7e67866eedfa31758ff90ce129402d813b53ea521c7eed`.
The reused payload summary SHA-256 is
`e33bc1ea14ac33adcdcf786c5003c6aba9b2a5e06cac1999bfa8b48190dcc258`.
The shared-font metrics TSV SHA-256 is
`3a83d9f03e67ca60e733f7a5c1a19c1e123b17e1944f1aee690afea69b2bf4ae`.
