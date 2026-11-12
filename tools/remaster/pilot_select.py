#!/usr/bin/env python3
"""Writes the pilot selection (31 textures across all classes, plus one
palette variant pair) as a TSV for upscale.py, contact_sheets.py and
make_pack.py, checked against the export manifest.

    python -I tools/remaster/pilot_select.py --export E:/DW4Remaster/export --out E:/DW4Remaster/pilot/selection.tsv

The picks come first from what the hidden test routes show
(work/dref/scripts/newgame_dng101b1.txt: title, character select, the
Humid Cave loading card, dng101b1; newgame_lobby.txt: the lobby stagml11).
Hashes are the renderer's pack hashes (TexturePack::image_hash).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402

ENV = "lanczos,pbrify_v4,pbrify_v3,hdcube3"
CHAR = "lanczos,ultrasharp_v2,pbrify_v4,hdcube3"
CHAR_B = CHAR + ",pathb"
EFFECT = "lanczos,pbrify_v3,lanczos_sharp"
FONT = "lanczos,pathb,ultrasharp_v2,hdcube3"

# slot, hash, class, variants, cells (atlas cells cut one by one), note
PILOT = [
    ("E1", "05f8453aab0ffa8d", "environment", ENV, 0, "dng101b1 moss floor, tiles on s (route)"),
    ("E2", "10c517ee7027e951", "environment", ENV, 0, "dng101b1 floor, border-to-border UVs (route)"),
    ("E3", "6aa861fa14e61bc5", "environment", ENV, 0, "fs3btil6 stone floor, tiles on s and t"),
    ("E4", "c902e5e08e419118", "environment", ENV, 0, "dng101b1 cave wall (route)"),
    ("E5", "7810b8d48479cf66", "environment", ENV, 0, "ML01GR00_n lobby structure atlas (lobby route)"),
    ("E6", "2d765aa2f58943d0", "environment", ENV, 0, "Wat00 water (sphere-mapped, blended)"),
    ("E7", "0efd4f52ec860c11", "environment", ENV, 0, "TA02BJ16 foliage cut-out (alpha test GREATER 0.6)"),
    ("E8", "33598159e6a13519", "environment", ENV, 0, "unnamed BC1 prop of the dungeon set (route)"),
    ("C1", "15f547b59aec7d42", "character", CHAR, 0, "P_dorumon01 256 (character select)"),
    ("C2", "2bd7f7dc8b54f03e", "character", CHAR, 0, "P_dorumon02 64 (in play, route)"),
    ("C3", "c82af955b2701ee7", "character", CHAR, 0, "unnamed 64 enemy skin of dng101b1 (probably Numemon)"),
    ("C4", "2d7da3451408597a", "character", CHAR, 0, "N_dijilff01 Digi-Elf NPC (lobby route)"),
    ("C5", "528f223572a17dc5", "character", CHAR, 0, "F_luce boss (contact sheet only)"),
    ("W1", "635f3332c7e4a816", "item", CHAR, 0, "w_common07 weapon (route)"),
    ("W2", "5f5819fdcfc9e4fb", "item", CHAR, 0, "sfaichin item pickup (dng101b1)"),
    ("X1", "172ac9831edec68f", "effect", EFFECT, 0, "x_100trnsin01a additive (lobby route)"),
    ("X2", "ddb793f70f07a33c", "effect", EFFECT, 0, "x_200fire01 additive"),
    ("X3", "73fe8fab2f183154", "effect", EFFECT, 0, "y_endmg01b additive and blended"),
    ("X4", "d417d34c1a07e099", "effect", EFFECT, 0, "x_200dark01a soft particle, normal blend"),
    ("U1", "472c41325d2b24bf", "ui", CHAR, 0, "messwinmst message window (route)"),
    ("U2", "e6f805492c827ecf", "ui", CHAR_B, 0, "num01 digits (route)"),
    ("U3", "7f95c2d9ffd0a206", "ui", CHAR, 1, "charaicon portraits, 16 cells (route)"),
    ("U4", "fc50dfecfe461591", "ui", CHAR, 0, "cursor (route)"),
    ("U5", "dce3cec48abddbd1", "ui", CHAR, 1, "stmojipnl01 status panel atlas (route)"),
    ("F1", "faccbeda501eefc1", "font", FONT, 1, "font00 glyphs and button pictures"),
    ("L1", "a587cce7c63abf90", "loading", CHAR, 0, "ldimg01 USA Humid Cave loading card (route)"),
    ("L2", "d3c1f8ed4d1121db", "loading", CHAR_B, 0, "loading2 Licensed by Nintendo (route)"),
    ("K1", "123f9e24ae1b6ed9", "card", CHAR, 0, "card01 Agumon"),
    ("K2", "9a2a7bcc2236e1be", "card", CHAR, 0, "card08 WarGreymon"),
    ("T1", "ac566f5a15c2a226", "title", CHAR_B, 0, "mojigc PRESS START (the renderer's hash special case)"),
    ("T2", "269fee9e73fa226b", "title", CHAR, 0, "logo title logo (route)"),
    ("P1", "eaf578e440cbe82d", "palette", ENV, 0, "Z23G02n palette variant 1 of 2 (lh_pack)"),
    ("P2", "94190acc4014da85", "palette", ENV, 0, "Z23G02n palette variant 2 of 2 (ls_pack)"),
]

COLUMNS = ["slot", "hash", "class", "variants", "cells", "file", "names", "width", "height", "note"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--export", required=True, help="dw4_texpack export folder")
    ap.add_argument("--out", required=True, help="selection TSV to write")
    args = ap.parse_args()
    manifest = rmcommon.read_manifest(args.export)
    rows = []
    for slot, h, cls, variants, cells, note in PILOT:
        if h not in manifest:
            sys.exit(f"{slot}: hash {h} is not in the export manifest")
        m = manifest[h]
        if cells and not rmcommon.read_cells(args.export, m):
            sys.exit(f"{slot}: {h} has no atlas cells")
        rows.append([slot, h, cls, variants, str(cells), m["file"], m["names"], m["width"], m["height"], note])
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(COLUMNS) + "\n")
        for r in rows:
            f.write("\t".join(r) + "\n")
    pilot = sum(1 for r in rows if r[2] != "palette")
    print(f"{len(rows)} textures ({pilot} pilot slots + {len(rows) - pilot} palette variants) -> {args.out}")


if __name__ == "__main__":
    main()
