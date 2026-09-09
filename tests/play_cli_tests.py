"""Asset-free validation of the native play preview command line.

Run: python tests/play_cli_tests.py path/to/dw4_pc[.exe]
The application validates options before returning --help. These checks must
never load game data, create configuration files or open a graphics context.
Actual input, movement and rendering need the separate local play smoke run.
"""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest


class PlayCliTests(unittest.TestCase):
    executable: Path

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="dw4_play_cli_")
        self.addCleanup(self.temporary.cleanup)

    def invoke(self, arguments: list[str]) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
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
        self.assertEqual(list(Path(self.temporary.name).iterdir()), [],
                         "Argument validation must not create runtime files")
        return result

    def accepted(self, arguments: list[str]) -> None:
        result = self.invoke(arguments)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Usage: dw4_pc", result.stdout)
        self.assertIn("--play", result.stdout)
        self.assertIn("--play-smoke", result.stdout)
        self.assertEqual(result.stderr, "")

    def rejected(self, arguments: list[str], message: str = "--play") -> None:
        result = self.invoke(arguments)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("dw4_pc:", result.stderr)
        self.assertIn(message, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_help_exposes_play_without_reading_assets(self) -> None:
        self.accepted([])
        self.accepted(["--play"])
        self.accepted(["--play", "--data", "missing-original-game-directory"])

    def test_play_accepts_host_configuration_and_capture(self) -> None:
        self.accepted([
            "--config", "missing-settings.ini", "--data", "missing-game-data",
            "--play", "--smoke-test", "1", "--screenshot", "not-created.bmp",
        ])
        self.accepted(["--play", "--smoke-test", "120"])

    def test_replay_accepts_minimum_maximum_and_option_order(self) -> None:
        for frames in ("120", "10000"):
            with self.subTest(frames=frames):
                self.accepted(["--play", "--play-smoke", "--smoke-test", frames])
                self.accepted(["--smoke-test", frames, "--play-smoke", "--play"])

    def test_replay_requires_play_mode(self) -> None:
        self.rejected(["--play-smoke"])
        self.rejected(["--play-smoke", "--smoke-test", "120"])

    def test_duplicate_play_flags_are_rejected(self) -> None:
        self.rejected(["--play", "--play"], "Duplicate play option")
        self.rejected([
            "--play", "--play-smoke", "--smoke-test", "120", "--play-smoke",
        ], "Duplicate play option")

    def test_replay_requires_enough_finite_frames(self) -> None:
        self.rejected(["--play", "--play-smoke"])
        for frames in ("1", "2", "119"):
            with self.subTest(frames=frames):
                self.rejected(["--play", "--play-smoke", "--smoke-test", frames])
        for frames in ("0", "10001", "-1", "120x", "120.0", "nan", "", "+120"):
            with self.subTest(invalid_syntax=frames):
                self.rejected(["--play", "--play-smoke", "--smoke-test", frames],
                              "--smoke-test")

    def test_play_rejects_explicit_inspection_pages(self) -> None:
        for page in ("overview", "graphics", "controls", "assets", "textures",
                     "meshes", "scenes", "roadmap", "startup"):
            with self.subTest(page=page):
                self.rejected(["--play", "--page", page])

    def test_play_rejects_viewer_sources_and_selectors(self) -> None:
        alternatives = (
            ["--resource", "synthetic.igb"],
            ["--archive", "synthetic.afs", "--entry", "0"],
            ["--package", "synthetic.igb"],
            ["--resource", "synthetic.igb", "--texture", "0"],
            ["--resource", "synthetic.igb", "--mesh", "0"],
            ["--resource", "synthetic.igb", "--mesh", "0", "--mesh-mode", "textured"],
            ["--resource", "synthetic.igb", "--rig"],
            ["--resource", "synthetic.igb", "--animation", "synthetic-clip"],
            ["--resource", "synthetic.igb", "--scene-root", "0"],
            ["--resource", "synthetic.igb", "--scene-root", "0",
             "--scene-player-resource", "synthetic-player.igb",
             "--scene-player-model", "0", "--scene-player-slot", "0",
             "--scene-player-branch", "0", "--scene-player-point", "0",
             "--scene-player-group", "0"],
            ["--max-entry-mib", "64"],
        )
        for selectors in alternatives:
            with self.subTest(selectors=selectors):
                self.rejected(["--play", *selectors])
                self.rejected([*selectors, "--play"])

    def test_play_rejects_boot_inspection(self) -> None:
        self.rejected(["--play", "--boot-package", "synthetic.igb"])
        self.rejected([
            "--boot-package", "synthetic.igb", "--boot-player-0", "p_agumon", "--play",
        ])

    def test_play_keeps_capture_and_unknown_option_validation(self) -> None:
        self.rejected(["--play", "--screenshot", "not-created.bmp"], "--screenshot")
        self.rejected(["--play", "--play-unknown"], "--play-unknown")
        self.rejected(["--play", "--smoke-test"], "Missing value")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tests/play_cli_tests.py path/to/dw4_pc[.exe]")
    PlayCliTests.executable = Path(sys.argv.pop()).resolve(strict=True)
    if not PlayCliTests.executable.is_file():
        raise SystemExit("The app executable must be a file.")
    unittest.main()
