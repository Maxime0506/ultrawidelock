#!/usr/bin/env python3
"""Unit tests for tools/power_profile.py: the walk-up reduction (held time,
gate-to-bolt, UWB duty), the PPK2 merge, and the --calibrate pass that turns a
walk-up's interleaved range/RSSI trace into a dBm-to-metres curve. Synthetic
captures with hand-computable answers; stdlib only. Run directly or via
tests/host/run.sh."""

import contextlib
import io
import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import power_profile  # noqa: E402


def lines(*events):
    """Synthetic capture: each event is (t_us, 'ev[ k=v]')."""
    return "\n".join("[ALAB] t=%d ev=%s" % (t, ev) for t, ev in events) + "\n"


def write(text, suffix=".log"):
    fd, path = tempfile.mkstemp(suffix=suffix)
    with os.fdopen(fd, "w") as f:
        f.write(text)
    return path


def run_main(argv):
    """main() with stdout captured; returns (rc, output)."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        try:
            rc = power_profile.main(argv)
        except SystemExit as e:
            rc = e.code
    return rc, buf.getvalue()


# One walk-up: connect, hold, open at -64, m4, bolt, then a departure close.
WALKUP = [
    (1_000_000, "session.start"),
    (1_000_000, "ph.connect"),
    (1_200_000, "gate.hold dbm=-70"),
    (2_000_000, "gate.open dbm=-64"),
    (2_500_000, "ph.m4"),
    (4_000_000, "ph.bolt"),
    (9_000_000, "gate.close dbm=-77"),
]


class TestParse(unittest.TestCase):
    def test_ignores_non_alab_noise(self):
        text = "boot: chip revision v0.2\n" + lines((1, "session.start"))
        path = write(text)
        try:
            self.assertEqual(power_profile.parse_log(path), [(1, "session.start", {})])
        finally:
            os.unlink(path)

    def test_attrs_parse_negative_values(self):
        path = write(lines((5, "rssi dbm=-70")))
        try:
            self.assertEqual(power_profile.parse_log(path)[0][2], {"dbm": -70})
        finally:
            os.unlink(path)

    def test_walkup_needs_a_connect(self):
        """A session with no ph.connect is a fragment, not a walk-up."""
        evs = power_profile.parse_log(write(lines((1, "session.start"), (2, "rssi dbm=-50"))))
        self.assertEqual(power_profile.split_walkups(evs), [])


class TestAnalyze(unittest.TestCase):
    def one(self, events):
        evs = [(t, ev.split()[0], dict(power_profile.ATTR_RE.findall(" ".join(ev.split()[1:]))))
               for t, ev in events]
        evs = [(t, n, {k: int(v) for k, v in a.items()}) for t, n, a in evs]
        return power_profile.analyze(evs)

    def test_held_and_latencies(self):
        r = self.one(WALKUP)
        self.assertEqual(r["held_us"], 1_000_000)      # connect -> gate.open
        self.assertEqual(r["gate_bolt_us"], 2_000_000)  # gate.open -> bolt
        self.assertEqual(r["conn_bolt_us"], 3_000_000)  # connect -> bolt
        self.assertEqual(r["uwb_on_us"], 6_500_000)     # m4 -> gate.close
        self.assertEqual(r["open_dbm"], -64)

    def test_gate_already_open_reports_no_hold(self):
        """Connecting at the door never emits gate.hold/gate.open: held is 0."""
        r = self.one([e for e in WALKUP if not e[1].startswith("gate.")])
        self.assertEqual(r["held_us"], 0)
        self.assertIsNone(r["open_dbm"])

    def test_holdcap_counts_as_the_open(self):
        """The hold hit WOZ_RSSI_GATE_MAX_HOLD_MS: gate.holdcap ends the held span."""
        evs = [e for e in WALKUP if not e[1].startswith("gate.open")]
        evs.append((2_000_000, "gate.holdcap dbm=-72"))
        evs.sort()
        r = self.one(evs)
        self.assertEqual(r["held_us"], 1_000_000)
        self.assertEqual(r["open_dbm"], -72)


class TestPpk(unittest.TestCase):
    def test_span_mean_and_manual_shift(self):
        # 0..1 s at 5 mA, 1..2 s at 100 mA, in PPK2 units (ms, uA).
        rows = ["timestamp,current"]
        rows += ["%d,%d" % (t, 5_000) for t in range(0, 1000, 10)]
        rows += ["%d,%d" % (t, 100_000) for t in range(1000, 2000, 10)]
        path = write("\n".join(rows) + "\n", ".csv")
        try:
            s = power_profile.parse_ppk(path)
        finally:
            os.unlink(path)
        self.assertEqual(len(s), 200)
        # --shift: capture t=1.0 s is device m4 (2.5 s), so offset is 1.5 s.
        off = power_profile.align_ppk(s, 2_500_000, 1.0)
        self.assertAlmostEqual(off, 1.5)
        # The high span starts exactly at m4.
        self.assertAlmostEqual(power_profile.span_ma(s, off, 2_500_000, 3_400_000), 100.0)
        self.assertAlmostEqual(power_profile.span_ma(s, off, 1_600_000, 2_400_000), 5.0)

    def test_auto_align_finds_the_current_step(self):
        rows = ["timestamp,current"]
        rows += ["%d,%d" % (t, 5_000) for t in range(0, 1000, 10)]
        rows += ["%d,%d" % (t, 100_000) for t in range(1000, 2000, 10)]
        path = write("\n".join(rows) + "\n", ".csv")
        try:
            s = power_profile.parse_ppk(path)
        finally:
            os.unlink(path)
        off = power_profile.align_ppk(s, 2_500_000, None)
        self.assertAlmostEqual(off, 1.5, delta=0.1)


class TestCalibrate(unittest.TestCase):
    def test_pairs_each_range_with_the_nearest_rssi(self):
        evs = power_profile.parse_log(write(lines(
            (1_000_000, "range cm=300"),
            (1_050_000, "rssi dbm=-70"),   # nearest to the 300 cm range
            (1_400_000, "rssi dbm=-50"),
            (1_450_000, "range cm=50"),    # nearest to -50
        )))
        self.assertEqual(power_profile.pair_range_rssi(evs, 300_000),
                         [(300, -70), (50, -50)])

    def test_pairing_window_drops_orphans(self):
        """An RSSI a full second away is not evidence about this range."""
        evs = power_profile.parse_log(write(lines(
            (1_000_000, "range cm=300"),
            (2_500_000, "rssi dbm=-70"),
        )))
        self.assertEqual(power_profile.pair_range_rssi(evs, 300_000), [])

    def test_curve_and_threshold_scoring(self):
        """Clean separation: near is -50, far is -80, so any threshold between
        them opens for every near sample and no far one."""
        pairs = [(20, -50)] * 10 + [(280, -80)] * 10
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            best = power_profile.calibrate(pairs, 100, [0, 50, 100, 150, 200, 250, 300])
        out = buf.getvalue()
        self.assertIn("0-50 cm", out)
        self.assertIn("250-300 cm", out)
        # The threshold is inclusive, so the near level itself already opens for
        # every near sample and no far one; nothing lower can beat that.
        self.assertEqual(best, -50)
        self.assertIn("opens for 100% of near samples, 0% of far", out)
        self.assertNotIn("WEAK SEPARATION", out)

    def test_overlapping_levels_are_reported_as_weak(self):
        """Near and far at the same level: no threshold can tell them apart, and
        the tool must say so rather than hand back a confident number."""
        pairs = [(20, -65)] * 10 + [(280, -65)] * 10
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            power_profile.calibrate(pairs, 100, [0, 50, 100, 150, 200, 250, 300])
        self.assertIn("WEAK SEPARATION", buf.getvalue())

    def test_one_sided_capture_has_no_separation_table(self):
        pairs = [(280, -80)] * 5
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            best = power_profile.calibrate(pairs, 100, [0, 50, 100, 150, 200, 250, 300])
        self.assertIsNone(best)
        self.assertIn("need samples on both sides", buf.getvalue())

    def test_percentiles(self):
        self.assertEqual(power_profile.pct([1, 2, 3, 4, 5], 50), 3)
        self.assertEqual(power_profile.pct([1, 2, 3, 4, 5], 10), 1)
        self.assertEqual(power_profile.pct([1, 2, 3, 4, 5], 90), 5)
        self.assertIsNone(power_profile.pct([], 50))


class TestMain(unittest.TestCase):
    def test_reduction_table(self):
        path = write(lines(*WALKUP))
        try:
            rc, out = run_main(["power_profile.py", path, "--tag", "normal"])
        finally:
            os.unlink(path)
        self.assertEqual(rc, 0)
        self.assertIn("normal", out)
        self.assertIn("1000", out)  # held ms
        self.assertIn("-64", out)   # open dBm

    def test_csv_appends_with_one_header(self):
        path = write(lines(*WALKUP))
        out_csv = write("", ".csv")
        os.unlink(out_csv)
        try:
            run_main(["power_profile.py", path, "--tag", "a", "--csv", out_csv])
            run_main(["power_profile.py", path, "--tag", "b", "--csv", out_csv])
            with open(out_csv) as f:
                body = f.read()
        finally:
            os.unlink(path)
            if os.path.exists(out_csv):
                os.unlink(out_csv)
        self.assertEqual(body.count("open_dBm"), 1)
        self.assertEqual(len(body.strip().split("\n")), 3)

    def test_calibrate_mode(self):
        path = write(lines(
            (1_000_000, "range cm=280"), (1_050_000, "rssi dbm=-80"),
            (2_000_000, "range cm=20"), (2_050_000, "rssi dbm=-50"),
        ))
        try:
            rc, out = run_main(["power_profile.py", path, "--calibrate"])
        finally:
            os.unlink(path)
        self.assertEqual(rc, 0)
        self.assertIn("calibration: 2 paired samples", out)

    def test_no_walkups_is_an_input_error(self):
        path = write("nothing to see here\n")
        try:
            rc, _ = run_main(["power_profile.py", path])
        finally:
            os.unlink(path)
        self.assertIn("no walk-ups", str(rc))

    def test_calibrate_without_pairs_is_an_input_error(self):
        path = write(lines((1, "session.start")))
        try:
            rc, _ = run_main(["power_profile.py", path, "--calibrate"])
        finally:
            os.unlink(path)
        self.assertIn("no range/rssi pairs", str(rc))

    def test_unknown_option_rejected(self):
        rc, _ = run_main(["power_profile.py", "x.log", "--banana"])
        self.assertIn("unknown option", str(rc))


if __name__ == "__main__":
    unittest.main()
