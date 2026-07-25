#!/usr/bin/env python3
"""Aliro Lab: score a captured reader serial log.

Usage: python3 tools/aliro_lab.py <capture.log> [report.html]

Parses the structured "[ALAB] t=<us> ev=..." trace lines the firmware emits
when CONFIG_WOZ_ALIRO_LAB is enabled (see modules/woz_aliro/src/aliro_lab.h),
groups them into walk-up transactions, and reports phase timings, the flow
taken (fast vs standard), and pass/warn/fail invariant checks — to the
terminal and as a self-contained HTML report (default: <capture.log>.html).

Every check encodes an invariant of this repo's reader implementation (see
internal notes in the check text), nothing else. Exit status: 0 = no failing
check, 1 = at least one FAIL, 2 = usage/input error.
"""

import html
import os
import re
import sys

# One trace line: "[ALAB] t=<us> ev=<name>[ <key>=<val>]..." anywhere in a
# console line (log prefixes and interleaved noise are expected).
ALAB_RE = re.compile(r"\[ALAB\] t=(\d+) ev=(\S+)((?: \S+=-?\d+)*)")
ATTR_RE = re.compile(r"(\S+)=(-?\d+)")

# Latency phases in pipeline order (mirrors enum aliro_lat_phase / the
# k_phase_name strings the firmware dumps as ph.<name>). Older captures carry
# a subset; absent phases simply do not render.
PHASES = [
    ("connect", "BLE connect"),
    ("spsm", "SPSM/versions read"),
    ("ver", "version selected"),
    ("l2cap", "L2CAP channel open"),
    ("op05", "session opened (op-0x05)"),
    ("auth0", "AUTH0 sent"),
    ("a0rsp", "AUTH0 response (flow fork)"),
    ("auth1", "auth segment done"),
    ("exch", "EXCHANGE accepted"),
    ("apc", "AP-Completed sent"),
    ("irs", "Initiate-Ranging-Session"),
    ("m1", "ranging setup M1 sent"),
    ("m2", "ranging setup M2 received"),
    ("m3", "ranging setup M3 sent"),
    ("m4rx", "ranging setup M4 received"),
    ("m4", "UWB session active (M4)"),
    ("range", "first range"),
    ("trusted", "trusted range"),
    ("near", "approach threshold met"),
    ("bolt", "bolt driven"),
]
PHASE_INDEX = {key: i for i, (key, _) in enumerate(PHASES)}
PHASE_LABEL = dict(PHASES)

# Ranging-SDU names by (protocol, message id), mirroring the engine's
# aliro_uwb_msg_spec.h. Ids repeat across protocols, so both bytes are needed:
# proto-2 id-1 is Initiate-Ranging-Session, proto-1 id-1 is M2.
SDU_NAMES = {
    (1, 0): "M1", (1, 1): "M2", (1, 2): "M3", (1, 3): "M4",
    (1, 4): "SUSPEND-REQ", (1, 5): "SUSPEND-RSP",
    (1, 6): "RESUME-REQ", (1, 7): "RESUME-RSP",
    (2, 0): "EVENT", (2, 1): "IRS", (2, 2): "READER-STATUS-CHANGED",
    (2, 3): "READER-STATUS-AP", (2, 4): "PKE-REQ",
    (2, 5): "INITIATE-AP", (2, 6): "INITIATE-AP-RKE",
}


def sdu_label(ev):
    """Human name for an rtx/rrx event, e.g. "rtx M1".

    Captures from firmware before the id-based latency stamping carry no
    `proto`, and there the id alone is ambiguous; those render as "rrx id=1".
    """
    mid = ev.attrs.get("id", -1)
    proto = ev.attrs.get("proto")
    if proto is None:
        return "%s id=%d" % (ev.name, mid)
    return "%s %s" % (ev.name, SDU_NAMES.get((proto, mid),
                                             "proto=%d id=%d" % (proto, mid)))

# Bench-derived timing envelopes (ms): generous margins over the measured
# walk-up numbers on this hardware (auth segment 203 ms fast / 757 ms
# standard, connect->bolt 2.6 s). The auth segment is op05->auth1 (the phone
# initiating the access protocol through EXCHANGE), NOT connect->auth1: the
# connect->op05 gap is BLE/phone start latency, not reader auth work, and
# folding it in mismeasures the segment. Exceeding an envelope is WARN, not FAIL.
BUDGET_AUTH_FAST_MS = 400
BUDGET_AUTH_STD_MS = 1100
BUDGET_BOLT_MS = 4000


class Event:
    def __init__(self, t_us, name, attrs, line_no):
        self.t_us = t_us
        self.name = name
        self.attrs = attrs
        self.line_no = line_no


class Transaction:
    """One walk-up: the events between a session.start and its session.end."""

    def __init__(self, index):
        self.index = index
        self.events = []
        self.open = True  # no session.end seen (truncated capture)

    def finish(self):
        self.events.sort(key=lambda e: e.t_us)
        # First stamp of each phase; duplicates recorded for the `once` check.
        self.phases = {}
        self.dup_phases = []
        for e in self.events:
            if e.name.startswith("ph."):
                key = e.name[3:]
                if key in self.phases:
                    self.dup_phases.append(key)
                else:
                    self.phases[key] = e.t_us
        self.flow = "incomplete"
        if self.has("flow.fast"):
            self.flow = "fast"
        elif self.has("flow.standard"):
            self.flow = "standard"
        # Trusted-range samples (the approach curve), one per ranged block.
        self.ranges = [(e.t_us, e.attrs["cm"]) for e in self.events
                       if e.name == "range" and "cm" in e.attrs]

    def has(self, name):
        return any(e.name == name for e in self.events)

    def first(self, name):
        for e in self.events:
            if e.name == name:
                return e
        return None

    def named(self, name):
        return [e for e in self.events if e.name == name]

    def t0(self):
        """Walk-up zero: the connect stamp, else the session.start line."""
        if "connect" in self.phases:
            return self.phases["connect"]
        start = self.first("session.start")
        return start.t_us if start else self.events[0].t_us

    def offset_ms(self, key):
        return (self.phases[key] - self.t0()) / 1000.0

    def last_phase(self):
        last = None
        for key, _ in PHASES:
            if key in self.phases:
                last = key
        return last


def parse_events(text):
    events = []
    for line_no, line in enumerate(text.splitlines(), 1):
        m = ALAB_RE.search(line)
        if not m:
            continue
        attrs = {k: int(v) for k, v in ATTR_RE.findall(m.group(3))}
        events.append(Event(int(m.group(1)), m.group(2), attrs, line_no))
    return events


def split_transactions(events):
    """Group by session boundaries in LINE order (the ph.* dump lines carry
    historical timestamps, so t order can't delimit walk-ups)."""
    txns = []
    current = None
    for e in events:
        if e.name == "session.start":
            current = Transaction(len(txns) + 1)
            txns.append(current)
            current.events.append(e)
        elif current is not None:
            current.events.append(e)
            if e.name == "session.end":
                current.open = False
                current = None
        # events before the first session.start are boot noise: dropped
    for t in txns:
        t.finish()
    return txns


def run_checks(txn):
    """Invariant checks; each returns (id, class, status, detail) with class
    the worst it can score (fail/warn) and status pass/warn/fail/n-a."""
    results = []

    def add(cid, cls, ok, detail, applicable=True):
        status = "n/a" if not applicable else ("pass" if ok else cls)
        results.append((cid, cls, status, detail))

    ph = txn.phases

    # order — phase stamps are one-shot marks of a forward-only pipeline, so
    # present phases must be monotonic in enum order.
    bad = None
    prev_key, prev_t = None, None
    for key, _ in PHASES:
        if key not in ph:
            continue
        if prev_t is not None and ph[key] < prev_t:
            bad = (prev_key, key)
            break
        prev_key, prev_t = key, ph[key]
    add("order", "fail", bad is None,
        "phase stamps monotonic in pipeline order" if bad is None else
        "ph.%s stamped after ph.%s" % (bad[0], bad[1]))

    # once — marks and the dump are both one-shot per walk-up.
    add("once", "fail", not txn.dup_phases,
        "each phase stamped at most once" if not txn.dup_phases else
        "duplicate phase line(s): %s" % ", ".join(sorted(set(txn.dup_phases))))

    # flow — the reader takes exactly one auth path; flow.standard is emitted
    # at AUTH1 send, flow.fast on cryptogram match, never both. ph.auth1 is
    # the auth-segment end stamp and exists on BOTH paths, so it demands a
    # flow event.
    both = txn.has("flow.fast") and txn.has("flow.standard")
    if both:
        add("flow", "fail", False, "both flow.fast and flow.standard present")
    elif "auth1" in ph and txn.flow == "incomplete":
        add("flow", "fail", False, "auth segment completed but no flow event")
    else:
        add("flow", "fail", True, "%s flow" % txn.flow,
            applicable="auth1" in ph or txn.flow != "incomplete")

    # setup — reaching UWB-active means the engine ran the full ranging setup:
    # the device's Initiate-Ranging-Session before our first reply, then at
    # least 2 TX and 2 RX setup messages by the m4 stamp.
    #
    # The IRS is proto-2 id-1; M2 is proto-1 id-1. Message ids repeat across
    # protocols, so `proto` is what tells them apart. Firmware before the
    # id-based latency stamping did not emit it, and there the first id=1 RX
    # is the IRS by arrival order -- keep reading those logs.
    if "m4" in ph:
        m4_t = ph["m4"]
        rtx = [e for e in txn.named("rtx") if e.t_us <= m4_t]
        rrx = [e for e in txn.named("rrx") if e.t_us <= m4_t]
        irs = [e for e in rrx if e.attrs.get("id") == 1
               and e.attrs.get("proto", 2) == 2]
        first_rtx_t = rtx[0].t_us if rtx else None
        ok = (len(rtx) >= 2 and len(rrx) >= 2 and irs and
              first_rtx_t is not None and irs[0].t_us <= first_rtx_t)
        add("setup", "fail", ok,
            "ranging setup complete (%d tx, %d rx before UWB active)"
            % (len(rtx), len(rrx)) if ok else
            "UWB active without a full setup exchange (%d tx, %d rx, IRS %s)"
            % (len(rtx), len(rrx), "seen" if irs else "missing"))
    else:
        add("setup", "fail", True, "UWB session not reached", applicable=False)

    # trusted-bolt — the approach controller only feeds trusted ranges into
    # the unlock median, so a driven bolt requires a trusted range first.
    if "bolt" in ph:
        ok = "trusted" in ph and ph["trusted"] <= ph["bolt"]
        add("trusted-bolt", "fail", ok,
            "bolt driven after a trusted range" if ok else
            "bolt driven without a preceding trusted range")
    else:
        add("trusted-bolt", "fail", True, "bolt not driven", applicable=False)

    # grant — the Wallet grant notification fires on the first trusted range
    # of an approach; a trusted range without it regresses the unlock
    # animation.
    if "trusted" in ph:
        grants = [e for e in txn.named("grant.sent") if e.t_us >= ph["trusted"]]
        add("grant", "warn", bool(grants),
            "grant sent after first trusted range" if grants else
            "no grant.sent after the trusted range (Wallet animation regression)")
    else:
        add("grant", "warn", True, "no trusted range", applicable=False)

    # kp — a standard-flow unlock against a trusted credential mints the
    # Kpersistent that lets the next walk-up go fast (dev-accepted untrusted
    # credentials legitimately skip this).
    if txn.flow == "standard" and "bolt" in ph:
        ok = txn.has("kp.minted")
        add("kp", "warn", ok,
            "Kpersistent minted (next walk-up can go fast)" if ok else
            "standard-flow unlock without kp.minted (fast path won't self-arm)")
    else:
        add("kp", "warn", True, "standard-flow unlock not applicable",
            applicable=False)

    # budget — bench-derived envelopes; a slow phase is worth a look, not a
    # failure.
    slow = []
    if "auth1" in ph and txn.flow in ("fast", "standard"):
        limit = BUDGET_AUTH_FAST_MS if txn.flow == "fast" else BUDGET_AUTH_STD_MS
        seg_start = ph["op05"] if "op05" in ph else txn.t0()
        got = (ph["auth1"] - seg_start) / 1000.0
        if got > limit:
            slow.append("auth segment %.0f ms (envelope %d)" % (got, limit))
    if "bolt" in ph:
        got = txn.offset_ms("bolt")
        if got > BUDGET_BOLT_MS:
            slow.append("connect->bolt %.0f ms (envelope %d)" % (got, BUDGET_BOLT_MS))
    applicable = "auth1" in ph or "bolt" in ph
    add("budget", "warn", not slow,
        "within bench envelopes" if not slow else "; ".join(slow),
        applicable=applicable)

    # complete — a walk-up that never drove the bolt.
    if "bolt" in ph:
        add("complete", "warn", True, "walk-up completed (bolt driven)")
    else:
        last = txn.last_phase()
        add("complete", "warn", False,
            "incomplete walk-up; last phase reached: %s"
            % (PHASE_LABEL[last] if last else "none"))

    return results


def worst_status(all_checks):
    statuses = [s for checks in all_checks for (_, _, s, _) in checks]
    if "fail" in statuses:
        return "fail"
    if "warn" in statuses:
        return "warn"
    return "pass"


def fmt_ms(us_delta):
    return "%.1f" % (us_delta / 1000.0)


# ---- terminal report ----

def render_terminal(name, txns, checks_by_txn, use_color):
    def paint(status, text):
        if not use_color:
            return text
        code = {"pass": "32", "warn": "33", "fail": "31"}.get(status)
        return "\033[%sm%s\033[0m" % (code, text) if code else text

    out = []
    out.append("Aliro Lab — %s" % name)
    if not txns:
        out.append("no [ALAB] transactions found "
                   "(flash a lab build, then `lab on` at the console before the walk-up)")
        return "\n".join(out) + "\n"

    for txn, checks in zip(txns, checks_by_txn):
        head = "transaction %d: %s flow" % (txn.index, txn.flow.upper())
        if "bolt" in txn.phases:
            head += " — UNLOCKED (connect->bolt %s ms)" % fmt_ms(
                txn.phases["bolt"] - txn.t0())
        else:
            head += " — no unlock"
        if txn.open:
            head += " [capture truncated: no session.end]"
        out.append("")
        out.append(head)

        out.append("  phase timeline (ms from connect):")
        prev_t = None
        for key, label in PHASES:
            if key not in txn.phases:
                continue
            t = txn.phases[key]
            delta = "" if prev_t is None else "  (+%s)" % fmt_ms(t - prev_t)
            out.append("    %8s  %-26s%s" % (fmt_ms(t - txn.t0()), label, delta))
            prev_t = t

        setup = [e for e in txn.events if e.name in ("rtx", "rrx")]
        if setup:
            out.append("  ranging setup: " + ", ".join(
                sdu_label(ev) for ev in setup))

        if txn.ranges:
            cms = [cm for _, cm in txn.ranges]
            out.append("  approach: %d trusted ranges (%d cm first, %d cm closest)"
                       % (len(cms), cms[0], min(cms)))

        out.append("  checks:")
        for cid, _cls, status, detail in checks:
            out.append("    %s  %-12s %s"
                       % (paint(status, "%-4s" % status.upper()), cid, detail))

    counts = {"pass": 0, "warn": 0, "fail": 0, "n/a": 0}
    for checks in checks_by_txn:
        for _, _, status, _ in checks:
            counts[status] += 1
    overall = worst_status(checks_by_txn)
    out.append("")
    out.append("overall: %s (%d transaction(s); %d pass, %d warn, %d fail)"
               % (paint(overall, overall.upper()), len(txns), counts["pass"],
                  counts["warn"], counts["fail"]))
    return "\n".join(out) + "\n"


# ---- HTML report ----

_CSS = """
:root {
  color-scheme: light dark;
  --surface: #fcfcfb; --plane: #f9f9f7;
  --ink: #0b0b0b; --ink-2: #52514e; --muted: #898781;
  --grid: #e1e0d9; --ring: rgba(11,11,11,0.10);
  --bar: #2a78d6; --track: #f0efec;
  --good: #0ca30c; --warning: #fab219; --critical: #d03b3b;
}
@media (prefers-color-scheme: dark) {
  :root {
    --surface: #1a1a19; --plane: #0d0d0d;
    --ink: #ffffff; --ink-2: #c3c2b7; --muted: #898781;
    --grid: #2c2c2a; --ring: rgba(255,255,255,0.10);
    --bar: #3987e5; --track: #26262a;
  }
}
* { box-sizing: border-box; margin: 0; }
body { background: var(--plane); color: var(--ink); padding: 2rem 1rem;
  font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 860px; margin: 0 auto; }
h1 { font-size: 1.35rem; margin-bottom: .25rem; }
.sub { color: var(--ink-2); margin-bottom: 1.5rem; }
section { background: var(--surface); border: 1px solid var(--ring);
  border-radius: 10px; padding: 1.25rem 1.5rem; margin-bottom: 1.25rem; }
h2 { font-size: 1.05rem; margin-bottom: .25rem; }
.meta { color: var(--ink-2); font-size: .9rem; margin-bottom: 1rem; }
.tiles { display: flex; gap: 1rem; flex-wrap: wrap; margin-bottom: 1.1rem; }
.tile { border: 1px solid var(--ring); border-radius: 8px;
  padding: .5rem .9rem; min-width: 8.5rem; }
.tile .k { color: var(--muted); font-size: .75rem; text-transform: uppercase;
  letter-spacing: .04em; }
.tile .v { font-size: 1.15rem; }
.lane { display: grid; grid-template-columns: 13.5rem 1fr 10.5rem;
  gap: .5rem; align-items: center; padding: .18rem 0; }
.lane:hover { background: var(--track); }
.lane .lbl { color: var(--ink-2); font-size: .9rem; text-align: right; }
.track { position: relative; height: 14px; background: none; }
.track .fill { position: absolute; top: 0; bottom: 0;
  background: var(--bar); border-radius: 4px; min-width: 2px; }
.chart-title { color: var(--muted); font-size: .75rem; text-transform: uppercase;
  letter-spacing: .04em; margin: 1rem 0 .25rem; }
svg.approach { width: 100%; height: auto; display: block; }
svg.approach .grid { stroke: var(--grid); stroke-width: 1; }
svg.approach .tick { fill: var(--muted); font-size: 11px; }
svg.approach .mark { stroke: var(--ink-2); stroke-width: 1; stroke-dasharray: 3 3; }
svg.approach .marklbl { fill: var(--ink-2); font-size: 11px; }
svg.approach .series { fill: none; stroke: var(--bar); stroke-width: 2; }
svg.approach .dot { fill: var(--bar); stroke: var(--surface); stroke-width: 2; }
.lane .val { color: var(--ink-2); font-size: .85rem;
  font-variant-numeric: tabular-nums; }
.lane .val .d { color: var(--muted); }
.axis { border-top: 1px solid var(--grid); margin: .35rem 0 .9rem; }
table { border-collapse: collapse; width: 100%; margin-top: .35rem; }
th { text-align: left; color: var(--muted); font-size: .75rem;
  text-transform: uppercase; letter-spacing: .04em; padding: .3rem .5rem; }
td { padding: .3rem .5rem; border-top: 1px solid var(--grid);
  font-size: .9rem; }
td.detail { color: var(--ink-2); }
.badge { display: inline-block; font-size: .75rem; font-weight: 600;
  padding: .1rem .5rem; border-radius: 99px; border: 1px solid; }
.badge.pass { color: var(--good); border-color: var(--good); }
.badge.warn { color: var(--warning); border-color: var(--warning); }
.badge.fail { color: var(--critical); border-color: var(--critical); }
.badge.na { color: var(--muted); border-color: var(--muted); }
.setup { color: var(--ink-2); font-size: .9rem; margin-top: .8rem; }
.setup code { color: var(--ink); }
.overall { font-size: 1.05rem; }
"""

_BADGE_TEXT = {"pass": "✓ PASS", "warn": "! WARN", "fail": "✗ FAIL", "n/a": "— N/A"}
_BADGE_CLASS = {"pass": "pass", "warn": "warn", "fail": "fail", "n/a": "na"}


def render_approach_svg(txn):
    """Distance-over-time chart of the approach: one dot per trusted range,
    dashed markers at grant/bolt/relock. Inline SVG, themed via the CSS vars."""
    if len(txn.ranges) < 2:
        return ""
    t0 = txn.t0()
    pts = [((t - t0) / 1000.0, cm) for t, cm in txn.ranges]
    marks = []
    for name, label in (("grant.sent", "grant"), ("relock.sent", "relock")):
        for ev in txn.named(name):
            marks.append(((ev.t_us - t0) / 1000.0, label))
    if "bolt" in txn.phases:
        marks.append(((txn.phases["bolt"] - t0) / 1000.0, "bolt"))
    width, height = 800.0, 220.0
    pad_l, pad_r, pad_t, pad_b = 48.0, 12.0, 16.0, 30.0
    # The x-domain follows the range data only; a marker far outside it (e.g. a
    # relock long after ranging stopped) would squash the curve, so those drop.
    xs = [x for x, _ in pts]
    ys = [y for _, y in pts]
    xmin, xmax = min(xs), max(xs)
    if xmax - xmin < 1e-9:
        xmax = xmin + 1.0
    marks = [(x, label) for x, label in marks if xmin <= x <= xmax]
    ypad = max((max(ys) - min(ys)) * 0.12, 5.0)
    ymin, ymax = max(0.0, min(ys) - ypad), max(ys) + ypad

    def sx(x):
        return pad_l + (width - pad_l - pad_r) * (x - xmin) / (xmax - xmin)

    def sy(y):
        return pad_t + (height - pad_t - pad_b) * (ymax - y) / (ymax - ymin)

    s = ['<svg class="approach" viewBox="0 0 %d %d" role="img" '
         'aria-label="trusted range in cm over time">' % (width, height)]
    for i in range(5):
        gval = ymin + (ymax - ymin) * i / 4.0
        gy = sy(gval)
        s.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" class="grid"/>'
                 % (pad_l, gy, width - pad_r, gy))
        s.append('<text x="%.1f" y="%.1f" class="tick" text-anchor="end">%d</text>'
                 % (pad_l - 6, gy + 3.5, round(gval)))
    for x in (xmin, xmax):
        s.append('<text x="%.1f" y="%.1f" class="tick" text-anchor="middle">'
                 "%.1f s</text>" % (sx(x), height - 10, x / 1000.0))
    for x, label in marks:
        lx = min(max(sx(x), pad_l + 20.0), width - pad_r - 24.0)
        s.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" class="mark"/>'
                 % (sx(x), pad_t, sx(x), height - pad_b))
        s.append('<text x="%.1f" y="%.1f" class="marklbl" text-anchor="middle">'
                 "%s</text>" % (lx, pad_t - 4, label))
    s.append('<polyline points="%s" class="series"/>'
             % " ".join("%.1f,%.1f" % (sx(x), sy(y)) for x, y in pts))
    for x, y in pts:
        s.append('<circle cx="%.1f" cy="%.1f" r="3.5" class="dot">'
                 "<title>%d cm at %.2f s</title></circle>" % (sx(x), sy(y), y, x / 1000.0))
    s.append("</svg>")
    return ('<p class="chart-title">approach — trusted range (cm) over time</p>'
            + "".join(s))


def render_html(name, txns, checks_by_txn):
    e = html.escape
    parts = []
    parts.append("<style>%s</style>" % _CSS)
    parts.append("<main>")
    parts.append("<h1>Aliro Lab report</h1>")
    parts.append('<p class="sub">capture: <code>%s</code> — %d transaction(s)</p>'
                 % (e(name), len(txns)))

    if not txns:
        parts.append("<section><h2>No transactions</h2>"
                     '<p class="meta">no [ALAB] lines found — flash a lab build, '
                     "then <code>lab on</code> at the console before the walk-up."
                     "</p></section>")

    for txn, checks in zip(txns, checks_by_txn):
        parts.append("<section>")
        parts.append("<h2>Transaction %d</h2>" % txn.index)
        note = " — capture truncated (no session.end)" if txn.open else ""
        parts.append('<p class="meta">%d trace events%s</p>'
                     % (len(txn.events), e(note)))

        bolt = "bolt" in txn.phases
        tiles = [("flow", txn.flow.upper()),
                 ("result", "UNLOCKED" if bolt else "no unlock")]
        if bolt:
            tiles.append(("connect → bolt",
                          fmt_ms(txn.phases["bolt"] - txn.t0()) + " ms"))
        parts.append('<div class="tiles">')
        for k, v in tiles:
            parts.append('<div class="tile"><div class="k">%s</div>'
                         '<div class="v">%s</div></div>' % (e(k), e(v)))
        parts.append("</div>")

        present = [(k, l) for k, l in PHASES if k in txn.phases]
        if present:
            t0 = txn.t0()
            span = max(txn.phases[k] - t0 for k, _ in present) or 1
            prev_t = None
            for key, label in present:
                t = txn.phases[key]
                seg_from = prev_t if prev_t is not None else t0
                left = 100.0 * (seg_from - t0) / span
                seg_w = 100.0 * (t - seg_from) / span
                delta = ("" if prev_t is None
                         else ' <span class="d">(+%s)</span>' % fmt_ms(t - prev_t))
                parts.append(
                    '<div class="lane" title="%s at t=%d µs">'
                    '<span class="lbl">%s</span>'
                    '<span class="track"><span class="fill" '
                    'style="left:%.2f%%;width:%.2f%%"></span></span>'
                    '<span class="val">%s ms%s</span></div>'
                    % (e(label), t, e(label), left, seg_w, fmt_ms(t - t0), delta))
                prev_t = t
            parts.append('<div class="axis"></div>')

        chart = render_approach_svg(txn)
        if chart:
            parts.append(chart)

        setup = [ev for ev in txn.events if ev.name in ("rtx", "rrx")]
        if setup:
            parts.append('<p class="setup">ranging setup: %s</p>' % ", ".join(
                "<code>%s</code>" % e(sdu_label(ev)) for ev in setup))

        parts.append("<table><tr><th>check</th><th>status</th>"
                     "<th>detail</th></tr>")
        for cid, _cls, status, detail in checks:
            parts.append(
                "<tr><td><code>%s</code></td>"
                '<td><span class="badge %s">%s</span></td>'
                '<td class="detail">%s</td></tr>'
                % (e(cid), _BADGE_CLASS[status], e(_BADGE_TEXT[status]),
                   e(detail)))
        parts.append("</table>")
        parts.append("</section>")

    overall = worst_status(checks_by_txn) if txns else "warn"
    parts.append('<section class="overall">overall: '
                 '<span class="badge %s">%s</span></section>'
                 % (_BADGE_CLASS[overall], e(_BADGE_TEXT[overall])))
    parts.append("</main>")

    return ("<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>Aliro Lab — %s</title></head><body>%s</body></html>"
            % (e(name), "".join(parts)))


def main(argv):
    if len(argv) < 2 or len(argv) > 3 or argv[1] in ("-h", "--help"):
        sys.stderr.write(__doc__)
        return 2
    log_path = argv[1]
    html_path = argv[2] if len(argv) > 2 else log_path + ".html"
    try:
        with open(log_path, "r", errors="replace") as f:
            text = f.read()
    except OSError as exc:
        sys.stderr.write("aliro_lab: cannot read %s: %s\n" % (log_path, exc))
        return 2

    name = os.path.basename(log_path)
    txns = split_transactions(parse_events(text))
    checks_by_txn = [run_checks(t) for t in txns]

    sys.stdout.write(render_terminal(name, txns, checks_by_txn,
                                     sys.stdout.isatty()))
    with open(html_path, "w") as f:
        f.write(render_html(name, txns, checks_by_txn))
    sys.stdout.write("html report: %s\n" % html_path)

    return 1 if worst_status(checks_by_txn) == "fail" else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
