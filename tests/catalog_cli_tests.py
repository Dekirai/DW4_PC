"""Asset-free argument checks for the native package/catalog CLI.

Run: python tests/catalog_cli_tests.py path/to/dw4_catalog[.exe]
Package payloads and catalog reads are covered by the C++ package fixtures.
"""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class CatalogTests(unittest.TestCase):
    executable: Path

    def invoke(self, *arguments: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [str(self.executable), *arguments], capture_output=True,
            text=True, encoding="utf-8", timeout=20, check=False,
        )

    def rejected(self, arguments: list[str], message: str) -> None:
        with tempfile.TemporaryDirectory(prefix="dw4_catalog_cli_") as directory:
            result = self.invoke(str(Path(directory) / "missing"), *arguments)
        self.assertEqual(result.returncode, 1)
        self.assertIn(message, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_help_and_no_arguments(self) -> None:
        result = self.invoke("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("--package", result.stdout)
        self.assertIn("--member", result.stdout)
        self.assertEqual(self.invoke().returncode, 2)

    def test_member_requires_package_including_zero(self) -> None:
        for value in ("0", "1", "4294967295"):
            with self.subTest(value=value):
                self.rejected(["--member", value], "--member requires --package")

    def test_member_rejects_partial_signed_and_overflow_values(self) -> None:
        for value in ("", "-1", "+1", "1x", " 1", "1 ", "1.5", "4294967296"):
            with self.subTest(value=value):
                self.rejected(["--package", "synthetic.igb", "--member", value], "unsigned decimal")

    def test_package_source_is_unambiguous(self) -> None:
        self.rejected(["--package", ""], "Package path is empty")
        for option in ("--read", "--find"):
            with self.subTest(option=option):
                self.rejected(["--package", "synthetic.igb", option, "x"], "exclusive")
        self.rejected(["--package", "a.igb", "--package", "b.igb"], "Duplicate")
        self.rejected(["--package", "a.igb", "--member", "0", "--member", "1"], "Duplicate")

    def test_missing_and_unknown_options(self) -> None:
        self.rejected(["--package"], "Missing option value")
        self.rejected(["--package", "a.igb", "--member"], "Missing option value")
        self.rejected(["--unknown", "x"], "Unknown option")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tests/catalog_cli_tests.py path/to/dw4_catalog[.exe]")
    CatalogTests.executable = Path(sys.argv.pop()).resolve(strict=True)
    unittest.main()
