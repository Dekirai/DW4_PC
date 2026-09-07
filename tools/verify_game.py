#!/usr/bin/env python3
"""Read-only identification and optional ISO/extracted-file consistency check.

Python 3.10+, standard library only. Reports metadata, never copies game data.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path, PurePosixPath

EXPECTED_DOL_SHA1 = "e409a88a7379ed1a536f93b0a303a0ce7cd5d877"
MAX_FST = 32 * 1024 * 1024


def read_exact(stream, count: int) -> bytes:
    data = stream.read(count)
    if len(data) != count:
        raise ValueError("Unexpected end of file")
    return data


def hash_region(stream, offset: int, length: int, algorithm="sha256") -> str:
    digest = hashlib.new(algorithm)
    stream.seek(offset)
    remaining = length
    while remaining:
        chunk = read_exact(stream, min(remaining, 1024 * 1024))
        digest.update(chunk)
        remaining -= len(chunk)
    return digest.hexdigest()


def parse_fst(data: bytes, disc_size: int) -> dict[str, tuple[int, int]]:
    """Validate the GameCube directory tree and return plain file extents."""
    if len(data) < 12:
        raise ValueError("Truncated FST root")
    kind_name, parent, count = struct.unpack_from(">III", data)
    if kind_name >> 24 != 1 or parent != 0 or not 1 <= count <= len(data) // 12:
        raise ValueError("Invalid FST root")
    strings = data[count * 12:]
    directories = [(count, 0, PurePosixPath())]
    entries = {}
    seen = set()
    for index in range(1, count):
        while directories and index >= directories[-1][0]:
            directories.pop()
        if not directories:
            raise ValueError("FST entry outside root")
        kind_name, offset, size = struct.unpack_from(">III", data, index * 12)
        kind, name_offset = kind_name >> 24, kind_name & 0xFFFFFF
        end = strings.find(b"\0", name_offset)
        if name_offset >= len(strings) or end == -1:
            raise ValueError("Invalid FST name offset")
        name = strings[name_offset:end].decode("ascii", errors="strict")
        if name in ("", ".", "..") or any(c in name for c in "/\\:"):
            raise ValueError("Unsafe or invalid FST name")
        path = directories[-1][2] / name
        key = path.as_posix()
        if key in seen:
            raise ValueError("Duplicate FST path")
        seen.add(key)
        if kind == 1:
            if offset != directories[-1][1] or not index < size <= directories[-1][0]:
                raise ValueError("Invalid FST directory extent")
            directories.append((size, index, path))
        elif kind == 0:
            if offset > disc_size or size > disc_size - offset:
                raise ValueError("FST file extends beyond ISO")
            entries[key] = (offset, size)
        else:
            raise ValueError("Invalid FST entry type")
    return entries


def verify(root: Path, iso: Path | None = None) -> dict:
    system = root / "sys" if (root / "sys").is_dir() else root
    files = root / "files" if (root / "files").is_dir() else root
    with (system / "boot.bin").open("rb") as stream:
        boot = read_exact(stream, 0x440)
    if struct.unpack_from(">I", boot, 0x1C)[0] != 0xC2339F3D:
        raise ValueError("boot.bin is not a GameCube header")
    game_id = boot[:6].decode("ascii", errors="strict")
    if game_id != "GDJEB2":
        raise ValueError(f"Expected USA GDJEB2, found {game_id}")
    dol_path = system / "main.dol"
    dol_size = dol_path.stat().st_size
    with dol_path.open("rb") as stream:
        dol_sha1 = hash_region(stream, 0, dol_size, "sha1")
    if dol_sha1 != EXPECTED_DOL_SHA1:
        raise ValueError(f"DOL differs from the supported reference build: {dol_sha1}")
    result = {
        "schema_version": 1,
        "game_id": game_id,
        "disc_number": boot[6],
        "disc_revision_byte": boot[7],
        "title": boot[0x20:0x400].split(b"\0", 1)[0].decode("ascii", errors="replace"),
        "dol": {"size": dol_size, "sha1": dol_sha1, "matches_reference": True},
        "iso_compared": iso is not None,
        "files": [],
    }
    if iso is None:
        for name in ("area.afs", "ndmw.afs", "ndmw.igb", "opening.bnr"):
            path = files / name
            with path.open("rb") as stream:
                size = path.stat().st_size
                result["files"].append({"name": name, "size": size, "sha256": hash_region(stream, 0, size)})
        return result

    with iso.open("rb") as disc:
        iso_size = iso.stat().st_size
        header = read_exact(disc, 0x440)
        if header != boot:
            raise ValueError("ISO boot header differs from extracted boot.bin")
        dol_offset, fst_offset, fst_size = struct.unpack_from(">III", header, 0x420)
        if dol_offset + dol_size > iso_size:
            raise ValueError("ISO DOL extent is invalid")
        if hash_region(disc, dol_offset, dol_size, "sha1") != dol_sha1:
            raise ValueError("ISO DOL differs from extracted main.dol")
        if fst_size < 12 or fst_size > MAX_FST or fst_offset + fst_size > iso_size:
            raise ValueError("Invalid ISO filesystem table extent")
        disc.seek(fst_offset)
        fst = read_exact(disc, fst_size)
        if (system / "fst.bin").read_bytes() != fst:
            raise ValueError("ISO filesystem table differs from extracted fst.bin")
        for name, (offset, size) in parse_fst(fst, iso_size).items():
            path = files.joinpath(*PurePosixPath(name).parts)
            if path.stat().st_size != size:
                raise ValueError(f"Extracted size differs: {name}")
            with path.open("rb") as stream:
                digest = hash_region(stream, 0, size)
            if hash_region(disc, offset, size) != digest:
                raise ValueError(f"Extracted content differs from ISO: {name}")
            result["files"].append({"name": name, "size": size, "sha256": digest, "matches_iso": True})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("extracted", type=Path, help="Dolphin extraction root (sys/ and files/)")
    parser.add_argument("--iso", type=Path, help="Optional original uncompressed ISO to compare byte-for-byte")
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.extracted, args.iso), indent=2))
        return 0
    except (OSError, ValueError, struct.error) as error:
        print(f"Verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
