"""PTY integration checks; Python is only used for tests, never by om itself."""

import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / ".cache/om")
if len(sys.argv) > 2 and sys.argv[1] == "--binary":
    BINARY = str(Path(sys.argv[2]).resolve())
    del sys.argv[1:3]


class Terminal:
    def __init__(self, *args, width=100, height=32, launcher=False, background=None):
        self.master, self.slave = pty.openpty()
        self.set_size(width, height)
        self.before = termios.tcgetattr(self.slave)
        command = [str(ROOT / "om") if launcher else BINARY, *args]
        self.process = subprocess.Popen(
            command, stdin=self.slave, stdout=self.slave, stderr=self.slave,
            env={**os.environ, "TERM": "xterm-256color", "COLORTERM": "truecolor"},
        )
        self.output = b""
        self.background = background
        self.background_sent = False

    def set_size(self, width, height):
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", height, width, 0, 0))

    def collect(self, seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            if select.select([self.master], [], [], max(0, until-time.monotonic()))[0]:
                try:
                    block = os.read(self.master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not block:
                    break
                self.output += block
                if (self.background is not None and not self.background_sent
                        and b"\x1b]11;?\x1b\\" in self.output):
                    self.send(self.background)
                    self.background_sent = True

    def send(self, keys):
        os.write(self.master, keys)

    def finish(self, sig=None):
        if self.process.poll() is None:
            if sig is not None:
                self.process.send_signal(sig)
            else:
                self.send(b"q")
        deadline = time.monotonic() + 10
        while self.process.poll() is None and time.monotonic() < deadline:
            self.collect(.05)
        self.collect(.05)
        self.process.wait(timeout=1)
        return self.process.returncode, termios.tcgetattr(self.slave)

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        os.close(self.master)
        os.close(self.slave)


class VisualizerTests(unittest.TestCase):
    def terminal(self, *args, **kwargs):
        terminal = Terminal(*args, **kwargs)
        self.addCleanup(terminal.close)
        return terminal

    def assert_clean(self, terminal, sig=None):
        code, after = terminal.finish(sig)
        self.assertEqual(code, 0, terminal.output[-2000:])
        self.assertEqual(after, terminal.before, "terminal flags were not restored")
        self.assertIn(b"\x1b[?25h", terminal.output)
        self.assertIn(b"\x1b[?1049l", terminal.output)

    def test_launcher_enters_nix_and_renders_mantra(self):
        t = self.terminal("--seed", "42", launcher=True)
        deadline = time.monotonic()+45
        while b"\x1b[?1049h" not in t.output and time.monotonic() < deadline:
            t.collect(.1)
        t.collect(.5)
        self.assertIn(b"\x1b[?1049h", t.output)
        for word in ("ॐ", "मणि", "पद्मे", "हूँ"):
            self.assertIn(word.encode(), t.output)
        self.assertIn(b"38;2;", t.output)
        # A PTY read can stop halfway through an escape sequence. Inspect
        # startup through the last complete frame, not a partial write.
        startup = t.output.split(b"\x1b[?1049h",1)[1]
        self.assertIn(b"\x1b[?2026l", startup)
        startup = startup[:startup.rfind(b"\x1b[?2026l")+len(b"\x1b[?2026l")]
        visible = re.sub(rb"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b\[[0-?]*[ -/]*[@-~]",
                         b"", startup)
        self.assertTrue(all(ord(char)>127 or char.isspace() for char in visible.decode()),
                        f"startup showed UI text: {bytes(c for c in visible if c < 128)!r}")
        self.assert_clean(t)

    def test_all_motions_and_color_depths(self):
        for mode in ("rain", "river", "aurora", "lotus", "bloom", "incense", "lanterns", "mandala", "whirlpool"):
            with self.subTest(mode=mode):
                t = self.terminal("--mode", mode, "--color", "256", "--ascii", "--no-hint")
                t.collect(.25)
                self.assertIn(b"38;5;", t.output)
                self.assertIn(b"mani", t.output)
                self.assert_clean(t)

    def test_motion_moves_words_between_columns_in_complete_frames(self):
        t = self.terminal("--seed", "42", "--no-hint")
        t.collect(.5)
        frames = re.findall(rb"\x1b\[\?2026h(.*?)\x1b\[\?2026l", t.output, re.S)
        self.assertGreater(len(frames), 8)
        placement = re.compile(rb"\x1b\[(\d+);(\d+)H\x1b\[22;38;2;\d+;\d+;\d+m([^\x1b]+)")
        positions = [{(int(y), int(x), word) for y,x,word in placement.findall(frame)}
                     for frame in frames]
        columns = {x % 6 for frame in positions for _,x,_ in frame}
        self.assertGreaterEqual(len(columns), 4, "words still occupy a fixed six-column grid")
        self.assertGreater(len(positions[0] ^ positions[-1]), 10,
                           "word positions did not move through the screen")
        self.assert_clean(t)

    def test_resize_including_tiny_terminals(self):
        t = self.terminal("--mode", "lotus", "--no-hint")
        for width, height in ((100,32), (1,1), (6,2), (23,7), (240,80), (80,24)):
            t.set_size(width, height)
            t.process.send_signal(signal.SIGWINCH)
            t.collect(.1)
            self.assertIsNone(t.process.poll())
        self.assert_clean(t)

    def test_controls_pause_and_resume(self):
        t = self.terminal("--seed", "4")
        t.collect(.2)
        t.send(b"pm+[]a ")
        t.collect(.2)
        paused_at = len(t.output)
        t.collect(.15)
        self.assertNotIn(b"38;2;", t.output[paused_at:], "paused animation kept changing")
        self.assertNotIn(b"paused", t.output)
        self.assertNotIn(b"density", t.output)
        t.send(b" ")
        resumed_at = len(t.output)
        t.collect(.15)
        self.assertIn(b"38;2;", t.output[resumed_at:], "animation failed to resume")
        self.assert_clean(t)

    def test_unattended_palette_and_motion_cycles(self):
        t = self.terminal("--interval", "5", "--scene-interval", "10", "--fps", "10")
        t.collect(10.5)
        visible = re.sub(rb"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b\[[0-?]*[ -/]*[@-~]", b"", t.output)
        self.assertTrue(all(ord(char)>127 or char.isspace() for char in visible.decode()),
                        "automatic cycling showed UI text")
        self.assertEqual(t.output.count(b"\x1b[2J"), 2, "cycling cleared the screen")
        self.assertIn(b"38;2;", t.output)
        self.assert_clean(t)

    def test_terminal_background_is_queried_and_early_keys_are_preserved(self):
        background = b"\x1b]11;rgb:3030/1818/3c3c\x1b\\"
        t = self.terminal("--no-hint", "--palette", "nebula", background=background)
        t.collect(.3)
        self.assertTrue(t.background_sent)
        colors = re.findall(rb"38;2;(\d+);(\d+);(\d+)m", t.output)
        self.assertTrue(colors)
        # Every channel stays between the theme background and palette ink.
        self.assertTrue(all(int(r)>=48 and int(g)>=24 and int(b)>=60 for r,g,b in colors))
        self.assert_clean(t)
        t = self.terminal()
        t.send(b"q")
        t.collect(.3)
        self.assertIsNotNone(t.process.poll(), "startup swallowed the quit key")
        self.assert_clean(t)

    def test_signals_restore_terminal(self):
        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            with self.subTest(signal=sig):
                t = self.terminal("--no-hint")
                t.collect(.1)
                self.assert_clean(t, sig)

    def test_suspend_restores_terminal_and_resume_redraws(self):
        t = self.terminal()
        t.collect(.1)
        t.process.send_signal(signal.SIGTSTP)
        t.collect(.2)
        self.assertEqual(termios.tcgetattr(t.slave), t.before)
        t.process.send_signal(signal.SIGCONT)
        t.collect(.2)
        self.assertGreaterEqual(t.output.count(b"\x1b[?1049h"), 2)
        self.assert_clean(t)

    def test_duration_exits_cleanly(self):
        t = self.terminal("--duration", ".15")
        t.collect(.3)
        self.assertIsNotNone(t.process.poll())
        self.assert_clean(t)

    def test_invalid_arguments_and_noninteractive_output(self):
        for args in (("--fps", "nan"), ("--fps", "20.5"), ("--density", "2"),
                     ("--mantra-interval", "20"),
                     ("--mode", "missing"), ("--seed", "-1"), ("--unknown",)):
            with self.subTest(args=args):
                result = subprocess.run([BINARY, *args], capture_output=True)
                self.assertEqual(result.returncode, 2)
                self.assertNotIn(b"\x1b", result.stdout)
        result = subprocess.run([BINARY], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"interactive terminal", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
