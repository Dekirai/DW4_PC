"""Synthetic tests for ISO extent checking; no original data needed."""
import importlib.util
import io
import struct
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("verify_game", Path(__file__).parents[1] / "tools" / "verify_game.py")
verify_game = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify_game)


def fst(entries, names):
    return struct.pack(">III", 0x01000000, 0, len(entries) + 1) + b"".join(
        struct.pack(">III", *entry) for entry in entries
    ) + names


class VerificationTests(unittest.TestCase):
    def test_nested_file_extents(self):
        data = fst([(0x01000000, 0, 3), (4, 64, 10), (10, 90, 10)], b"dir\0a.igb\0b.afs\0")
        self.assertEqual(verify_game.parse_fst(data, 100), {"dir/a.igb": (64, 10), "b.afs": (90, 10)})

    def test_invalid_extent_and_count(self):
        for data in (b"", b"\0" * 12, fst([(0, 95, 10)], b"a\0"), fst([(0x01000000, 0, 9)], b"d\0")):
            with self.subTest(data=data), self.assertRaises(ValueError):
                verify_game.parse_fst(data, 100)

    def test_rejects_bad_names(self):
        for name in (b"..\0", b"/escape\0", b"C:foo\0", b"a\\b\0", b"unterminated", b"\0"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                verify_game.parse_fst(fst([(0, 50, 1)], name), 100)

    def test_rejects_duplicate_directory_or_file(self):
        for entries in ([(0, 50, 1), (0, 60, 1)], [(0x01000000, 0, 2), (0, 60, 1)]):
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                verify_game.parse_fst(fst(entries, b"same\0"), 100)

    def test_rejects_bad_directory_parent(self):
        with self.assertRaises(ValueError):
            verify_game.parse_fst(fst([(0x01000000, 4, 2)], b"dir\0"), 100)

    def test_region_hash_and_truncation(self):
        self.assertEqual(verify_game.hash_region(io.BytesIO(b"XabcY"), 1, 3, "sha1"), "a9993e364706816aba3e25717850c26c9cd0d89d")
        with self.assertRaises(ValueError):
            verify_game.hash_region(io.BytesIO(b"abc"), 0, 4)


if __name__ == "__main__":
    unittest.main()
