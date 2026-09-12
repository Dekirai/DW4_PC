"""Asset-free CLI syntax checks; --help never initializes graphics or reads data."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

EXECUTABLES = [Path(value).resolve() for value in sys.argv[1:]]
sys.argv = sys.argv[:1]


class BootCliTests(unittest.TestCase):
    def invoke(self, exe, args, expected):
        with tempfile.TemporaryDirectory() as cwd:
            result = subprocess.run(
                [str(exe), "--help", *args], cwd=cwd, stdin=subprocess.DEVNULL,
                capture_output=True, text=True, encoding="utf-8", errors="replace",
                timeout=15, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def each(self, options, expected=0):
        for i, exe in enumerate(EXECUTABLES):
            with self.subTest(executable=exe.name, options=options):
                package = ["--boot-package" if i else "--package", "synthetic.igb"]
                self.invoke(exe, [*package, *options], expected)

    def test_explicit_states(self):
        for options in [[], ["--boot-difficulty", "2", "--boot-camera", "1"],
                        ["--boot-player-0", "p_test", "--boot-weapon-0-2", "w_test"],
                        ["--boot-player-3", "p_last"]]:
            self.each(options)

    def test_invalid_ranges(self):
        for option, value in [("--boot-difficulty", "3"), ("--boot-camera", "2"),
                              ("--boot-player-4", "p_test"), ("--boot-player-00", "p_test"),
                              ("--boot-weapon-0-3", "w_test"), ("--boot-weapon-4-0", "w_test")]:
            self.each([option, value], 1)

    def test_malformed_numbers(self):
        for value in ["", "-1", "+1", "1x", "1.0", "999999999999999999999"]:
            self.each(["--boot-difficulty", value], 1)

    def test_model_and_weapon_tokens(self):
        for value in ["", "../escape", "a/b", "a\\b", "a b", "a\x00b", "x" * 129]:
            if "\x00" in value:
                continue  # OS argument APIs reject embedded NUL before the application runs.
            self.each(["--boot-player-0", value], 1)
            self.each(["--boot-player-0", "p_test", "--boot-weapon-0-0", value], 1)

    def test_unjoined_weapon(self):
        self.each(["--boot-weapon-2-1", "w_test"], 1)

    def test_duplicate_state(self):
        for option, value in [("--boot-difficulty", "0"), ("--boot-player-1", "p_test")]:
            self.each([option, value, option, value], 1)

    def test_unknown_and_missing_options(self):
        for options in [["--boot-unknown", "0"], ["--boot-player-0"], ["--boot-camera"],
                        ["--boot-weapon-0", "w_test"], ["--boot-country", "0"],
                        ["--boot-area", "0"], ["--boot-pack-only", "0"]]:
            self.each(options, 1)

    def test_preliminary_stage_options(self):
        exe = EXECUTABLES[0]
        self.invoke(exe, ["--package", "synthetic.igb", "--preliminary"], 0)
        self.invoke(exe, ["--preliminary", "--preliminary"], 1)
        self.invoke(exe, ["--preliminary", "--boot-difficulty", "0"], 1)
        self.invoke(exe, ["--boot-player-0", "p_test", "--preliminary"], 1)
        self.invoke(exe, ["--preliminary", "--package"], 1)

    def test_app_source_guards(self):
        if len(EXECUTABLES) < 2:
            self.skipTest("The app executable was not supplied for this local check.")
        exe = EXECUTABLES[1]
        self.invoke(exe, ["--boot-player-0", "p_test"], 1)
        for extra in [["--page", "scenes"], ["--resource", "mesh.igb"],
                      ["--archive", "test.afs", "--entry", "0"],
                      ["--package", "another.igb"], ["--rig"], ["--scene-mode", "solid"], ["--max-entry-mib", "1"]]:
            self.invoke(exe, ["--boot-package", "synthetic.igb", *extra], 1)
        self.invoke(exe, ["--boot-package", "synthetic.igb", "--page", "startup"], 0)


if __name__ == "__main__":
    if not 1 <= len(EXECUTABLES) <= 2:
        raise SystemExit("Usage: boot_cli_tests.py DW4_BOOT [DW4_PC]")
    unittest.main()
