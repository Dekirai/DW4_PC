#!/usr/bin/env python3
"""Write the frozen names list (English -> translation) for the dialogue packages.

    python tools/l10n/freeze_names.py --source work/l10n/source/game_en.tsv \
        --catalog local/lang/de/game.tsv [--extra FILE ...] --out work/l10n/frozen_names.tsv

Every database name the export marks as class db_name or card (weapons, armor, chips,
items, MP techniques, panel states, Digivolution and card names) is listed with
its catalog translation, plus the --extra rows (columns: english, translation,
kind, notes; e.g. status labels, facilities, speakers, names only the dialogue
quotes). Dialogue quotes a name without its icon, so each row also gives the
bare form (icon tokens and edge spaces dropped). A name without a translation
is listed with an empty one and counted: the dialogue packages need it first.
A bare English name with two different bare translations is reported.

The files hold game text: keep them local (local/, work/); this tool holds none.
"""
import argparse
import re
import sys

ICON = re.compile(r'\{I_[A-Z0-9]+\}')
KIND = {
    'weapon': 'weapon', 'armor1': 'armor (core)', 'armor2': 'armor (board)', 'abChip': 'chip',
    'conItem': 'item (disk)', 'impItem': 'key item', 'insItem': 'instant item', 'pcTech': 'MP technique',
    'wazaPanel': 'panel state', 'shinka': 'Digivolution', 'card': 'card',
}


def rows(path):
    out, header = [], None
    with open(path, encoding='utf-8') as f:
        for line in f.read().split('\n'):
            if not line.strip() or line.startswith('#'):
                continue
            c = line.split('\t')
            if header is None and c[0] in ('id', 'english'):
                header = c
                continue
            out.append(c)
    return header, out


def bare(s):
    return ICON.sub('', s).strip(' ')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', required=True)
    ap.add_argument('--catalog', required=True)
    ap.add_argument('--extra', action='append', default=[], help='english, translation, kind[, notes] rows')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()

    tr = {}
    for c in rows(a.catalog)[1]:
        if len(c) >= 3 and (c[0] in ('', '*') or c[0].startswith('db:')):
            tr.setdefault(c[1], c[2])
    head, src = rows(a.source)
    col = {name: i for i, name in enumerate(head)}
    names, seen = [], set()
    for c in src:
        if c[col['class']] not in ('db_name', 'card'):
            continue
        en, where = c[1], c[col['where']]
        parts = where.split(':')
        kind = KIND.get(parts[1], parts[1]) if len(parts) > 1 else where
        names.append((en, tr.get(en, ''), kind, where))
        seen.add(en)
    for path in a.extra:
        for c in rows(path)[1]:
            c = c + [''] * 4
            if c[0] in seen:
                continue
            seen.add(c[0])
            names.append((c[0], c[1] or tr.get(c[0], ''), c[2], c[3]))

    missing = [n for n in names if not n[1]]
    by_bare = {}
    for en, de, kind, where in names:
        if de:
            by_bare.setdefault(bare(en), set()).add(bare(de))
    clashes = {k: v for k, v in by_bare.items() if len(v) > 1}
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Frozen names (English -> translation) for the dialogue packages: quote them exactly (the bare form without icons in running text).\n')
        f.write('# Derived from the game: local only, never commit or share.\n')
        f.write('english\ttranslation\tenglish_bare\ttranslation_bare\tkind\twhere\n')
        for en, de, kind, where in sorted(names, key=lambda n: (n[2], bare(n[0]).casefold())):
            f.write('\t'.join((en, de, bare(en), bare(de), kind, where)) + '\n')
    print(f'{len(names)} names -> {a.out}; {len(missing)} without a translation; {len(clashes)} bare names with two translations')
    for en, de, kind, where in missing:
        print(f'  untranslated: {kind}: {en!r} ({where})')
    for k, v in clashes.items():
        print(f'  two translations: {k!r} -> {sorted(v)!r}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
