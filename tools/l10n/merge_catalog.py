#!/usr/bin/env python3
"""Merge translation fragments into a language catalog (game.tsv).

    python tools/l10n/merge_catalog.py --catalog local/lang/de/game.tsv \
        --fragment work/l10n/chunks/P1.tsv --fragment work/l10n/chunks/P2.tsv \
        [--resolve FILE] [--prefer catalog|fragment] [--source work/l10n/source/game_en.tsv] \
        [--out FILE] [--report FILE] [--strict]

Every file is in the game.tsv form (docs/L10N.md section 2): '#' comment lines,
an optional header row whose first column reads 'id', then
    id <TAB> english <TAB> translation [<TAB> flags [<TAB> notes ...]]
A row's key is (id, english): the engine keys a content entry by its exact
English text, an id row by its id. The catalog's rows keep their order; a
fragment's new rows follow in fragment order. Merging the same fragments again
changes nothing.

A conflict is one key with two different translations (or the same translation
with other flags). The catalog's row is kept (--prefer catalog, the default) or
the fragment's (--prefer fragment); a row of the --resolve file (same form)
decides its key for good, whatever the catalog and the fragments say. The
report lists every conflict with all candidates and what was kept, plus
English texts that differ only in their edges (spaces, {BR}) or letter case
but got different translations, and fragment rows whose English text is not in
the --source export. --strict exits 2 while a conflict has no --resolve row.

The files hold game text: keep them local (local/, work/); this tool holds none.
"""
import argparse
import os
import re
import sys

EDGE = re.compile(r'^(?: |\{BR\})*|(?: |\{BR\})*$')


def read_tsv(path):
    """Returns (comment lines before the first row, header or None, rows); a row is a list of cells."""
    comments, header, rows = [], None, []
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f.read().split('\n'), 1):
            if not line.strip():
                continue
            if line.startswith('#'):
                if not rows and header is None:
                    comments.append(line)
                continue
            cells = line.split('\t')
            if header is None and not rows and cells[0] == 'id':
                header = cells
                continue
            if len(cells) < 3:
                sys.exit(f'{path}:{n}: fewer than 3 columns')
            while len(cells) < 4:
                cells.append('')
            rows.append(cells)
    return comments, header, rows


def core(text):
    return EDGE.sub('', text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--catalog', required=True, help='the catalog to merge into (read; written unless --out)')
    ap.add_argument('--fragment', action='append', default=[], help='a fragment, in order (repeatable)')
    ap.add_argument('--resolve', help='rows that decide conflicting keys (game.tsv form)')
    ap.add_argument('--prefer', choices=('catalog', 'fragment'), default='catalog')
    ap.add_argument('--source', help='the English export (dw4_l10n export): flags rows not in the corpus')
    ap.add_argument('--out', help='write here instead of over --catalog')
    ap.add_argument('--report', help='write the report here too')
    ap.add_argument('--strict', action='store_true', help='exit 2 on an unresolved conflict')
    a = ap.parse_args()

    out_lines = []

    def say(s=''):
        print(s)
        out_lines.append(s)

    comments, header, base = read_tsv(a.catalog)
    order, rows, origin = [], {}, {}
    for r in base:
        k = (r[0], r[1])
        if k in rows:
            say(f'catalog: duplicate key {k!r} (first row kept)')
            continue
        order.append(k)
        rows[k] = r
        origin[k] = os.path.basename(a.catalog)

    resolve = {}
    if a.resolve:
        for r in read_tsv(a.resolve)[2]:
            resolve[(r[0], r[1])] = r

    corpus = None
    if a.source:
        corpus = {r[1] for r in read_tsv(a.source)[2]}

    conflicts = {}  # key -> list of (origin, row)
    added, same, not_in_corpus = {}, 0, []
    for frag in a.fragment:
        name = os.path.basename(frag)
        n_new = 0
        for r in read_tsv(frag)[2]:
            k = (r[0], r[1])
            if corpus is not None and r[0] == '' and r[1] not in corpus:
                not_in_corpus.append((name, r[1]))
            if k not in rows:
                order.append(k)
                rows[k] = r
                origin[k] = name
                n_new += 1
                continue
            old = rows[k]
            if old[2] == r[2] and old[3] == r[3]:
                same += 1
                continue
            conflicts.setdefault(k, [(origin[k], old)]).append((name, r))
            if a.prefer == 'fragment':
                rows[k] = r
                origin[k] = name
        added[name] = n_new

    unresolved = 0
    applied = 0
    for k, r in resolve.items():
        if k not in rows:
            order.append(k)
        if rows.get(k) != r:
            applied += 1
        rows[k] = r
        origin[k] = os.path.basename(a.resolve)

    say(f'catalog {a.catalog}: {len(base)} rows')
    for name, n in added.items():
        say(f'  + {name}: {n} new rows')
    say(f'  identical rows already present: {same}')
    say(f'  resolve rows applied: {applied}')
    say(f'conflicts (same key, different translation or flags): {len(conflicts)}')
    for k, cands in conflicts.items():
        fixed = k in resolve
        unresolved += not fixed
        say(f'  [{"resolved" if fixed else "kept " + origin[k]}] id={k[0]!r} english={k[1]!r}')
        for name, r in cands:
            say(f'      {name}: {r[2]!r} flags={r[3]!r}')
        if fixed:
            say(f'      -> {resolve[k][2]!r} flags={resolve[k][3]!r}')

    # English texts that differ only in edges or case: one meaning, so one translation core.
    groups = {}
    for k in order:
        if k[0] == '':
            groups.setdefault(core(k[1]).casefold(), []).append(k)
    variants = []
    for g in groups.values():
        cores = {core(rows[k][2]) for k in g}
        if len(g) > 1 and len(cores) > 1:
            variants.append(g)
    say(f'edge / case variants with different translations: {len(variants)}')
    for g in variants:
        for k in g:
            say(f'      {k[1]!r} -> {rows[k][2]!r}')

    if corpus is not None:
        say(f'fragment rows not in the source export: {len(not_in_corpus)}')
        for name, e in not_in_corpus:
            say(f'      {name}: {e!r}')

    def trimmed(cells):
        cells = list(cells)
        while len(cells) > 4 and cells[-1] == '':
            cells.pop()
        return cells

    hdr = ['id', 'english', 'translation', 'flags']
    if any(len(trimmed(rows[k])) > 4 for k in order):
        hdr.append('notes')
    out = a.out or a.catalog
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        for c in comments:
            f.write(c + '\n')
        f.write('\t'.join(hdr) + '\n')
        for k in order:
            f.write('\t'.join(trimmed(rows[k])) + '\n')
    say(f'{len(order)} rows -> {out}')
    if a.report:
        with open(a.report, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(out_lines) + '\n')
    if a.strict and unresolved:
        sys.exit(2)


if __name__ == '__main__':
    main()
