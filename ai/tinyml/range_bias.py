#!/usr/bin/env python3
"""Does an OBSTRUCTED phone read FARTHER than an unobstructed one, phone fixed?

Run from the repository root, after the two captures below:
    ai/tinyml/.venv/bin/python ai/tinyml/range_bias.py <door.csv> <true_cm>
    ai/tinyml/.venv/bin/python ai/tinyml/range_bias.py <hand.csv> <pocket.csv> <true_cm>

    <door.csv>    one parse_alab.py --distance output holding both runs; label 0
                  is the hand run and label 1 the pocket run, which is what
                  --clear and --blocked mean on that command line
    <true_cm>     tape-measured reader-to-phone distance, same for both runs

THE CAPTURE, and it is the whole experiment. Everything else in this file is
arithmetic.

 1. `make cirdiag`, then `make flash CDK_BUILD=build/cdk-cirdiag`. The build
    directory is NOT optional: bare `make flash` flashes build/cdk-matter and
    the capture never starts. Never `flash-erase` -- the walk-up needs the Apple
    Home credential -- and do not touch SW2, which held through a reset is the
    factory reset.
 2. PUT THE PHONE ON A TRIPOD and measure reader-to-PHONE in cm. 100 cm, because
    aliro_approach's unlock_cm is 100 and a bias matters where it changes a
    decision. Not below ~50 cm.

    The tripod is the whole design and the first version of this experiment did
    not have it. Holding the phone in hand for one run and pocketing it for the
    other moves the phone 20-30 cm each way relative to the body, so a difference
    of tens of centimetres is geometry rather than channel and nothing in the
    capture separates them. With the phone fixed, only the SUBJECT moves, and
    every centimetre of difference is channel. Do not touch the tripod between
    the two runs.
 3. `make monitor CDK_RTT_BUILD=build/cdk-cirdiag | tee hand.log`. The firmware
    puts the DS-TWR distance on the [ALAB] line itself as `d=<cm> dage=<ms>`, so
    the range arrives attached to the reception it belongs to and no bench TUI
    is involved. Captures taken before that field existed have to be parsed the
    old way, by scraping the TUI's rendered status line, and parse_alab.py still
    does that when `d=` is absent.
 4. CLEAR run: nothing between phone and reader, you stand off to the side or
    behind the reader. ~2 minutes.
 5. BLOCKED run: phone untouched on the tripod, YOU stand between it and the
    reader with your body squarely in the path, back nearly against the tripod
    rather than midway. Same duration.

    WATCH `d=` BEFORE STARTING THE CLOCK. It must sit well above the tape figure.
    If it reads near the tape figure you are not blocking, and the capture will
    come out flat: Result 21 threw away a run where only 5 of 39 frame-matched
    receptions had a channel signature different from clear. Ten seconds of
    watching catches it; nothing in the analysis can.

    DO NOT MOVE THE TRIPOD BETWEEN RUNS, and if it moves, redo BOTH runs. The
    unknown antenna constant cancels only when the phone is in the same place for
    both, and no amount of arithmetic afterwards can separate a geometry change
    from a channel one. Result 21 threw away a second run this way.
 6. `parse_alab.py --clear clear.log --blocked blocked.log --distance -o out.csv`,
    then feed that one file here with the tape figure.

THE ABSOLUTE BIAS COLUMN IS NOT TRUSTWORTHY AND THE DIFFERENCE IS. Nothing in
this project has ever programmed the DW3000 antenna-delay registers: the
responder's distance is a pure scale with no offset term, `d_mm = tof * 4692 /
1000` (`ccc_shim_rx.c:631`), so every range it reports carries an unknown
constant. `true_cm` is therefore printed for orientation only. The deciding
number is blocked MINUS clear with the phone fixed, where that constant cancels
exactly, which is why it is the only figure this script puts an interval on.

WHICH DISTANCE TO STAND AT. 100 cm, because `aliro_approach`'s `unlock_cm` is
100 and a bias only matters where it changes a decision. A second pair at 200 cm
costs two more minutes and answers a different question: whether the bias grows
with distance, which it should if it is a signal-to-noise effect and which would
say the correction has to scale rather than be a constant. Do not go below about
50 cm: `ccc_ds_twr_tof()` underflows near contact, and while the shipping path
works around it by recomputing signed (`ccc_shim_rx.c:625-630`), that is not a
regime to take a calibration measurement in.

WHY STANDING STILL MATTERS. Result 13 could not test its hypothesis because the
subject walked: 25.6 cm of movement between consecutive receptions contaminates
every per-reception statistic. This experiment measures a BIAS at a known
distance, so any movement goes straight into the number being measured. If you
cannot stand still, the capture is not usable.

WHAT THE ANSWER DECIDES.

  bias is large and positive  an obstructed owner reads farther than they are, so
                              the door unlocks late or not at all, and a bounded
                              range correction is the right use of the
                              classifier: it ADDS permission by undoing a
                              measurement error, and it never touches the STS
                              check that defends against distance reduction.
  bias is near zero           there is nothing to compensate. The classifier
                              stays unwired and this line of work stops.
  bias is negative            an obstructed phone reads CLOSER than it is, which
                              would be a security-relevant finding rather than a
                              usability one, and nothing should be compensated
                              until it is understood.

The classifier's own call rate per condition is reported beside the bias, and it
is free: it is the first check of whether the shipped tree, trained on a walking
subject, still says "obstructed" for a pocket when the subject is standing.
"""

import csv
import sys

import numpy as np

USAGE = ("usage: ai/tinyml/.venv/bin/python ai/tinyml/range_bias.py "
         "<door.csv> <true_cm>\n   or: ... <hand.csv> <pocket.csv> <true_cm>")
BOOTSTRAP = 2000
SEED = 42
# The shipped boundary, from modules/woz_ml/src/woz_ml_los_lin.h. Read rather
# than re-fitted: this file checks the model that ships, not a new one.
LIN_W = np.array([-0.174858306, -0.400662594])
LIN_B = -52.466789493


COLS = ("fp_resid", "rx_pwr", "dist_cm")


def load(path, label=None):
    """One parse_alab.py CSV, optionally narrowed to one label.

    `label` exists because `parse_alab.py -o` writes BOTH runs into one file with
    a label column -- 0 for --clear, 1 for --blocked -- so the single-file form
    is what the capture procedure actually produces. Two separate files still
    work, for a capture split some other way.
    """
    rows = list(csv.DictReader(open(path)))
    missing = [c for c in COLS if c not in rows[0]]
    if missing:
        sys.exit(f"{path}: missing column(s) {missing}. The capture must be parsed with"
                 " `parse_alab.py --distance`, and that needs a `make openaliro` log:"
                 " a plain `make monitor` capture carries no range at all.")
    if label is not None:
        rows = [r for r in rows if int(r["label"]) == label]
        if not rows:
            sys.exit(f"{path}: no rows with label {label}. The single-file form wants"
                     " both runs, --clear for hand and --blocked for pocket.")
    return {c: np.array([float(r[c]) for r in rows]) for c in COLS}


def boot_diff(a, b, rng):
    """Bootstrap the difference of medians. Medians, because a single reception
    that lost the first path entirely is a real event and would drag a mean."""
    out = np.empty(BOOTSTRAP)
    for i in range(BOOTSTRAP):
        out[i] = (np.median(rng.choice(b, len(b))) - np.median(rng.choice(a, len(a))))
    return out


def main():
    if len(sys.argv) == 3:
        hand, pocket = load(sys.argv[1], 0), load(sys.argv[1], 1)
        true_cm = float(sys.argv[2])
    elif len(sys.argv) == 4:
        hand, pocket = load(sys.argv[1]), load(sys.argv[2])
        true_cm = float(sys.argv[3])
    else:
        sys.exit(USAGE)
    rng = np.random.RandomState(SEED)

    print(f"true distance {true_cm:.0f} cm, "
          f"{len(hand['dist_cm'])} clear / {len(pocket['dist_cm'])} blocked receptions\n")

    print("%-10s %8s %8s %8s %11s %6s" % (
        "condition", "median", "mean", "IQR", "vs tape*", "n"))
    print("-" * 56)
    for tag, d in (("clear", hand), ("blocked", pocket)):
        r = d["dist_cm"]
        iqr = np.percentile(r, 75) - np.percentile(r, 25)
        print("%-10s %8.1f %8.1f %8.1f %+11.1f %6d" % (
            tag, np.median(r), np.mean(r), iqr, np.median(r) - true_cm, len(r)))
    print("* orientation only: antenna delay has never been calibrated on this")
    print("  project, so every range carries an unknown constant offset. The")
    print("  difference below is what survives that, because the offset cancels.")

    # --- the number the decision turns on ------------------------------------
    diff = np.median(pocket["dist_cm"]) - np.median(hand["dist_cm"])
    bs = boot_diff(hand["dist_cm"], pocket["dist_cm"], rng)
    lo, hi = np.percentile(bs, [2.5, 97.5])
    print(f"\nblocked minus clear: {diff:+.1f} cm, 95% CI [{lo:+.1f}, {hi:+.1f}]")
    if lo > 0:
        print("  -> obstructed reads FARTHER. A bounded correction is justified;")
        print("     size it from the median, not from the tail.")
    elif hi < 0:
        print("  -> pocketed reads CLOSER. Do not compensate. Understand it first.")
    else:
        print("  -> interval straddles zero. Nothing to compensate, and nothing")
        print("     here says the classifier belongs in the ranging path.")

    # --- does the shipped model even agree the pocket is obstructed? ---------
    # Free, and it is the field check the training data could not provide: the
    # tree was fitted on a WALKING subject and this capture stands still.
    print("\nthe shipped model on this capture, as a sanity check")
    print("%-10s %14s %12s" % ("condition", "obstructed %", "mean conf"))
    print("-" * 40)
    for tag, d in (("clear", hand), ("blocked", pocket)):
        m = np.column_stack([d["fp_resid"], d["rx_pwr"]]) @ LIN_W + LIN_B
        # The linear boundary is the confidence, not the classifier; the class
        # here is its sign, which is only used to say whether the two conditions
        # separate at all. woz_ml_los_classify() is the tree and is the authority.
        print("%-10s %13.1f%% %12.2f" % (tag, 100.0 * (m > 0).mean(), np.abs(m).mean()))
    print("\n(sign of the linear margin, which is NOT the shipped classifier: it")
    print(" scores 0.8222 against the tree's 0.8773. It is here because it is the")
    print(" one form this script can evaluate without the generated tree header.)")


if __name__ == "__main__":
    main()
