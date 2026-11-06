#!/usr/bin/env python3
"""Exports the graph of all stage scripts as data/online/stage_graph.json (docs/SEAMLESS.md 19.24, AreaTableTests).

Reads the decompiled stage scripts of area.afs (work/port-research/dec/<stage>_m1__<script>.lua, made by the research
tools; not in the repository) and writes, per stage, every way out a script names literally:

- funcStageChange("<stage>", <mode>, <message>, <point>) (the exit mats' prompt and departure),
- be:DMsetSD("STAGECTL", nil, -1, "NEXTSTAGE", "<stage>", <point>) (direct stage changes: warps, quest ends, ...),

and every Security Gate (funcSecurityGate("<stage>", <point>, ...)), the arrivals the lobby's return mat leads to.
Calls with a variable target (the bodies of funcStageChange itself, SYSgetGateStageName) are left out. Stage names are
written lower case; the edges are sorted, so a new export of the same scripts is byte-identical.

Usage: python tools/export_stage_graph.py [--dec DIR] [--out FILE]
"""
import argparse
import json
import os
import re
import sys

STAGE = r'"((?:stag|dng)[A-Za-z0-9]+)"'
CHANGE = re.compile(r'funcStageChange\(\s*' + STAGE + r'\s*,\s*[^,]*,\s*[^,]*,\s*(-?\d+)\s*\)')
NEXT = re.compile(r'"NEXTSTAGE"\s*,\s*' + STAGE + r'\s*,\s*(-?\d+)')
GATE = re.compile(r'funcSecurityGate\(\s*' + STAGE + r'\s*,\s*(-?\d+)')
FILE = re.compile(r'^((?:stag|dng)[A-Za-z0-9]+)_m1__(.+)\.lua$', re.IGNORECASE)


def export(dec):
    edges = set()
    gates = set()
    for name in sorted(os.listdir(dec)):
        m = FILE.match(name)
        if not m:
            continue
        stage, script = m.group(1).lower(), m.group(2)
        with open(os.path.join(dec, name), encoding='utf-8', errors='replace') as f:
            text = f.read()
        for kind, pattern in (('exit', CHANGE), ('nextstage', NEXT)):
            for target, point in pattern.findall(text):
                edges.add((stage, target.lower(), int(point), script, kind))
        for target, point in GATE.findall(text):
            if script != 'func':
                gates.add((target.lower(), int(point), stage, script))
    return {
        'format': 'dw4-stage-graph/1',
        'source': 'tools/export_stage_graph.py from the decompiled area.afs stage scripts (work/port-research/dec)',
        'edges': [{'from': a, 'to': b, 'point': p, 'script': s, 'kind': k} for a, b, p, s, k in sorted(edges)],
        'securityGates': [{'stage': a, 'point': p, 'script': f'{s}:{sc}'} for a, p, s, sc in sorted(gates)],
    }


def write(graph, out):
    lines = ['{', '  "format": "%s",' % graph['format'], '  "source": "%s",' % graph['source'], '  "edges": [']
    lines += ['    ' + json.dumps(e, separators=(', ', ': ')) + (',' if i + 1 < len(graph['edges']) else '')
              for i, e in enumerate(graph['edges'])]
    lines += ['  ],', '  "securityGates": [']
    lines += ['    ' + json.dumps(g, separators=(', ', ': ')) + (',' if i + 1 < len(graph['securityGates']) else '')
              for i, g in enumerate(graph['securityGates'])]
    lines += ['  ]', '}', '']
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--dec', default=os.path.join(root, 'work', 'port-research', 'dec'))
    ap.add_argument('--out', default=os.path.join(root, 'data', 'online', 'stage_graph.json'))
    args = ap.parse_args()
    if not os.path.isdir(args.dec):
        sys.exit(f'no decompiled scripts in {args.dec} (pass --dec)')
    graph = export(args.dec)
    write(graph, args.out)
    print(f"wrote {args.out}: {len(graph['edges'])} edges, {len(graph['securityGates'])} security gates")


if __name__ == '__main__':
    main()
