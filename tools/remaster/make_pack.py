#!/usr/bin/env python3
"""Builds a texture pack from upscale.py's results: per class (or per slot)
one variant, copied as <name>_<w>x<h>_<hash>.png, the naming the game's
loader reads (src/game/texture_pack.cpp: any PNG whose name ends in the
16-hex-digit pack hash, anywhere below the pack folder).

    python -I tools/remaster/make_pack.py --selection E:/DW4Remaster/pilot/selection.tsv \
        --up E:/DW4Remaster/pilot/up --out E:/DW4Remaster/pilot/pack \
        --choose environment=pbrify_v4,character=ultrasharp_v2 --override U2=pathb

Classes without a choice are left out. The folder must be new, empty or an
earlier pack of this tool (marked by .dw4_remaster_pack); its PNG files are
replaced. pack_manifest.tsv lists what went in (the loader ignores it).

Point the game at the pack:
  * test runs: DW4_TEXTURE_PACK=<folder> (hidden runs with --smoke-test);
  * play: F1 > Settings > Texture packs (settings.ini [mods] textures=<folder>),
    then Reload.
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402

MARKER = ".dw4_remaster_pack"


def parse_map(text):
    out = {}
    for item in filter(None, (s.strip() for s in text.split(","))):
        key, _, value = item.partition("=")
        if not value:
            sys.exit(f"bad mapping '{item}' (want key=variant)")
        out[key] = value
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selection", required=True)
    ap.add_argument("--up", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--choose", required=True, help="class=variant,... (the variant per class)")
    ap.add_argument("--override", default="", help="slot=variant,... (a slot's own variant; 'none' leaves it out)")
    args = ap.parse_args()
    choose = parse_map(args.choose)
    override = parse_map(args.override)
    selection = rmcommon.read_tsv(args.selection)

    if os.path.isdir(args.out) and os.listdir(args.out) and not os.path.exists(os.path.join(args.out, MARKER)):
        sys.exit(f"{args.out} is not empty and not a pack of this tool")
    os.makedirs(args.out, exist_ok=True)
    for name in os.listdir(args.out):
        if name.lower().endswith(".png"):
            os.remove(os.path.join(args.out, name))
    open(os.path.join(args.out, MARKER), "w").close()

    lines = ["slot\thash\tclass\tvariant\tfile"]
    for sel in selection:
        variant = override.get(sel["slot"], choose.get(sel["class"]))
        if not variant or variant == "none":
            continue
        if variant not in sel["variants"].split(","):
            sys.exit(f"{sel['slot']}: variant {variant} was not computed for it ({sel['variants']})")
        src = os.path.join(args.up, variant, sel["file"])
        if not os.path.exists(src):
            sys.exit(f"{sel['slot']}: missing {src}")
        shutil.copyfile(src, os.path.join(args.out, sel["file"]))
        lines.append("\t".join([sel["slot"], sel["hash"], sel["class"], variant, sel["file"]]))
    with open(os.path.join(args.out, "pack_manifest.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(lines) - 1} replacements -> {args.out}")


if __name__ == "__main__":
    main()
