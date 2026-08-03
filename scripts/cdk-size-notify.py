#!/usr/bin/env python3
"""cdk-size-notify.py — say what a change cost the CDK image, in Discord.

    DISCORD_WEBHOOK=https://discord.com/api/webhooks/... \
      scripts/cdk-size-notify.py --current size-report.json --run-url https://...

Formats a size report against the recorded baseline and posts it. Exit 0 when
it posted AND when it deliberately said nothing; exit 1 only when a post was
attempted and failed. Nothing here decides whether code merges: cdk-size-check
is the gate, this only reports what it decided.

SILENT ON A NO-OP, and that is the whole design. A bot that posts "+0 bytes" on
every push to every labelled pull request gets muted within a week, and a muted
bot is worse than none -- it is a channel everyone believes is being watched.
So a run that passed with both regions unchanged says nothing at all. Anything
that moved, was blocked, or could not be compared is worth an interruption.
Raise the bar with CDK_SIZE_NOTIFY_MIN if even that is too chatty.

THE REPORT IS UNTRUSTED INPUT. On a pull request from a fork, every byte of it
was produced by that fork's code -- including the symbol names, which end up in
a message this bot posts into your server. So nothing from it is interpolated
into a shell command, strings are stripped of markdown and length-capped, and
the payload disables mentions outright: a symbol named `@everyone` is a real
thing someone can write. The numbers themselves cannot be trusted either, and
are not meant to be; a fork can lie about its own size. What blocks a merge is
the gate's exit status, not this message.
"""

import argparse
import importlib.util
import json
import os
import re
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))

# The comparator's own logic, imported rather than re-implemented, so the bot
# cannot drift into reporting a different verdict from the one the gate reached.
# It must be loaded from THIS checkout -- on a workflow_run the checkout is the
# base repository, not the pull request, which is what makes this trustworthy
# code reading untrustworthy data.
_spec = importlib.util.spec_from_file_location(
    "cdk_size_compare", os.path.join(HERE, "cdk-size-compare.py")
)
_cmp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_cmp)

GREEN = 0x2ECC71
RED = 0xE74C3C
AMBER = 0xF1C40F
GREY = 0x95A5A6

# Discord embeds cap at 4096 characters of description and 25 fields; these are
# well under, because the interesting content is six numbers.
MAX_FIELD = 900
MAX_NAME = 60


def clean(text, limit=MAX_NAME):
    """Make untrusted PROSE safe to put in a Discord message.

    A blacklist, because prose legitimately contains punctuation and only the
    markdown-active characters have to go. Used for the gate's own messages,
    which are generated here but interpolate numbers out of the report -- a
    crafted report can put a string where an integer belongs.
    """
    text = str(text)
    text = re.sub(r"[`\\*_~|<>@#]", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) > limit:
        text = text[: limit - 1] + "…"
    return text or "(unnamed)"


# A WHITELIST for anything rendered inside a code span, and the blacklist above
# is wrong for these: it strips `_`, which is in nearly every C symbol, so
# cdk_size_gate_probe came out as "cdksizegateprobe" -- unsearchable, and the
# top-mover list is only useful if you can paste the name into a grep.
#
# Inside backticks the markdown characters are inert anyway, so the only real
# requirement is that the content cannot END the code span or introduce a
# mention. Allowing exactly the characters a C identifier or an overlay key can
# contain satisfies that by construction, rather than by enumerating attacks.
_CODE_SAFE = re.compile(r"[^A-Za-z0-9_.:+-]")


def clean_code(text, limit=MAX_NAME):
    """Make an untrusted identifier safe INSIDE a code span, keeping it readable."""
    text = _CODE_SAFE.sub("", str(text))
    if len(text) > limit:
        text = text[: limit - 1] + "…"
    return text or "(unnamed)"


def num(n):
    return f"{n:,}" if isinstance(n, int) else str(n)


def signed(n):
    return f"{n:+,}" if isinstance(n, int) else str(n)


def build(cur, doc, run_url, min_delta):
    """Decide whether to say anything, and what. Returns (payload, why-silent)."""
    recorded = _cmp.baselines_of(doc)
    key = _cmp.config_key(cur.get("config"))
    base = recorded.get(key)

    commit = clean((cur.get("commit") or "unknown")[:12], 12)

    if base is None:
        return {
            "color": AMBER,
            "title": "CDK size · no baseline for this configuration",
            "description": (
                f"Built `{clean_code(key)}`, which has no recorded baseline, so nothing was "
                f"compared.\nRecorded: {clean(', '.join(sorted(recorded)) or 'none', 200)}"
            ),
        }, None

    cfg_diff = _cmp.config_diff(base, cur)
    if cfg_diff:
        lines = [
            f"`{clean_code(f, 40)}`: {clean(b, 40)} → {clean(c, 40)}"
            for f, b, c in cfg_diff[:6]
        ]
        return {
            "color": AMBER,
            "title": "CDK size · not comparable",
            "description": (
                "These two images were not built the same way, so no delta is "
                "reported.\n\n" + "\n".join(lines)
            )[:MAX_FIELD],
        }, None

    gate = base.get("gate", {})
    rows, fails = [], []
    for name in ("RAM", "FLASH"):
        row, f = _cmp.gate_region(
            name, base, cur,
            gate.get(f"{name.lower()}_free_floor"),
            gate.get(f"{name.lower()}_delta_cap"),
            False,
        )
        if row:
            rows.append(row)
        fails.extend(f)

    moved = max((abs(r["delta"]) for r in rows), default=0)
    # The silence rule. Blocked runs always speak; an unchanged, passing run
    # never does.
    if not fails and moved < min_delta:
        return None, f"passed with every region within {min_delta} B of the baseline"

    fields = [
        {
            "name": r["region"],
            "value": (
                f"**{signed(r['delta'])} B**\n"
                f"{num(r['used'])} / {num(r['size'])} used\n"
                f"**{num(r['free'])} B free** ({r['pct']}%)"
            ),
            "inline": True,
        }
        for r in rows
    ]

    top = _cmp.movers(base, cur, 3)
    if top:
        fields.append({
            "name": "Top movers (indicative under LTO)",
            "value": "\n".join(
                f"`{clean_code(n)}` {signed(d)} B" for d, n, _b, _a in top
            )[:MAX_FIELD],
            "inline": False,
        })

    if fails:
        title = "CDK size · blocked"
        colour = RED
        desc = "\n".join(f"• {clean(f, 300)}" for f in fails[:4])
    else:
        title = "CDK size · within budget"
        colour = GREEN
        desc = "Headroom is above the floor and growth is under the cap."

    return {
        "color": colour,
        "title": title,
        "url": run_url or None,
        "description": desc[:MAX_FIELD],
        "fields": fields,
        "footer": {"text": f"{clean_code(key)} · {commit}"},
    }, None


# Discord serves three endpoints off one webhook: the base URL takes its own
# JSON, and `/github` and `/slack` take those providers' payload shapes instead.
# A repository that already forwards pull-request events to Discord has the
# `/github` form configured in Settings -> Webhooks, and that is the URL somebody
# copies when asked for "the Discord webhook". Posting a Discord embed to it gets
# a 400 and a message that reads like the embed is malformed, which is a long way
# from the actual cause. Normalising here costs one line and removes the whole
# failure mode: either form works.
_SUFFIXES = ("/github", "/slack")


def normalise_webhook(url):
    url = url.strip().rstrip("/")
    for suffix in _SUFFIXES:
        if url.lower().endswith(suffix):
            return url[: -len(suffix)]
    return url


def post(webhook, embed, timeout):
    payload = {
        "embeds": [embed],
        # Belt and braces with clean(): a symbol name is attacker-controlled on
        # a fork pull request, and "@everyone" in an embed is a real ping unless
        # this is set.
        "allowed_mentions": {"parse": []},
    }
    req = urllib.request.Request(
        webhook,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json", "User-Agent": "openaliro-cdk-size"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status


def main():
    root = os.path.dirname(HERE)
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--current", required=True, help="size-report.json from the run")
    ap.add_argument("--baseline", default=os.path.join(root, "firmware", "size-baseline.json"))
    ap.add_argument("--run-url", default=os.environ.get("CDK_SIZE_RUN_URL", ""))
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument(
        "--min-delta", type=int,
        default=int(os.environ.get("CDK_SIZE_NOTIFY_MIN", "1")),
        help="stay silent on a passing run whose regions all moved less than this",
    )
    ap.add_argument("--dry-run", action="store_true", help="print the payload, post nothing")
    args = ap.parse_args()

    webhook = normalise_webhook(os.environ.get("DISCORD_WEBHOOK", ""))
    if not webhook and not args.dry_run:
        # Not a failure. A repository without the secret configured -- including
        # every fork -- should not have a red X on it for declining to run a
        # notifier it never asked for.
        sys.stderr.write(
            "\n  DISCORD_WEBHOOK is not set, so nothing was posted.\n"
            "      This is not an error: the gate is cdk-size-check, and this only\n"
            "      reports what it decided. Add the secret to turn the bot on.\n\n"
        )
        return 0

    for path, what in ((args.current, "report"), (args.baseline, "baseline")):
        if not os.path.isfile(path):
            sys.stderr.write(f"\n  no {what} at {path}; nothing to say\n\n")
            return 0

    with open(args.current, "r", errors="replace") as fh:
        cur = json.load(fh)
    with open(args.baseline, "r", errors="replace") as fh:
        doc = json.load(fh)

    embed, silent = build(cur, doc, args.run_url, args.min_delta)
    if embed is None:
        sys.stderr.write(f"\n  saying nothing: {silent}\n\n")
        return 0

    if args.dry_run:
        json.dump({"embeds": [embed], "allowed_mentions": {"parse": []}},
                  sys.stdout, indent=2)
        sys.stdout.write("\n")
        return 0

    try:
        status = post(webhook, embed, args.timeout)
    except (urllib.error.URLError, OSError) as exc:
        # The URL is a secret; printing the exception could echo it back into a
        # public log, so only the class and reason are reported.
        sys.stderr.write(f"\n  posting to Discord failed: {type(exc).__name__}\n\n")
        return 1
    sys.stderr.write(f"\n  posted  ·  HTTP {status}\n\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
