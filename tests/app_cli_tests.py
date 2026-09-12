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
    def actor_options() -> list[str]:
        # The animation implies --rig and mesh zero; --help avoids asset reads.
        return [
            "--resource", "synthetic-actor.igb", "--animation", "synthetic-clip",
            "--actor-info", "0", "--actor-ram", "0", "--actor-index", "0",
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

    def test_actor_selection_accepts_implied_rig_and_explicit_mesh(self) -> None:
        base = self.actor_options()
        self.accepted(base)
        self.accepted([*base, "--rig", "--mesh", "1", "--page", "meshes"])
        self.accepted(["--archive", "synthetic.afs", "--entry", "0", *base[2:]])
        self.accepted([
            *base, "--animation-time", "0.5", "--animation-play", "--smoke-test", "2",
        ])

    def test_actor_selectors_and_animation_are_required_together(self) -> None:
        base = self.actor_options()
        for option in ("--actor-info", "--actor-ram", "--actor-index", "--animation"):
            with self.subTest(missing=option):
                incomplete = base.copy()
                start = incomplete.index(option)
                del incomplete[start:start + 2]
                self.rejected(incomplete, "Actor inspection requires")
        for option in ("--actor-info", "--actor-ram", "--actor-index"):
            with self.subTest(only=option):
                self.rejected(["--resource", "synthetic-actor.igb", option, "0"], "Actor inspection requires")
        self.rejected(self.replace(base, "--animation", ""), "--animation requires an original clip name")

    def test_actor_numeric_syntax_and_ranges(self) -> None:
        base = self.actor_options()
        limits = {"--actor-info": 1048575, "--actor-ram": 1023, "--actor-index": 63}
        for option, maximum in limits.items():
            with self.subTest(option=option, boundary=maximum):
                self.accepted(self.replace(base, option, str(maximum)))
            for value in ("-1", str(maximum + 1), "+1", "1x", "1.5", " 1", "1 ", "", "2147483648"):
                with self.subTest(option=option, value=value):
                    self.rejected(self.replace(base, option, value), option + " requires")

    def test_actor_missing_values_unknown_and_duplicate_options(self) -> None:
        base = self.actor_options()
        for option in ("--actor-info", "--actor-ram", "--actor-index"):
            with self.subTest(option=option, invalid="missing"):
                self.rejected([option], "Missing value for " + option)
            with self.subTest(option=option, invalid="duplicate"):
                self.rejected([*base, option, "0"], "Duplicate actor inspection option")
        self.rejected(["--actor-unknown", "0"], "Unknown option: --actor-unknown")

    def test_actor_selection_preserves_source_and_mode_constraints(self) -> None:
        base = self.actor_options()
        self.rejected(base[2:], "--rig requires --resource, or --archive and --entry")
        self.rejected([*base, "--scene-root", "0"], "Scene inspection cannot be combined")
        for options in (["--bone", "0"], ["--rotate-x", "0"], ["--rotate-y", "1"], ["--rotate-z", "-1"]):
            with self.subTest(manual=options):
                self.rejected([*base, *options], "Choose an animation clip or manual bone rotation")
        for option in ("--actor-info", "--actor-ram", "--actor-index"):
            with self.subTest(play=option):
                self.rejected(["--play", option, "0"], "--play cannot be combined")

    @staticmethod
    def weapon_options() -> list[str]:
        return [
            "--resource", "synthetic-player.igb", "--mesh", "0",
            "--actor-info", "0", "--actor-ram", "0", "--actor-index", "0",
            "--actor-model-info", "0", "--actor-action", "synthetic-action",
            "--actor-weapon-resource", "synthetic-weapon.igb",
            "--actor-weapon-name", "synthetic-weapon",
        ]

    def test_weapon_action_inspection_requires_complete_selection(self) -> None:
        base = self.weapon_options()
        self.accepted(base)
        self.accepted([*base, "--animation-time", "0.5", "--animation-play", "--smoke-test", "2"])
        for option in ("--actor-info", "--actor-ram", "--actor-index", "--actor-model-info",
                       "--actor-action", "--actor-weapon-resource", "--actor-weapon-name"):
            with self.subTest(missing=option):
                incomplete = base.copy()
                index = incomplete.index(option)
                del incomplete[index:index + 2]
                self.rejected(incomplete, "Weapon inspection requires")
            with self.subTest(duplicate=option):
                self.rejected([*base, option, "0"], "Duplicate actor inspection option")
        for option in ("--actor-action", "--actor-weapon-resource", "--actor-weapon-name"):
            self.rejected(self.replace(base, option, ""), "Weapon inspection requires")

    def test_weapon_action_preserves_pose_and_time_boundaries(self) -> None:
        base = self.weapon_options()
        self.rejected([*base, "--animation", "clip"], "cannot be combined with a clip or manual joint pose")
        self.rejected([*base, "--bone", "0"], "cannot be combined with a clip or manual joint pose")
        self.rejected([*base, "--rotate-z", "1"], "cannot be combined with a clip or manual joint pose")
        self.rejected(self.replace(base, "--actor-ram", "4"), "--actor-ram from 0 to 3")
        self.accepted(self.replace(base, "--actor-ram", "3"))
        self.accepted([*base, "--animation-time", "600"])
        self.rejected([*base, "--animation-time", "600.1"], "at most 600 seconds")
        self.rejected([*base, "--animation-play", "--smoke-test", "1"], "at least two frames")
        self.rejected([*base, "--scene-root", "0"], "Scene inspection cannot be combined")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tests/app_cli_tests.py path/to/dw4_pc[.exe]")
    AppCliTests.executable = Path(sys.argv.pop()).resolve(strict=True)
    if not AppCliTests.executable.is_file():
        raise SystemExit("The app executable must be a file.")
    unittest.main()
