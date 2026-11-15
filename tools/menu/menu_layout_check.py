"""The console menu's layout check (docs/CONSOLE_MENU.md, "Checks", C) over the logs of runs with DW4_MENU_LAYOUT_LOG=1
(tools/menu/run.sh picture ...; a log's name: l_... the lower screen, s... another menu size):
  menu_layout_check.py OUTDIR REPORT
- rows that take a tap at least 48 u high (menu-layout: every row but sections, infos and progress bars is 52 u or more);
- touch targets: arrows and cells at least 44 u wide, buttons (back, close, confirm, capture, picker entries) at least
  48 u high, sidebar entries at least 40 u (an entry cut by the sidebar's edge, while it scrolls, is left out);
- nothing in the touch menu button's square (top screen, 1920 x 1080);
- the hint bar fits in one line.
"""
import os
import re
import sys

KIND = {0: "row", 1: "arrow_left", 2: "arrow_right", 3: "cell", 4: "category", 5: "back", 6: "close", 7: "picker_entry",
        8: "picker_outside", 9: "confirm_button", 10: "capture_cancel", 11: "capture_clear", 12: "text_done", 13: "text_cancel", 14: "card",
        15: "list_area", 16: "sidebar_area"}


def touch_rect(width, height, scale):
    side, margin = 44 * scale, 10 * scale
    x1 = width - margin
    x0 = x1 - side
    y0 = margin
    y1 = y0 + side
    bar = (width - height * 4.0 / 3.0) * 0.5
    if bar >= side + margin * 2:
        x0 = width - bar * 0.5 - side * 0.5
        x1 = x0 + side
    return x0, y0, x1, y1


def check(path):
    problems = []
    name = os.path.basename(path)[:-4]
    lower = name.startswith("l_")
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    scale = None
    # (The last block of hits: the final frame's targets.)
    blocks, block = [], []
    for line in lines:
        m = re.match(r"menu-hit: (\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) ([\d.]+)", line)
        if m:
            block.append(tuple(float(v) for v in m.groups()))
            scale = float(m.group(6))
        elif line.startswith("menu-hint:"):
            m = re.match(r"menu-hint: (\d+) of (\d+)", line)
            if m and float(m.group(1)) > float(m.group(2)) + 1:
                problems.append(f"the hint bar is wider than the screen ({m.group(1)} of {m.group(2)} px)")
            blocks.append(block)
            block = []
        m = re.match(r"menu-layout: (\S+) (\d+) \S+ \S+ \S+ (\d+) (\d+)", line)
        if m and int(m.group(2)) not in (0, 1, 13):
            row_h, font = float(m.group(3)), float(m.group(4))
            s = font / 19.0
            if row_h / s < 48 - 0.5:
                problems.append(f"row {m.group(1)} {row_h / s:.0f} u high")
    if not blocks or scale is None:
        return name, ["no layout log"]
    hits = blocks[-1]
    u = lambda px: px / scale
    rect = touch_rect(1920, 1080, scale) if not lower and not name.startswith("s") else None
    sidebar = next(((y0, y1) for kind, x0, y0, x1, y1, _ in hits if KIND.get(int(kind)) == "sidebar_area"), None)
    full = max((y1 - y0 for kind, x0, y0, x1, y1, _ in hits if KIND.get(int(kind)) == "category"), default=0)
    for kind, x0, y0, x1, y1, _ in hits:
        k = KIND.get(int(kind), str(kind))
        if k == "category" and sidebar and y1 - y0 < full - 0.5 and (y0 <= sidebar[0] + 0.5 or y1 >= sidebar[1] - 0.5):
            continue  # (cut by the sidebar's edge: it scrolls)
        w, h = u(x1 - x0), u(y1 - y0)
        if k in ("arrow_left", "arrow_right", "cell") and w < 44 - 0.5:
            problems.append(f"{k} {w:.0f} u wide")
        if k in ("back", "close", "confirm_button", "capture_cancel", "capture_clear") and h < 48 - 0.5:
            problems.append(f"{k} {h:.0f} u high")
        if k == "cell" and h < 48 - 0.5 and h > 20:
            problems.append(f"cell {h:.0f} u high")
        if k == "category" and 20 < h < 40 - 0.5:
            problems.append(f"category {h:.0f} u high")
        if rect and k not in ("picker_outside", "card", "row", "list_area", "sidebar_area") and not (x1 <= rect[0] or x0 >= rect[2] or y1 <= rect[1] or y0 >= rect[3]):
            problems.append(f"{k} in the touch button's square ({x0:.0f},{y0:.0f})-({x1:.0f},{y1:.0f})")
    return name, sorted(set(problems))


def main(out, report):
    results = []
    for f in sorted(os.listdir(out)):
        if f.endswith(".log"):
            results.append(check(os.path.join(out, f)))
    bad = [r for r in results if r[1]]
    with open(report, "w", encoding="utf-8") as f:
        for name, problems in results:
            f.write(f"{name}: {'ok' if not problems else '; '.join(problems)}\n")
        f.write(f"{len(results) - len(bad)} of {len(results)} pictures pass the layout check\n")
    print(f"{len(results) - len(bad)} of {len(results)} pictures pass the layout check")
    for name, problems in bad:
        print(name, problems)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
