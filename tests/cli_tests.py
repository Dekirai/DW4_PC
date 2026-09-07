"""End-to-end inspector checks with generated, asset-free binary fixtures.

Run: python tests/cli_tests.py path/to/dw4_inspect[.exe]
"""

from __future__ import annotations

import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


def igb(name: bytes = b"SyntheticField\0") -> bytes:
    field_block = struct.pack("<III", len(name), 1, 0) + name
    header = bytearray(48)
    struct.pack_into("<IIII", header, 32, len(field_block), 1, 0xFADA, 6)
    return bytes(header) + field_block + struct.pack("<I", 4)


def afs(payload: bytes) -> bytes:
    header = b"AFS\0" + struct.pack("<III", 1, 32, len(payload))
    return header.ljust(32, b"\0") + payload


class InspectorTests(unittest.TestCase):
    executable: Path

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="dw4_cli_tests_")
        self.directory = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self, name: str, data: bytes) -> Path:
        path = self.directory / name
        path.write_bytes(data)
        return path

    def run_json(self, *args: object, code: int = 0) -> dict:
        process = subprocess.run(
            [str(self.executable), *(str(arg) for arg in args), "--json"],
            capture_output=True,
            timeout=20,
            check=False,
        )
        self.assertEqual(process.returncode, code, process.stderr.decode("utf-8", errors="replace"))
        # Strict UTF-8 and JSON parsing also reject partial JSON before an error.
        result = json.loads(process.stdout.decode("utf-8"))
        self.assertIsInstance(result, dict)
        return result

    def test_unicode_path_and_type_names(self) -> None:
        path = self.write("grüße-日本語.igb", igb("Feld_日本語\0".encode("utf-8")))
        result = self.run_json(path, "--list")
        self.assertEqual(result["path"], str(path))
        self.assertEqual(result["igb"]["field_types"][0]["name"], "Feld_日本語")
        self.assertEqual(result["igb"]["object_count"], 0)
        self.assertEqual(result["igb"]["scope"], "header_and_type_metadata")

    def test_invalid_name_encoding_replaced_in_json(self) -> None:
        path = self.write("encoding.igb", igb(b"Bad_\xff\xc0\xaf\xed\xa0\x80\0"))
        result = self.run_json(path, "--list")
        self.assertTrue(result["igb"]["field_types"][0]["name"].startswith("Bad_\ufffd"))

    def test_selected_archive_igb_with_zero_preview(self) -> None:
        path = self.write("archive.afs", afs(igb()))
        result = self.run_json(path, "--entry", 0, "--preview-bytes", 0)
        self.assertEqual(result["preview_hex"], "")
        self.assertEqual(result["igb"]["field_type_count"], 1)
        self.assertEqual(result["entries"][0]["index"], 0)

    def test_selected_unsupported_igb_is_diagnostic(self) -> None:
        data = bytearray(igb())
        struct.pack_into("<I", data, 44, 7)
        path = self.write("archive.afs", afs(data))
        result = self.run_json(path, "--entry", 0)
        self.assertIn("igb_error", result)
        self.assertEqual(result["entry_count"], 1)
        self.assertNotIn("igb", result)

    def test_out_of_range_entry_has_one_json_error(self) -> None:
        path = self.write("archive.afs", afs(igb()))
        result = self.run_json(path, "--entry", 1, code=1)
        self.assertEqual(set(result), {"error"})

    def test_preview_limit(self) -> None:
        path = self.write("archive.afs", afs(igb()))
        result = self.run_json(path, "--entry", 0, "--preview-bytes", 1048577, code=1)
        self.assertIn("error", result)

    def test_truncated_standalone_metadata(self) -> None:
        path = self.write("truncated.igb", igb()[:-1])
        result = self.run_json(path, code=1)
        self.assertEqual(set(result), {"error"})

    def test_missing_file(self) -> None:
        result = self.run_json(self.directory / "missing.afs", code=1)
        self.assertEqual(set(result), {"error"})


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tests/cli_tests.py path/to/dw4_inspect[.exe]")
    InspectorTests.executable = Path(sys.argv.pop()).resolve(strict=True)
    unittest.main()
