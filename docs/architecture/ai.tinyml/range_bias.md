<!-- generated documentation — edit the source, not this file -->
# `ai/tinyml/range_bias.py`

Does an OBSTRUCTED phone read FARTHER than an unobstructed one, phone fixed?

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

**discussed in** [`ai/tinyml/RESULTS.md`](../../../ai/tinyml/RESULTS.md)

## API

### `load(path, label=None)`
`ai/tinyml/range_bias.py:118`

One parse_alab.py CSV, optionally narrowed to one label.

`label` exists because `parse_alab.py -o` writes BOTH runs into one file with
a label column -- 0 for --clear, 1 for --blocked -- so the single-file form
is what the capture procedure actually produces. Two separate files still
work, for a capture split some other way.

**called by** `main`

### `boot_diff(a, b, rng)`
`ai/tinyml/range_bias.py:140`

Bootstrap the difference of medians. Medians, because a single reception
that lost the first path entirely is a real event and would drag a mean.

**called by** `main`

<details><summary>Undocumented (1)</summary>

- `main`

</details>
