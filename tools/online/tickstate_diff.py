"""Compares two raw tick states (docs/SEAMLESS.md 6.13.2-6.13.3, round J).

    python tickstate_diff.py A.bin B.bin

A tick state is the header (magic "DW4T", version u32, form u8, stage (u32 length + bytes), frame u64, load end u32,
topology hash u64, section count u32) and then sections (tag u32, length u32, bytes) in a fixed order; a manager's
section starts with its type name (u32 length + bytes). Prints both headers, every section's size and whether it is
equal, and for the first differing section the first differing byte with the bytes around it. Exit code 0 when both
states are equal, 1 when they differ, 2 on a damaged file.
"""
import struct
import sys

NAMES = {0: "journal", 9: "structure", 1: "engine", 2: "generator", 3: "system", 4: "messenger", 5: "demo", 6: "nodes",
         7: "attributes", 8: "lua"}


def parse(data):
    at = 0

    def take(n):
        nonlocal at
        if at + n > len(data):
            raise ValueError("truncated at byte %d" % at)
        chunk = data[at:at + n]
        at += n
        return chunk

    magic, version = struct.unpack("<II", take(8))
    if magic != 0x54345744:
        raise ValueError("not a tick state (magic %08x)" % magic)
    form = take(1)[0]
    (n,) = struct.unpack("<I", take(4))
    stage = take(n).decode("ascii", "replace")
    frame, load_end, topology, count = struct.unpack("<QIQI", take(24))
    header = {"version": version, "form": form, "stage": stage, "frame": frame, "load_end": load_end, "topology": "%016x" % topology}
    sections = []
    for _ in range(count):
        tag, length = struct.unpack("<II", take(8))
        body = take(length)
        name = NAMES.get(tag)
        if name is None:
            name = "manager %d" % (tag - 0x100)
            if len(body) >= 4:
                (k,) = struct.unpack("<I", body[:4])
                if 0 < k < 64 and 4 + k <= len(body):
                    name = body[4:4 + k].decode("ascii", "replace")
        sections.append((tag, name, body))
    if at != len(data):
        raise ValueError("%d trailing bytes" % (len(data) - at))
    return header, sections


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    try:
        a = parse(open(argv[1], "rb").read())
        b = parse(open(argv[2], "rb").read())
    except (OSError, ValueError, struct.error) as error:
        print("damaged: %s" % error)
        return 2
    print("A: %s" % a[0])
    print("B: %s" % b[0])
    differ = a[0] != b[0]
    first = None
    for i in range(max(len(a[1]), len(b[1]))):
        sa = a[1][i] if i < len(a[1]) else None
        sb = b[1][i] if i < len(b[1]) else None
        if sa is None or sb is None or sa[0] != sb[0]:
            print("section %d: present in one state only or in another order" % i)
            differ = True
            break
        equal = sa[2] == sb[2]
        print("%-22s %9d %9d %s" % (sa[1], len(sa[2]), len(sb[2]), "equal" if equal else "DIFFERS"))
        if not equal:
            differ = True
            if first is None:
                first = (sa, sb)
    if first:
        (_, name, x), (_, _, y) = first
        k = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), min(len(x), len(y)))
        lo = max(0, k - 16)
        print("first difference in %s at byte %d of %d / %d" % (name, k, len(x), len(y)))
        print("  A: %s" % x[lo:k + 32].hex(" "))
        print("  B: %s" % y[lo:k + 32].hex(" "))
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
