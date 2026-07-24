#!/usr/bin/env python3
"""Unit tests for tools/aliro_gait.py on synthetic walk-up captures.

The synthetic traces model the E1 physics honestly: a linear approach with a
sinusoidal gait ripple (amplitude a few cm), Gaussian ranging noise at the
DW3000 scale, and whole-cm quantization at the observed 192 ms block rate.
Fixed seeds keep every case deterministic. The incremental (firmware-shaped)
estimator is held to the FFT answer so the future on-target learner has a
pinned reference."""

import contextlib
import io
import math
import os
import random
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import aliro_gait  # noqa: E402
import aliro_lab  # noqa: E402

SAMPLE = os.path.join(ROOT, "tests", "host", "data", "aliro_lab_sample.log")


def synth_log(cadence_hz=None, n=26, block_ms=192, d0_cm=700.0, v_cm_s=130.0,
              amp_cm=3.0, noise_cm=2.0, wander_cm=2.5, seed=0, t0_us=1000000):
    """One synthetic walk-up transaction: linear approach + sinusoidal gait
    ripple + slow multipath/body-shadow wander (0.25-0.35 Hz, the part of
    "ranging noise" that is NOT white) + white per-block jitter + whole-cm
    quantization. cadence_hz=None -> stationary phone."""
    rng = random.Random(seed)
    phase = rng.uniform(0.0, 2.0 * math.pi)
    wander_hz = rng.uniform(0.25, 0.35)
    wander_ph = rng.uniform(0.0, 2.0 * math.pi)
    lines = ["[ALAB] t=%d ev=session.start" % t0_us]
    for i in range(n):
        ti = i * block_ms / 1000.0
        r = d0_cm - v_cm_s * ti
        if cadence_hz is not None:
            r += amp_cm * math.sin(2.0 * math.pi * cadence_hz * ti + phase)
        r += wander_cm * math.sin(2.0 * math.pi * wander_hz * ti + wander_ph)
        r += rng.gauss(0.0, noise_cm)
        lines.append("[ALAB] t=%d ev=range cm=%d"
                     % (t0_us + int(ti * 1e6), max(1, int(round(r)))))
    lines.append("[ALAB] t=%d ev=session.end" % (t0_us + n * block_ms * 1000))
    return "\n".join(lines) + "\n"


def synth_session(cadence_hz=1.9, k=3, n=18, retreat=6, block_ms=192,
                  d0_cm=520.0, v_cm_s=130.0, amp_cm=3.0, noise_cm=2.0,
                  wander_cm=2.5, seed=0, t0_us=1000000, gap_us=10000000):
    """One held BLE session with k approaches: each descends (n blocks) then
    retreats (retreat blocks) and fires relock.sent, modeling a carrier who
    walks up repeatedly without the phone ever disconnecting. This is the
    natural bench capture, not one session per walk-up."""
    rng = random.Random(seed)
    lines = ["[ALAB] t=%d ev=session.start" % t0_us]
    t = t0_us
    for _ in range(k):
        phase = rng.uniform(0.0, 2.0 * math.pi)
        wander_hz = rng.uniform(0.25, 0.35)
        wander_ph = rng.uniform(0.0, 2.0 * math.pi)
        for i in range(n + retreat):
            ti = i * block_ms / 1000.0
            di = min(i, 2 * n - i)  # descend to block n, then rise (walk away)
            r = d0_cm - v_cm_s * (di * block_ms / 1000.0)
            r += amp_cm * math.sin(2.0 * math.pi * cadence_hz * ti + phase)
            r += wander_cm * math.sin(2.0 * math.pi * wander_hz * ti + wander_ph)
            r += rng.gauss(0.0, noise_cm)
            lines.append("[ALAB] t=%d ev=range cm=%d"
                         % (t + int(ti * 1e6), max(1, int(round(r)))))
        end_us = t + int((n + retreat) * block_ms / 1000.0 * 1e6)
        lines.append("[ALAB] t=%d ev=relock.sent" % end_us)
        t = end_us + gap_us
    lines.append("[ALAB] t=%d ev=session.end" % t)
    return "\n".join(lines) + "\n"


def analyze(text, label="x"):
    return aliro_gait.walkups_from_text(label, text)


class BlockDerivation(unittest.TestCase):
    def test_ran_from_timestamps(self):
        # Block duration and the phone's RAN multiplier come from the range
        # timestamps alone: no extra firmware logging needed on bench day.
        w = analyze(synth_log(cadence_hz=1.9, block_ms=192))[0]
        self.assertEqual(w.block_ms, 192)
        self.assertEqual(w.ran, 2)
        w = analyze(synth_log(cadence_hz=1.9, block_ms=96, n=52))[0]
        self.assertEqual(w.block_ms, 96)
        self.assertEqual(w.ran, 1)

    def test_too_few_samples_skips(self):
        w = analyze(synth_log(cadence_hz=1.9, n=6))[0]
        self.assertIsNone(w.features)
        self.assertIn("too few", w.skip)


class CadenceRecovery(unittest.TestCase):
    def test_median_within_tenth_hz(self):
        # S0 bar: MEDIAN recovered cadence within 0.1 Hz of truth across the
        # walking band, 8 seeded walk-ups per cadence. The median is the
        # honest statistic: at ~26 samples and SNR ~1, single windows have an
        # irreducible outlier rate (measured ~10%, always at low prominence),
        # and E1/Tier-2 aggregate across arrivals exactly the same way.
        for cadence in (1.5, 1.7, 1.9, 2.1, 2.3):
            errs = []
            for seed in range(8):
                w = analyze(synth_log(cadence_hz=cadence, seed=seed))[0]
                self.assertIsNotNone(w.features, "cad %.1f seed %d skipped: %s"
                                     % (cadence, seed, w.skip))
                errs.append(abs(w.features["cadence_hz"] - cadence))
            med = sorted(errs)[len(errs) // 2]
            self.assertLessEqual(med, 0.1, "cad %.1f: median err %.3f (%s)"
                                 % (cadence, med,
                                    ", ".join("%.2f" % e for e in errs)))

    def test_incremental_agrees_with_fft(self):
        # The firmware-shaped Goertzel estimator must track the offline FFT
        # in the median (outlier windows may disagree; both are then wrong in
        # their own way and Tier 2 down-weights them via prominence). This is
        # the KAT that lets the on-target learner mirror this pipeline.
        diffs = []
        for seed in range(10):
            f = analyze(synth_log(cadence_hz=1.9, seed=seed))[0].features
            self.assertIsNotNone(f["inc_cadence_hz"])
            diffs.append(abs(f["inc_cadence_hz"] - f["cadence_hz"]))
        med = sorted(diffs)[len(diffs) // 2]
        self.assertLessEqual(med, 0.15, "median fft/inc gap %.3f" % med)


class MotionVerdict(unittest.TestCase):
    def test_carried_vs_stationary_zero_overlap(self):
        # Tier-1 floor: a carried phone reads CARRY, a hall-table phone reads
        # still, with zero overlap over 20 seeded trials each. Stationary
        # noise is the ~1 cm static-channel jitter plus a little multipath
        # wander; the larger spread quoted for DW3000 ranging rides with body
        # shadow during motion, i.e. WITH the gait (measured margin here:
        # walking residual RMS >= 2.7 cm vs stationary <= 1.3 cm).
        for seed in range(20):
            carried = analyze(synth_log(cadence_hz=1.9, seed=seed))[0].features
            self.assertTrue(carried["motion"], "carried seed %d read still" % seed)
            self.assertTrue(carried["approach"],
                            "carried seed %d not approaching" % seed)
            still = analyze(synth_log(cadence_hz=None, v_cm_s=0.0, d0_cm=150.0,
                                      noise_cm=1.0, wander_cm=0.5,
                                      seed=seed))[0].features
            self.assertFalse(still["motion"], "stationary seed %d read CARRY" % seed)
            self.assertFalse(still["approach"],
                             "stationary seed %d read approaching" % seed)


class MultiApproach(unittest.TestCase):
    def test_held_session_splits_into_approaches(self):
        # The whole point: one BLE session, carrier walks up 3 times with a
        # relock between each. The probe must yield 3 analyzed walk-ups, not 1,
        # each reading as motion, with the median cadence on target.
        analyzed = [w for w in analyze(synth_session(cadence_hz=1.9, k=3,
                                                      seed=1)) if w.features]
        self.assertEqual(len(analyzed), 3)
        for w in analyzed:
            self.assertTrue(w.features["motion"], "approach read still")
            self.assertLessEqual(w.n, 22, "retreat not trimmed (n=%d)" % w.n)
        cads = sorted(w.features["cadence_hz"] for w in analyzed)
        self.assertAlmostEqual(cads[len(cads) // 2], 1.9, delta=0.25)

    def test_no_relock_stays_single_window(self):
        # A capture with no relock (synthetic / genuine single approach) is one
        # walk-up: the split must not manufacture extra windows.
        self.assertEqual(
            len([w for w in analyze(synth_log(cadence_hz=1.9, seed=0))
                 if w.features]), 1)


class Classification(unittest.TestCase):
    def _two_carriers(self):
        a = "".join(synth_log(cadence_hz=1.7, amp_cm=2.5, v_cm_s=110.0,
                              d0_cm=650.0, seed=s, t0_us=1000000 + s * 60000000)
                    for s in range(8))
        b = "".join(synth_log(cadence_hz=2.1, amp_cm=3.5, v_cm_s=140.0,
                              d0_cm=750.0, seed=100 + s,
                              t0_us=1000000 + s * 60000000)
                    for s in range(8))
        return analyze(a, "alice") + analyze(b, "bob")

    def test_loo_separates_two_carriers(self):
        cls = aliro_gait.classify(self._two_carriers())
        self.assertIsNotNone(cls)
        self.assertEqual(cls["n"], 16)
        self.assertGreaterEqual(cls["accuracy"], 0.9)

    def test_single_label_is_unclassifiable(self):
        walkups = analyze(synth_log(cadence_hz=1.9, seed=0), "solo")
        self.assertIsNone(aliro_gait.classify(walkups))

    def test_control_label_does_not_block(self):
        # The stationary hall-table control is one walk-up under its own
        # label; it must be dropped from LOO, not block the carriers'.
        control = analyze(synth_log(cadence_hz=None, v_cm_s=0.0, d0_cm=150.0,
                                    noise_cm=1.0, wander_cm=0.5, seed=7),
                          "table")
        cls = aliro_gait.classify(self._two_carriers() + control)
        self.assertIsNotNone(cls)
        self.assertEqual(cls["n"], 16)
        self.assertNotIn("table", cls["labels"])


class SampleLogSmoke(unittest.TestCase):
    def test_checked_in_sample_parses(self):
        # The Aliro Lab sample capture must at least flow through the gait
        # pipeline without error (its 310 ms cadence grid clips the band).
        with open(SAMPLE) as f:
            walkups = analyze(f.read(), "sample")
        self.assertGreaterEqual(len(walkups), 1)
        for w in walkups:
            self.assertTrue(w.features is not None or w.skip)


class Cli(unittest.TestCase):
    def test_main_reports_and_writes_html(self):
        with tempfile.TemporaryDirectory() as tmp:
            a_path = os.path.join(tmp, "alice.log")
            b_path = os.path.join(tmp, "bob.log")
            html = os.path.join(tmp, "report.html")
            with open(a_path, "w") as f:
                f.write("".join(
                    synth_log(cadence_hz=1.7, amp_cm=2.5, v_cm_s=110.0,
                              d0_cm=650.0, seed=s, t0_us=1000000 + s * 60000000)
                    for s in range(4)))
            with open(b_path, "w") as f:
                f.write("".join(
                    synth_log(cadence_hz=2.1, amp_cm=3.5, v_cm_s=140.0,
                              d0_cm=750.0, seed=100 + s,
                              t0_us=1000000 + s * 60000000)
                    for s in range(4)))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = aliro_gait.main(["aliro_gait.py", "alice=" + a_path,
                                      "bob=" + b_path, "-o", html])
            self.assertEqual(rc, 0)
            text = out.getvalue()
            self.assertIn("leave-one-out nearest-centroid", text)
            with open(html) as f:
                page = f.read()
            self.assertIn("scatter", page)
            self.assertIn("alice", page)


if __name__ == "__main__":
    unittest.main()
