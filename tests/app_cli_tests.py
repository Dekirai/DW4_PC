"""Asset-free native app option validation, without opening a graphics context.

Run: python tests/app_cli_tests.py path/to/dw4_pc[.exe]
Every subprocess receives --help before the tested options. The app validates
all options before returning help, so no game files or GPU are required.
"""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest


class AppCliTests(unittest.TestCase):
    executable: Path

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="dw4_app_cli_")
        self.addCleanup(self.temporary.cleanup)

    def invoke(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(self.executable), "--help", *arguments],
            cwd=self.temporary.name,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=15,
            check=False,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )

    def accepted(self, arguments: list[str]) -> None:
        result = self.invoke(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Usage: dw4_pc", result.stdout)
        self.assertIn("--scene-player-resource", result.stdout)
        self.assertEqual(result.stderr, "")

    def rejected(self, arguments: list[str], message: str) -> None:
        result = self.invoke(*arguments)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("dw4_pc:", result.stderr)
        self.assertIn(message, result.stderr)
        self.assertEqual(result.stdout, "")

    @staticmethod
    def player_options() -> list[str]:
        # Synthetic names and zero indices deliberately require no real assets.
        return [
            "--resource", "synthetic-map.igb", "--scene-root", "0",
            "--scene-player-resource", "synthetic-player.igb",
            "--scene-player-model", "0", "--scene-player-slot", "0",
            "--scene-player-branch", "0", "--scene-player-point", "0",
            "--scene-player-group", "0",
        ]

    @staticmethod
    def replace(arguments: list[str], option: str, value: str) -> list[str]:
        result = arguments.copy()
        result[result.index(option) + 1] = value
        return result

    def test_help_and_valid_full_selection(self) -> None:
        self.accepted([])
        base = self.player_options()
        self.accepted(base)
        self.accepted([
            "--package", "synthetic-package.igb", "--package-member", "0",
            *base[2:], "--scene-player-animation", "synthetic-clip",
            "--scene-player-time", "0.5", "--scene-player-play",
            "--smoke-test", "2", "--page", "scenes", "--scene-mode", "textured",
            "--scene-switch-control", "0", "--scene-switch-command", "all-on",
        ])
        self.accepted(["--archive", "synthetic.afs", "--entry", "0", *base[2:]])

    def test_every_player_selector_is_explicit(self) -> None:
        base = self.player_options()
        for option in (
            "--scene-root", "--scene-player-resource", "--scene-player-model",
            "--scene-player-slot", "--scene-player-branch", "--scene-player-point",
            "--scene-player-group",
        ):
            with self.subTest(option=option):
                incomplete = base.copy()
                start = incomplete.index(option)
                del incomplete[start:start + 2]
                self.rejected(incomplete, "Player inspection requires")
        self.rejected(self.replace(base, "--scene-player-resource", ""), "Player inspection requires")

    def test_player_numeric_syntax_and_ranges(self) -> None:
        base = self.player_options()
        limits = {
            "--scene-player-model": 1048575, "--scene-player-slot": 3,
            "--scene-player-branch": 4095, "--scene-player-point": 1048575,
            "--scene-player-group": 4095,
        }
        for option, maximum in limits.items():
            with self.subTest(option=option, boundary=maximum):
                self.accepted(self.replace(base, option, str(maximum)))
            for value in ("-1", str(maximum + 1), "+1", "1x", "1.5", "", "2147483648"):
                with self.subTest(option=option, value=value):
                    self.rejected(self.replace(base, option, value), option + " requires")

    def test_missing_values_and_unknown_player_option(self) -> None:
        self.rejected(["--scene-player-model"], "Missing value for --scene-player-model")
        self.rejected(["--scene-player-time"], "Missing value for --scene-player-time")
        self.rejected(["--scene-player-unknown", "0"], "Unknown option: --scene-player-unknown")

    def test_duplicate_player_options_rejected(self) -> None:
        base = self.player_options()
        for option, value in (
            ("--scene-player-resource", "other.igb"), ("--scene-player-model", "1"),
            ("--scene-player-slot", "1"), ("--scene-player-branch", "1"),
            ("--scene-player-point", "1"), ("--scene-player-group", "1"),
        ):
            with self.subTest(option=option):
                self.rejected([*base, option, value], "Duplicate player inspection option")
        self.rejected([*base, "--scene-player-play", "--scene-player-play"], "Duplicate player inspection option")
        self.rejected([
            *base, "--scene-player-animation", "a", "--scene-player-animation", "b",
        ], "Duplicate player inspection option")
        self.rejected([*base, "--scene-player-time", "0", "--scene-player-time", "1"], "Duplicate player inspection option")

    def test_animation_requires_nonempty_clip(self) -> None:
        base = self.player_options()
        for options in (
            ["--scene-player-time", "0"], ["--scene-player-play"],
            ["--scene-player-animation", ""],
        ):
            with self.subTest(options=options):
                self.rejected([*base, *options], "require a nonempty --scene-player-animation")

    def test_animation_time_is_finite_nonnegative_and_complete(self) -> None:
        base = [*self.player_options(), "--scene-player-animation", "synthetic-clip"]
        for value in ("nan", "inf", "-inf", "1e309", "-0.1", "1x", " 1", "1 ", ""):
            with self.subTest(value=value):
                self.rejected([*base, "--scene-player-time", value], "finite, nonnegative seconds")
        for value in ("0", "0.5", "1e2"):
            with self.subTest(valid=value):
                self.accepted([*base, "--scene-player-time", value])

    def test_scene_page_and_mesh_modes_cannot_conflict(self) -> None:
        base = self.player_options()
        self.rejected([*base, "--page", "meshes"], "Scene options require --page scenes")
        for options in (
            ["--mesh", "0"], ["--mesh-mode", "wireframe"], ["--texture", "0"],
            ["--rig"], ["--animation", "synthetic-mesh-clip"],
        ):
            with self.subTest(options=options):
                self.rejected([*base, *options], "Scene inspection cannot be combined")

    def test_scene_source_is_explicit_and_unambiguous(self) -> None:
        base = self.player_options()
        self.rejected(base[2:], "selection require --package, --resource, or --archive and --entry")
        self.rejected([*base, "--archive", "synthetic.afs"], "Use --archive NAME and --entry INDEX together")
        self.rejected([*base, "--entry", "0"], "Use --archive NAME and --entry INDEX together")
        self.rejected([*base, "--archive", "synthetic.afs", "--entry", "0"], "source options cannot be combined")
        self.rejected([*base, "--package", "synthetic-package.igb"], "source options cannot be combined")
        self.rejected(["--package", "synthetic-package.igb", *base[2:]], "explicit --package-member INDEX")

    def test_playback_smoke_requires_multiple_frames(self) -> None:
        base = [*self.player_options(), "--scene-player-animation", "synthetic-clip", "--scene-player-play"]
        self.rejected([*base, "--smoke-test", "1"], "Player playback smoke testing requires at least two frames")
        self.accepted([*base, "--smoke-test", "2"])
        self.accepted([*self.player_options(), "--smoke-test", "1"])

    def test_switch_command_and_controller_are_paired(self) -> None:
        base = self.player_options()
        self.rejected([*base, "--scene-switch-control", "0"], "--scene-switch-command all-on|all-off together")
        self.rejected([*base, "--scene-switch-command", "all-on"], "--scene-switch-command all-on|all-off together")
        self.rejected([*base, "--scene-switch-command", "ALLON"], "requires all-on or all-off")
        self.rejected([
            *base, "--scene-switch-control", "0", "--scene-switch-command", "all-on",
            "--scene-inspect-child", "0",
        ], "cannot be combined")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tests/app_cli_tests.py path/to/dw4_pc[.exe]")
    AppCliTests.executable = Path(sys.argv.pop()).resolve(strict=True)
    if not AppCliTests.executable.is_file():
        raise SystemExit("The app executable must be a file.")
    unittest.main()
