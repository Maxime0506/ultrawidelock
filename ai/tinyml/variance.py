#!/usr/bin/env python3
"""Does round-to-round variance carry obstruction information on this board?

Run from the repository root:
    ai/tinyml/.venv/bin/python ai/tinyml/variance.py

Then ai/tinyml/variance_significance.py, which tests whether the best gain here
survives a bootstrap and whether the shipped model already encodes it. Result 13
in RESULTS.md is what these two produced.

THE HYPOTHESIS UNDER TEST, from an external proposal: a torso shadow flickers
because a standing person sways and breathes, while a door is rigid, so the
round-to-round variance of the channel should separate body-obstruction from
door-obstruction where the instantaneous features cannot.

WHAT THIS DATA CAN AND CANNOT SAY ABOUT IT. There is no door in these captures.
`clear` is the phone in hand facing the reader; `obstructed` is the phone behind
the back or in a back pocket. Both classes are a body. So this cannot test
body-versus-door directly. What it CAN test is the premise the idea rests on:
that variance carries obstruction information at all. And the test is biased
TOWARDS the hypothesis, because the obstructed class here is exactly the
flickering case the proposal says variance detects. A null result is therefore
strong evidence against; a positive result is weak evidence for, and would need
a real door to confirm.

TWO TRAPS THIS AVOIDS.

1. Motion. The subject walked: distance sweeps 0.6 m to 5.8 m, with a median
   change of 33 cm between consecutive receptions. Raw variance of received
   power would mostly measure the walk. So the variance is taken over
   `fp_resid`, which already has free-space spreading at the measured range
   removed, and a distance-matched control is reported beside it.

2. Window leakage. Rolling features make neighbouring rows share inputs, so a
   random train/test split lets the test set peek at its own neighbours through
   the window. Every split here holds out CONTIGUOUS blocks instead. The
   difference is not cosmetic: the same features score far higher under a random
   split, and that number would be fiction.

A SECOND HYPOTHESIS, from the same proposal, is tested at the bottom: subtract a
causal running mean of `fp_resid` from itself, so a session-wide offset cancels
and the classifier sees only the shortfall below the reader's own recent normal.
Result 10 already ruled out running baselines built from ABSOLUTE power, and its
reason was mechanical rather than empirical: such a baseline anchors to the
closest recent reception, so the shortfall mostly re-encodes distance. The
question here is whether that reason survives being applied to `fp_resid`, which
has range already subtracted and therefore cannot re-encode it. Read the section
header there for what this data can and cannot say about it.
"""

import csv
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import balanced_accuracy_score
from sklearn.model_selection import GroupKFold
from sklearn.tree import DecisionTreeClassifier

CSV = "ai/tinyml/captures-door-2026-08-07-ranged.csv"
WINDOWS = (3, 5, 9)
N_BLOCKS = 8       # contiguous blocks per label, so 16 groups for GroupKFold
# Baseline lengths for the running-mean shortfall. The ceiling is the block:
# 544 receptions in 16 blocks is ~34 rows each, and a baseline restarted per
# block cannot be longer than the block it restarts in. 25 already spends most
# of one, which is the limit this data imposes on the idea and not a tuning
# choice.
SHORTFALL_W = (5, 9, 17, 25)
SEED = 42


def load():
    rows = list(csv.DictReader(open(CSV)))
    cols = ("fp_pwr", "rx_pwr", "pwr_diff", "rxpacc", "dist_cm", "fp_resid")
    d = {c: np.array([float(r[c]) for r in rows]) for c in cols}
    d["label"] = np.array([int(r["label"]) for r in rows])
    return d


def rolling_std(x, w):
    """Trailing standard deviation over w samples, NaN until the window fills.

    Trailing and not centred: a centred window would use receptions from the
    future, which a lock does not have.
    """
    out = np.full(x.shape, np.nan)
    for i in range(w - 1, len(x)):
        out[i] = np.std(x[i - w + 1:i + 1], ddof=1)
    return out


def rolling_absdiff(x, w):
    """Mean absolute round-to-round change over w samples. Robust cousin of the
    standard deviation, and the one a fixed-point implementation would pick."""
    dx = np.abs(np.diff(x, prepend=x[0]))
    out = np.full(x.shape, np.nan)
    for i in range(w - 1, len(x)):
        out[i] = np.mean(dx[i - w + 1:i + 1])
    return out


def causal_shortfall(x, w):
    """x minus the mean of the w receptions BEFORE it. NaN until w have passed.

    Strictly causal and excluding the current sample: the question the feature
    asks is "how far below its own recent normal is THIS reception", and a
    baseline that includes the reception blends the signal into its own
    reference and shrinks it.
    """
    out = np.full(x.shape, np.nan)
    for i in range(w, len(x)):
        out[i] = x[i] - np.mean(x[i - w:i])
    return out


def interleave(a, b):
    """Round-robin two index runs proportionally, so neither ends as a solid tail."""
    pos = np.concatenate([(np.arange(len(a)) + 0.5) / len(a),
                          (np.arange(len(b)) + 0.5) / len(b)])
    return np.concatenate([a, b])[np.argsort(pos, kind="stable")]


def streams_single_class(groups):
    """One stream per block: the baseline only ever sees one class."""
    return [np.flatnonzero(groups == g) for g in np.unique(groups)]


def streams_mixed(groups):
    """One stream per block PAIR, k-th clear block interleaved with k-th obstructed.

    Result 10 established this control as mandatory rather than optional. Over a
    single-class stream the baseline anchors to that class's own level and
    normalises away precisely the thing the feature is meant to detect, so a
    null there is uninformative about a deployed reader, which meets both
    classes. The pair is also the held-out unit below, so a baseline never spans
    the train/test split and no window has to be dropped for leakage.
    """
    pairs = groups % N_BLOCKS
    return pairs, [interleave(np.flatnonzero((pairs == p) & (groups < N_BLOCKS)),
                              np.flatnonzero((pairs == p) & (groups >= N_BLOCKS)))
                   for p in range(N_BLOCKS)]


def stream_shortfall(x, streams, w):
    """causal_shortfall restarted at the head of every stream.

    Restarting is what makes this leak-free without clean_window(): a baseline
    that ran across a stream boundary would be computed partly from rows on the
    other side of the split, which is the same leak the blocked CV exists to
    stop, wearing a different hat.
    """
    out = np.full(x.shape, np.nan)
    for s in streams:
        out[s] = causal_shortfall(x[s], w)
    return out


def cohens_d(a, b):
    na, nb = len(a), len(b)
    s = np.sqrt(((na - 1) * np.var(a, ddof=1) + (nb - 1) * np.var(b, ddof=1)) / (na + nb - 2))
    return (np.mean(b) - np.mean(a)) / s


def blocked_groups(label):
    """Contiguous blocks within each label run, as GroupKFold groups."""
    groups = np.zeros(len(label), dtype=int)
    g = 0
    for L in (0, 1):
        idx = np.flatnonzero(label == L)
        for chunk in np.array_split(idx, N_BLOCKS):
            groups[chunk] = g
            g += 1
    return groups


def clean_window(v, groups, w):
    """Rows whose whole window lies inside one block, and is not NaN.

    A window that straddles a block boundary mixes a test row with training
    rows, which is the leak the blocked split exists to prevent. Dropping those
    rows costs (w-1) samples per boundary and removes the leak entirely.
    """
    ok = ~np.isnan(v)
    for i in range(1, w):
        ok[:i] = False
        ok[i:] &= groups[i:] == groups[:-i]
    return ok


def score(X, y, groups, model="tree"):
    """Balanced accuracy under GroupKFold over contiguous blocks."""
    gkf = GroupKFold(n_splits=8)
    preds = np.zeros(len(y))
    for tr, te in gkf.split(X, y, groups):
        if model == "tree":
            m = DecisionTreeClassifier(max_depth=2, random_state=SEED)
        elif model == "tree4":
            m = DecisionTreeClassifier(max_depth=4, random_state=SEED)
        else:
            m = RandomForestClassifier(n_estimators=200, random_state=SEED, n_jobs=-1)
        m.fit(X[tr], y[tr])
        preds[te] = m.predict(X[te])
    return balanced_accuracy_score(y, preds)


def main():
    d = load()
    y = d["label"]
    groups = blocked_groups(y)

    print(f"{len(y)} receptions, {int((y == 0).sum())} clear / {int((y == 1).sum())} obstructed")
    print(f"blocked CV: {N_BLOCKS} contiguous blocks per label, 8 folds\n")

    # --- how much of the round-to-round change is just the walk? -------------
    dd = np.abs(np.diff(d["dist_cm"], prepend=d["dist_cm"][0]))
    print("round-to-round |change in distance|: median %.1f cm, p90 %.1f cm" % (
        np.median(dd), np.percentile(dd, 90)))
    print("  -> the subject is walking; variance below is measured on fp_resid,")
    print("     which already has free-space spreading at that distance removed.\n")

    # --- the variance features, one at a time -------------------------------
    print("%-26s %8s %8s %8s" % ("feature", "d", "|d|rank", "solo bAcc"))
    print("-" * 56)
    built = {}
    for base in ("fp_resid", "rx_pwr", "pwr_diff"):
        for w in WINDOWS:
            for fn, tag in ((rolling_std, "std"), (rolling_absdiff, "absdiff")):
                v = fn(d[base], w)
                ok = ~np.isnan(v)
                name = f"{base}.{tag}{w}"
                built[name] = v
                dcoh = cohens_d(v[ok & (y == 0)], v[ok & (y == 1)])
                solo = score(v[ok].reshape(-1, 1), y[ok], groups[ok])
                print("%-26s %8.3f %8s %8.4f" % (name, dcoh, "", solo))

    # --- does any of it ADD to what ships? ----------------------------------
    # A depth-2 tree has three nodes and can decline to use a third feature at
    # all, so testing "does it add" at depth 2 tests whether the SHIPPED tree
    # changes its mind, not whether the information is there. Both are asked.
    base_X = np.column_stack([d["fp_resid"], d["rx_pwr"]])

    for model, tag in (("tree", "depth-2 tree, what ships"),
                       ("tree4", "depth-4 tree"),
                       ("forest", "random forest, 200 trees")):
        print(f"\nadding one variance feature to [fp_resid, rx_pwr] -- {tag}:\n")
        print("%-26s %10s %9s" % ("added feature", "bAcc", "delta"))
        print("-" * 48)
        best = (None, -99.0)
        for name, v in sorted(built.items()):
            ok = clean_window(v, groups, int(name[-1]))
            X = np.column_stack([base_X[ok], v[ok]])
            s = score(X, y[ok], groups[ok], model)
            # base score on the SAME rows, so the delta is not a change of sample
            b = score(base_X[ok], y[ok], groups[ok], model)
            if s - b > best[1]:
                best = (name, s - b, s, b)
            if abs(s - b) > 0.005:
                print("%-26s %10.4f %+9.4f" % (name, s, s - b))
        print("%-26s %10.4f %+9.4f   <- best" % (best[0], best[2], best[1]))
        print("   (rows with |delta| <= 0.005 omitted; base on the same rows was "
              f"{best[3]:.4f})")

    # --- the control that decides how to read all of the above --------------
    print("\ncontrol: distance alone, same splits — %.4f" % (
        score(d["dist_cm"].reshape(-1, 1), y, groups)))
    print("control: a shuffled label, shipped pair — %.4f" % (
        score(base_X, np.random.RandomState(SEED).permutation(y), groups)))

    shortfall(d, y, groups, base_X)


def shortfall(d, y, groups, base_X):
    """Does subtracting a causal running mean of fp_resid buy anything?

    WHAT THIS DATA CANNOT SAY, and it has to lead. The feature's purpose is to
    cancel a SESSION-wide offset -- the 2.9 dB/h drift of Result 9 -- and the
    tracked CSV carries no session column, so the honest test, holding out a
    session, cannot be run here at all. What is run is a proxy: contiguous
    blocks stand in for sessions. A block is minutes of one walk, not an hour
    apart, so it contains far less drift than the thing the feature targets, and
    a null under this proxy is therefore weaker evidence than a null under a
    real session split would be.

    Both streams are reported because they fail differently and only one of them
    resembles a deployed reader; see streams_mixed(). The mechanism check at the
    end is the part that does not depend on the classifier at all.
    """
    fp = d["fp_resid"]
    pairs, mixed = streams_mixed(groups)
    single = streams_single_class(groups)

    print("\n\n=== running-mean shortfall of fp_resid ===")
    print("proxy for a session hold-out: blocks stand in for sessions, and a")
    print("block is one walk rather than an hour apart. See the docstring.\n")
    print("%-8s %3s %6s %7s %7s %8s %8s %8s %9s" % (
        "stream", "w", "rows", "d", "solo", "+depth2", "+depth4", "+forest", "replaces"))
    print("-" * 74)

    for tag, streams, g in (("1-class", single, groups), ("mixed", mixed, pairs)):
        for w in SHORTFALL_W:
            v = stream_shortfall(fp, streams, w)
            ok = ~np.isnan(v)
            X = np.column_stack([base_X[ok], v[ok]])
            # fp_resid replaced by its own shortfall, both powers kept otherwise
            Xr = np.column_stack([v[ok], d["rx_pwr"][ok]])
            deltas = []
            for model in ("tree", "tree4", "forest"):
                b = score(base_X[ok], y[ok], g[ok], model)   # same rows, same folds
                deltas.append(score(X, y[ok], g[ok], model) - b)
            rep = score(Xr, y[ok], g[ok]) - score(base_X[ok], y[ok], g[ok])
            print("%-8s %3d %6d %7.3f %7.4f %+8.4f %+8.4f %+8.4f %+9.4f" % (
                tag, w, int(ok.sum()),
                cohens_d(v[ok & (y == 0)], v[ok & (y == 1)]),
                score(v[ok].reshape(-1, 1), y[ok], g[ok]),
                deltas[0], deltas[1], deltas[2], rep))

    # --- the control that disarms the 1-class column -------------------------
    # fp_resid minus its own shortfall IS the trailing local mean, and the model
    # has both columns, so it can recover that mean whenever it wants it. In a
    # single-class block the local mean is a CLASS AVERAGE. Scoring it alone
    # says how much of the "gain" above is a label oracle that no deployed
    # reader could compute, and the giveaway is the direction: an oracle
    # sharpens as the window lengthens, a real feature does not.
    print("\ncontrol: the recovered trailing mean, scored alone (oracle check)")
    print("%-8s %3s %6s %10s" % ("stream", "w", "rows", "solo bAcc"))
    print("-" * 30)
    for tag, streams, g in (("1-class", single, groups), ("mixed", mixed, pairs)):
        for w in SHORTFALL_W:
            v = stream_shortfall(fp, streams, w)
            ok = ~np.isnan(v)
            print("%-8s %3d %6d %10.4f" % (tag, w, int(ok.sum()),
                  score((fp[ok] - v[ok]).reshape(-1, 1), y[ok], g[ok])))
    print("reference: fp_resid alone, all rows — %.4f" % (
        score(fp.reshape(-1, 1), y, groups)))

    # --- the mechanism, measured without a classifier in the way -------------
    # If the feature works by removing a per-block offset then the spread of
    # per-block means must shrink. If it does not shrink, no accuracy number
    # above can be attributed to the mechanism claimed for it.
    print("\nmechanism: spread of per-block means WITHIN a class (dB, lower = offset removed)")
    print("%-8s %3s %10s %10s" % ("stream", "w", "clear", "obstructed"))
    print("-" * 34)
    raw = [np.std([fp[(groups == g_) & (y == L)].mean() for g_ in np.unique(groups)
                   if ((groups == g_) & (y == L)).any()]) for L in (0, 1)]
    print("%-8s %3s %10.3f %10.3f   <- fp_resid itself" % ("none", "-", raw[0], raw[1]))
    for tag, streams in (("1-class", single), ("mixed", mixed)):
        for w in SHORTFALL_W:
            v = stream_shortfall(fp, streams, w)
            s = []
            for L in (0, 1):
                m = [np.nanmean(v[(groups == g_) & (y == L)]) for g_ in np.unique(groups)
                     if ((groups == g_) & (y == L) & ~np.isnan(v)).any()]
                s.append(np.std(m))
            print("%-8s %3d %10.3f %10.3f" % (tag, w, s[0], s[1]))


if __name__ == "__main__":
    main()
