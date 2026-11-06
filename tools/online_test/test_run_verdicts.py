"""Unit tests of run.py's verdicts (no server, no game): the counter-proof (W1x, W30x) passes only when its expected check
failed and nothing else broke, and a skipped scenario is no pass.

    python tools/online_test/test_run_verdicts.py        (or python -m unittest discover -s tools/online_test -p "test_*.py")

The problem lines are the ones run.py printed in the trial merge's runs (build/i1aweb/scen_c1.log, scen_c2.log, scen_c3.log).
"""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def scenario(name):
    with open(os.path.join(HERE, "scenarios", name, "scenario.json"), encoding="utf-8") as f:
        return json.load(f)


W1X_FAIL = "b: online-script: FAIL (line 16) key:4=1 (frame 13001, stage stagml11, slot 0, present 1, mode solo, tick 551)"
W1X_MUST_NOT = "b logged what it must not (online-script: FAIL): online-script: FAIL (line 16) key:4=1 (frame 13001, stage stagml11, " \
               "slot 0, present 1, mode solo, tick 551)"
W30X_FAIL = "b: online-script: FAIL (line 12) key:9=0 (frame 13536, stage stag0201, slot 0, present 1, mode solo, tick 1485)"
W30X_MUST_NOT = "b logged what it must not (online-script: FAIL): online-script: FAIL (line 12) key:9=0 (frame 13536, stage stag0201, " \
                "slot 0, present 1, mode solo, tick 1485)"


class CounterProofTests(unittest.TestCase):
    def test_the_expected_story_check_passes_it(self):
        for name, problems in (("W1x", [W1X_FAIL, W1X_MUST_NOT]), ("W30x", [W30X_FAIL, W30X_MUST_NOT])):
            p, notes = run.counter_proof_verdict(scenario(name), problems, ["n"])
            self.assertEqual([], p, name)
            self.assertIn("counter-proof failed as it must: " + problems[0], notes)

    def test_consequences_of_the_failed_check_are_allowed(self):
        p, _ = run.counter_proof_verdict(scenario("W1x"), [W1X_FAIL, W1X_MUST_NOT, "b: the script did not finish",
                                                           "b: expected /online-script: step .* log: own story again at home/ (in order) not found"], [])
        self.assertEqual([], p)

    def test_no_problem_fails_it(self):
        p, _ = run.counter_proof_verdict(scenario("W1x"), [], [])
        self.assertEqual(["the counter-proof passed: the checks do not catch the switched-off behaviour"], p)

    def test_a_crash_or_a_timeout_alone_fails_it(self):
        # (Run c2's W30x: both clients crashed; before the review this counted as the counter-proof's pass.)
        crashed = ["a exited with 4294967295", "b exited with 4294967295", "a: the script did not finish", "b: the script did not finish",
                   "a: expected /online-script: step .* log: a in dng205b1/ (in order) not found"]
        p, _ = run.counter_proof_verdict(scenario("W30x"), crashed, [])
        self.assertEqual(len(crashed), len(p))
        self.assertTrue(all(x.startswith("the counter-proof failed, but not in its expected check") for x in p))
        p, _ = run.counter_proof_verdict(scenario("W1x"), ["b did not finish within 1000 s"], [])
        self.assertEqual(1, len(p))

    def test_infrastructure_beside_the_expected_check_fails_it(self):
        for extra in ("a exited with 4294967295", "b exited with 3221225477", "a did not finish within 1000 s",
                      "b: online-trace: close reason=8", "a: error: the server refused the ticket", "b: inputs stopped arriving",
                      "fake_guests: a never logged /x/", "db: one instance failed (ERROR 1146)",
                      "a: expected /online-script: step .* log: a hosts in stag0101/ (in order) not found",
                      "relay logged 2 x desync"):
            p, notes = run.counter_proof_verdict(scenario("W1x"), [W1X_FAIL, W1X_MUST_NOT, extra], [])
            self.assertEqual(["the counter-proof's run broke besides its expected check: " + extra], p, extra)
            self.assertIn("(the expected check failed: %s)" % W1X_FAIL, notes)

    def test_an_exception_or_a_server_start_fails_it(self):
        p, _ = run.counter_proof_verdict(scenario("W1x"), ["RuntimeError: the server did not start on 57210"], [], broke=True)
        self.assertEqual(["the counter-proof's run broke, no verdict: RuntimeError: the server did not start on 57210"], p)

    def test_the_wrong_check_fails_it(self):
        # b's story check at the very start (line 5, before any join) is not what the switched-off replace breaks.
        early = "b: online-script: FAIL (line 5) key:4=1 (frame 900, stage stagml11, slot 0, present 1, mode solo, tick 20)"
        p, _ = run.counter_proof_verdict(scenario("W1x"), [early], [])
        self.assertEqual(1, len(p))
        self.assertIn("not in its expected check", p[0])

    def test_a_counter_proof_needs_expect_fail(self):
        sc = dict(scenario("W1x"))
        del sc["expect_fail"]
        p, _ = run.counter_proof_verdict(sc, [W1X_FAIL], [])
        self.assertTrue(p[0].startswith("scenario.json: a counter-proof needs expect_fail"))

    def test_every_counter_proof_names_its_check(self):
        for name in os.listdir(os.path.join(HERE, "scenarios")):
            path = os.path.join(HERE, "scenarios", name, "scenario.json")
            if os.path.isfile(path) and scenario(name).get("counter_proof"):
                self.assertTrue(scenario(name).get("expect_fail"), name)


class SummaryTests(unittest.TestCase):
    def test_skips_are_counted_apart_and_fail_the_run(self):
        line, code = run.run_summary(30, 0, 4)
        self.assertTrue(line.startswith("26 of 30 scenarios passed, 4 skipped, 0 failed"), line)
        self.assertEqual(3, code)

    def test_allow_skip_accepts_them(self):
        self.assertEqual(("26 of 30 scenarios passed, 4 skipped, 0 failed", 0), run.run_summary(30, 0, 4, allow_skip=True))

    def test_a_failure_wins(self):
        self.assertEqual(1, run.run_summary(30, 2, 4)[1])
        self.assertEqual(1, run.run_summary(30, 2, 4, allow_skip=True)[1])
        self.assertEqual(("30 of 30 scenarios passed, 0 skipped, 0 failed", 0), run.run_summary(30, 0, 0))


if __name__ == "__main__":
    unittest.main()
