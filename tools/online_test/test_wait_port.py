"""Unit test of run.py's wait_port (no server, no game): a listener is found, a process that exited ends the wait at once
(mysqld's start window is 180 s since int-021's fix stage; a mysqld that dies must not cost that).

    python tools/online_test/test_wait_port.py        (or python -m unittest discover -s tools/online_test -p "test_*.py")
"""
import os
import socket
import subprocess
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run  # noqa: E402


class WaitPortTests(unittest.TestCase):
    def test_a_listener_is_found(self):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            s.listen()
            self.assertTrue(run.wait_port(s.getsockname()[1], 5))

    def test_an_exited_process_ends_the_wait_at_once(self):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]  # (bound, not listening: nobody accepts)
            p = subprocess.Popen([sys.executable, "-I", "-c", "pass"])
            p.wait()
            t0 = time.time()
            self.assertFalse(run.wait_port(port, 60, p))
            self.assertLess(time.time() - t0, 5)


if __name__ == "__main__":
    unittest.main()
