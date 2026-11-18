"""Unit test of run.py's "story_saved" watch (no server, no game): the precondition "the owner's row had no flag before the
client set it" holds when the server saves the flag sooner than the watcher polls the client's log (int-021's check pass:
S14p failed 3 of 4 runs on it, the save came 0.1-0.3 s after the client's "505 set" line).

    python tools/online_test/test_story_saved.py        (or python -m unittest discover -s tools/online_test -p "test_*.py")
"""
import json
import os
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def s14p():
    with open(os.path.join(HERE, "scenarios", "S14p", "scenario.json"), encoding="utf-8") as f:
        return json.load(f)


class StorySavedTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="dw4_story_saved_")
        self.out = self.temporary.name
        self.row = (1, 0)  # the owner's online_story: (revision, flag 505)
        self.saved_stored_story_flag = run.stored_story_flag
        run.stored_story_flag = lambda stack, connection, owner_id, flag: self.row

    def tearDown(self):
        run.stored_story_flag = self.saved_stored_story_flag
        self.temporary.cleanup()

    def watch(self, sc, steps):
        """Runs the watch on the client's log while `steps` (log line, the row afterwards, seconds to wait) are played."""
        result, stop = {}, threading.Event()
        log = os.path.join(self.out, sc["story_saved"]["client"] + ".log")
        open(log, "w", encoding="utf-8").close()
        watcher = threading.Thread(target=run.watch_story_saved, args=(None, sc, self.out, "", 1, result, stop))
        watcher.start()
        for line, row, wait in steps:
            # (The quickest path: the server saved the flag before the client's line reached its log.)
            if row is not None:
                self.row = row
            with open(log, "a", encoding="utf-8") as f:
                f.write(line + "\n")
            time.sleep(wait)
        watcher.join(timeout=5)
        stop.set()
        watcher.join()
        return run.story_saved_verdict(sc["story_saved"], result), result

    def test_a_save_quicker_than_the_log_poll_keeps_the_precondition(self):
        (problems, notes), result = self.watch(s14p(), [
            ("online-script: step 8 (line 13) log: 505 not in the host's story (frame 6183, stage stag0101, tick 2427)", None, 0.5),
            ("online-script: step 10 (line 15) log: 505 set (frame 7070, stage stag0101, tick 3314)", (2, 1), 0.3),
        ])
        self.assertEqual((1, 0), result["before"])
        self.assertEqual([], problems)
        self.assertTrue(any(n.startswith("story_saved: flag 505 in the owner's online_story (revision 2)") for n in notes), notes)

    def test_a_flag_there_before_the_step_still_fails_it(self):
        self.row = (2, 1)
        (problems, _), _ = self.watch(s14p(), [
            ("online-script: step 8 (line 13) log: 505 not in the host's story", None, 0.5),
            ("online-script: step 10 (line 15) log: 505 set", None, 0.3),
        ])
        self.assertEqual(["story_saved: the owner's online_story had flag 505 before g set it ((2, 1))"], problems)

    def test_a_missing_line_before_the_step_fails_it(self):
        (problems, _), _ = self.watch(s14p(), [("online-script: step 10 (line 15) log: 505 set", (2, 1), 0.3)])
        self.assertEqual(["story_saved: g never logged 'log: 505 not in the host's story' before 'log: 505 set'"], problems)


if __name__ == "__main__":
    unittest.main()
