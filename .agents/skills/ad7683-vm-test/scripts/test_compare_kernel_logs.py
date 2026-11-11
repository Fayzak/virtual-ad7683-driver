#!/usr/bin/env python3
"""Offline CLI regression tests; no VM, sudo, or third-party packages needed."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("compare_kernel_logs.py")
BASE = "<6>[    1.000000] Linux test kernel\n"


class CompareLogsTests(unittest.TestCase):
    def run_case(self, before, after, expected_status, expected_text):
        with tempfile.TemporaryDirectory(prefix="ad7683-log-test-") as folder:
            root = Path(folder)
            (root / "before").write_text(before, encoding="utf-8")
            (root / "after").write_text(after, encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(root / "before"), str(root / "after")],
                capture_output=True, text=True, check=False,
            )
        self.assertEqual(result.returncode, expected_status, result.stdout + result.stderr)
        self.assertIn(expected_text, result.stdout)
        self.assertNotIn("Traceback", result.stderr)
        return result.stdout

    def test_unchanged(self):
        self.run_case(BASE, BASE, 0, "PASS:")

    def test_normal_new_message_is_shown(self):
        line = "<6>[    2.000000] virtual_ad7683 unloaded\n"
        self.run_case(BASE, BASE + line, 0, line.strip())

    def test_old_warning_is_not_new(self):
        old = BASE + "<4>[    2.000000] WARNING: old unrelated warning\n"
        self.run_case(old, old + "<6>[    3.000000] normal message\n", 0, "PASS:")

    def test_new_diagnostics(self):
        for diagnostic in ("WARNING: CPU: 0", "Oops: 0000", "BUG: bad page",
                           "KASAN: use-after-free", "KCSAN: data-race",
                           "UBSAN: shift-out-of-bounds", "Kernel panic - not syncing",
                           "general protection fault", "Call Trace:"):
            with self.subTest(diagnostic=diagnostic):
                self.run_case(BASE, BASE + f"<4>[    2.000000] {diagnostic}\n",
                              1, diagnostic)

    def test_continuation_line(self):
        self.run_case(BASE, BASE + "<4>[    2.000000] diagnostic\nWARNING: detail\n",
                      1, "FAIL:")

    def test_module_taint_is_visible_but_not_oops(self):
        message = "<4>[    2.000000] module verification failed: tainting kernel\n"
        self.run_case(BASE, BASE + message, 0, message.strip())

    def test_ring_loss(self):
        before = BASE + "<6>[    2.000000] retained\n"
        after = "<6>[    2.000000] retained\n<6>[    3.000000] new\n"
        self.run_case(before, after, 2, "coverage unknown")

    def test_reboot(self):
        self.run_case(BASE, "<6>[    0.000000] Linux other kernel\n", 2, "NOT RUN:")

    def test_truncated_after(self):
        self.run_case(BASE + "<6>[    2.000000] tail\n", BASE, 2, "NOT RUN:")

    def test_empty_snapshots(self):
        for before, after in (("", ""), (BASE, ""), ("", BASE)):
            with self.subTest(before=before, after=after):
                self.run_case(before, after, 2, "empty snapshot")

    def test_wrong_capture_format(self):
        self.run_case("Linux kernel\n", "Linux kernel\nnormal\n", 2, "expected dmesg")

    def test_missing_file(self):
        with tempfile.TemporaryDirectory(prefix="ad7683-log-test-") as folder:
            result = subprocess.run(
                [sys.executable, str(SCRIPT), f"{folder}/missing", f"{folder}/also-missing"],
                capture_output=True, text=True, check=False,
            )
        self.assertEqual(result.returncode, 2)
        self.assertIn("cannot read snapshots", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
