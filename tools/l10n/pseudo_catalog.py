#!/usr/bin/env python3
"""A pseudo-translated game.tsv for testing the display language (docs/L10N.md 4.7).

Reads a dw4_l10n export (the English corpus with its boxes) and writes a
game.tsv whose every text is visibly "translated" without changing its
meaning: vowels carry umlauts, an 's' inside a word becomes 'ß', the text
is wrapped in „ “ and its first word in ‚ ‘, and spaced hyphens become
dashes. Tokens ({BR}, {A}, {I_WEAPON} ...), the word START, digits, '/' and
';' stay as they are, so the engine accepts every row. Texts the game shows
in the travel dialog picture or elsewhere can be given exact translations
with --add (a TSV of english<TAB>translation in token form, read first).

The output is derived from the game: keep it local (work/, never committed).

    python tools/l10n/pseudo_catalog.py --source work/l10n/source/game_en.tsv \
        --out work/l10n_test/lang/de/game.tsv [--add extra.tsv] [--every N]

--every N translates only every Nth text (the rest stays English), to see
both side by side.
"""
import argparse
import re
import sys

UMLAUT = {"a": "ä", "o": "ö", "u": "ü", "A": "Ä", "O": "Ö", "U": "Ü"}
TOKEN = re.compile(r"(\{[A-Z0-9_]+\})")
START = re.compile(r"(\bSTART\b|ＳＴＡＲＴ)")


def pseudo_word(word):
    out = []
    for i, c in enumerate(word):
        if c in UMLAUT:
            out.append(UMLAUT[c])
        elif c == "s" and 0 < i < len(word) - 1 and word[i - 1].isalpha() and word[i + 1].isalpha():
            out.append("ß")
        else:
            out.append(c)
    return "".join(out)


def pseudo_plain(text):
    # (START stays a word: the prompt layout draws it as a picture.)
    parts = START.split(text)
    out = []
    for part in parts:
        if START.fullmatch(part):
            out.append(part)
        else:
            out.append(re.sub(r"[A-Za-z]+", lambda m: pseudo_word(m.group(0)), part).replace(" - ", " – "))
    return "".join(out)


def pseudo(text):
    pieces = TOKEN.split(text)
    body = "".join(piece if TOKEN.fullmatch(piece) else pseudo_plain(piece) for piece in pieces)
    # The first plain word in ‚ ‘ (the composed glyphs), the whole text in „ “.
    body = re.sub(r"^([^{\s]+)", lambda m: "‚" + m.group(1) + "‘", body, count=1)
    return "„" + body + "“"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--add", help="exact translations: english<TAB>translation per line")
    parser.add_argument("--every", type=int, default=1)
    args = parser.parse_args()

    exact = {}
    if args.add:
        with open(args.add, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\r\n")
                if not line or line.startswith("#"):
                    continue
                english, _, german = line.partition("\t")
                if german:
                    exact[english] = german
    revision = ""
    rows = []
    with open(args.source, encoding="utf-8-sig") as f:
        header = None
        for line in f:
            line = line.rstrip("\r\n")
            if line.startswith("# revision="):
                revision = line[len("# revision="):]
            if not line or line.startswith("#"):
                continue
            columns = line.split("\t")
            if header is None:
                header = {name: i for i, name in enumerate(columns)}
                continue
            rows.append(columns[header["english"]])
    out = ["# pseudo translation (tools/l10n/pseudo_catalog.py); derived from the game: keep it local"]
    if revision:
        out.append("# revision=" + revision)
    out.append("id\tenglish\ttranslation\tflags")
    done = set()
    for english, german in exact.items():
        out.append("\t%s\t%s\t" % (english, german))
        done.add(english)
    for i, english in enumerate(rows):
        if english in done or i % max(args.every, 1):
            continue
        out.append("\t%s\t%s\t" % (english, pseudo(english)))
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("pseudo catalog: %d texts (%d exact) -> %s" % (len(out) - 3 if revision else len(out) - 2, len(exact), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
