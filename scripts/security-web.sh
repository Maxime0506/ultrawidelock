#!/usr/bin/env bash
#
# security-web.sh — the browser half of the supply chain, which nothing else in this repo looks at.
#
# scripts/security.sh's `deps` gate reads tools/tui/bun.lock and integration/homeassistant's
# pyproject. Neither of those is what a user actually executes. web-flasher/index.html executes a
# module fetched at page load from a CDN, and that page's whole purpose is to write firmware to a
# board over WebSerial — so whoever controls that module controls what gets flashed onto every
# device of everyone who used the hosted flasher. It is not in any lockfile, so `deps` has never
# seen it; semgrep's p/javascript pack parses .js, not a <script> tag inside .html, so semgrep has
# never seen it either; and security-diff.sh's URL check only fires on a URL being ADDED, so a
# dependency that has been there since the page was written is invisible to all three.
#
# This gate closes that. It reads every tracked HTML page and asks three questions of it:
#
#   1. Is every remote subresource pinned to an exact version AND carrying an integrity hash?
#      A range like `@10` resolves to whatever the registry serves at page load. That is not a
#      dependency, it is a promise from a stranger, and `integrity=` is the only thing that makes
#      the difference observable to the browser.
#   2. Does the page carry a Content-Security-Policy?
#      GitHub Pages sends no CSP header and cannot be made to, so a <meta http-equiv> is the only
#      place one can exist for this project. Without it, an injected script has the same authority
#      as the page: on the flasher, that is navigator.serial.
#   3. Is the vendored JavaScript free of known-vulnerable versions? (retire.js)
#
#   scripts/security-web.sh              # every check
#   scripts/security-web.sh pins         # one check: pins csp retire
#   make security-web
#
# Exit 0 if everything selected passed, 1 otherwise, 2 on bad usage.
#
# Baseline, not suppression: security/web-baseline.txt lists paths that are knowingly
# non-compliant, one per line, each with a reason after a '#'. A baselined path still prints, it
# just does not block — so the debt is visible on every run rather than deleted. An entry that no
# longer matches anything is itself an error, because a stale baseline is how a check quietly
# stops applying to the file it was written for.
#
# Env:
#   WEB_BASELINE=path   override the baseline file
#   NO_COLOR=1          plain output
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

BASELINE="${WEB_BASELINE:-security/web-baseline.txt}"

if [[ -z "${NO_COLOR:-}" ]]; then
	BOLD=$'\033[1m' DIM=$'\033[2m' RED=$'\033[31m' GRN=$'\033[32m'
	YEL=$'\033[33m' RESET=$'\033[0m'
	CHK="✓" CRS="✗" WRN="!"
else
	BOLD="" DIM="" RED="" GRN="" YEL="" RESET=""
	CHK="+" CRS="x" WRN="!"
fi

have() { command -v "$1" >/dev/null 2>&1; }
hdr() { printf '\n%s── %s%s\n' "$BOLD" "$1" "$RESET"; }
missing() {
	printf '  %s%s%s %s is not installed — %s\n' "$RED" "$CRS" "$RESET" "$1" "$2"
	printf '      A gate that cannot run is a gate that did not pass. CI runs it regardless.\n'
	return 1
}

# ---- pins + csp ------------------------------------------------------------
# Both live in one python pass because both need the same parse of the same files, and the whole
# point of reading HTML with html.parser rather than with grep is that a regex over markup cannot
# tell a <script src> from the string "<script src" inside a JS template literal — and this repo
# has pages that contain both.
gate_pins_csp() {
	local want="$1" # pins | csp | both
	case "$want" in
	pins) hdr "web pins · third-party subresource integrity" ;;
	csp) hdr "web csp · content-security-policy" ;;
	*) hdr "web pins + csp" ;;
	esac

	local files
	files="$(git ls-files '*.html' | grep -vE '^(workspace|build|site|node_modules)/' || true)"
	if [ -z "$files" ]; then
		printf '  %s%s%s no tracked HTML to examine\n' "$YEL" "$WRN" "$RESET"
		return 0
	fi

	WEB_WANT="$want" WEB_FILES="$files" WEB_BASELINE_FILE="$BASELINE" python3 - <<'PY'
import html.parser, os, re, sys
from urllib.parse import urlparse

want = os.environ["WEB_WANT"]
files = [f for f in os.environ["WEB_FILES"].splitlines() if f.strip()]

# --- baseline -------------------------------------------------------------
baseline = {}
bpath = os.environ["WEB_BASELINE_FILE"]
try:
    with open(bpath) as fh:
        for ln in fh:
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            path, _, why = ln.partition("#")
            baseline[path.strip()] = why.strip() or "no reason recorded"
except FileNotFoundError:
    pass
used = set()

block, warn = [], []
# A baseline line is "<check>:<path>" or a bare "<path>" for every check. Scoping it per check
# matters: web-flasher/index.html has a CSP finding that is fine to defer and an unpinned-CDN
# finding that is not, and a path-only key would silence both together.
def B(path, what, why, check=None):
    for key in ((check + ":" + path) if check else None, path):
        if key and key in baseline:
            used.add(key)
            warn.append((path, "BASELINED: " + what, baseline[key]))
            return
    block.append((path, what, why))
def W(path, what, why):
    warn.append((path, what, why))

# An exact pin is a full semver, a 40-hex commit, or a content-addressed path. Anything with a
# bare major, a caret/tilde, "latest", or no version at all resolves differently tomorrow.
EXACT = re.compile(r"@(\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?|[0-9a-f]{40})(?:[/?#]|$)")
# The trailing alternative is [\^~], NOT [@^~]. With @ in that class every exactly pinned URL
# matched it -- "@10.4.0" contains "@1" -- so the verdict below (`not EXACT or FLOATING`) blocked
# correctly pinned subresources and there was no spelling of a URL that could pass. The @ forms
# that genuinely float (@latest, @next, @10) are already covered by the first alternative; this
# one exists only for caret and tilde ranges.
FLOATING = re.compile(r"@(latest|next|\d+(?:\.\d+)?)(?:[/?#]|$)|[\^~]\s*\d")

# A remote @import inside a <style> block is the same dependency as a <link rel=stylesheet>,
# except worse: the integrity attribute does not exist in CSS, so there is no form of it that can
# be verified at all. It is invisible to an HTML-tag scan, which is exactly why it is checked
# separately rather than folded into the tag walk above.
CSS_REMOTE = re.compile(r"""@import\s+(?:url\()?\s*['"]?(https?://[^'")\s]+)""", re.I)

class P(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.subres = []   # (tag, url, integrity, crossorigin)
        self.csp = None
        self._instyle = False
        self.css_imports = []
    def handle_endtag(self, tag):
        if tag == "style":
            self._instyle = False
    def handle_data(self, data):
        if self._instyle:
            self.css_imports += CSS_REMOTE.findall(data)
    def handle_starttag(self, tag, attrs):
        if tag == "style":
            self._instyle = True
        a = dict(attrs)
        if tag == "meta":
            if (a.get("http-equiv") or "").lower() == "content-security-policy":
                self.csp = a.get("content") or ""
        elif tag == "script" and a.get("src"):
            self.subres.append(("script", a["src"], a.get("integrity"), a.get("crossorigin")))
        elif tag == "link" and a.get("href"):
            rel = (a.get("rel") or "").lower()
            # Only rels the browser actually executes or styles with. A <link rel="canonical">
            # to another origin is a URL, not a subresource, and flagging it would train people
            # to stop reading this gate's output.
            if any(r in rel for r in ("stylesheet", "preload", "modulepreload")):
                self.subres.append(("link", a["href"], a.get("integrity"), a.get("crossorigin")))
        elif tag in ("iframe", "embed", "object") and (a.get("src") or a.get("data")):
            self.subres.append((tag, a.get("src") or a.get("data"), None, None))

nremote = 0
for f in files:
    try:
        src = open(f, encoding="utf-8", errors="replace").read()
    except OSError as e:
        block.append((f, "unreadable", str(e)))
        continue
    p = P()
    try:
        p.feed(src)
    except Exception as e:
        block.append((f, "unparseable HTML, so NOT checked", str(e)))
        continue

    if want in ("pins", "both"):
        for tag, url, integrity, cors in p.subres:
            u = urlparse(url)
            if not u.scheme and not url.startswith("//"):
                continue  # same-origin/relative: covered by the rest of the repo's gates
            nremote += 1
            host = u.netloc or "?"
            if u.scheme == "data":
                continue
            if not EXACT.search(url) or FLOATING.search(url):
                B(f, "unpinned third-party %s: %s" % (tag, url),
                  "The version resolves at page load, so what executes here is whatever %s "
                  "serves that day. Pin the exact version, or vendor the file into the repo and "
                  "serve it same-origin." % host, check="pins")
            elif not integrity:
                B(f, "third-party %s without integrity=: %s" % (tag, url),
                  "Pinned but unverified: a compromised or replaced artifact at the same URL "
                  "runs with the page's full authority. Add integrity= + crossorigin=anonymous, "
                  "or vendor it.", check="pins")
            else:
                W(f, "third-party %s, pinned and hashed: %s" % (tag, host),
                  "Acceptable, but still a remote origin in the load path. Vendoring removes the "
                  "dependency on that host being reachable and honest.")

        for url in p.css_imports:
            nremote += 1
            B(f, "remote CSS @import: %s" % url,
              "CSS has no integrity attribute, so this can never be verified — it is an "
              "unconditional trust of %s on every page load, and it blocks first paint on that "
              "host being up. Self-host the font or stylesheet."
              % (urlparse(url).netloc or "?"), check="pins")

    if want in ("csp", "both"):
        if p.csp is None:
            B(f, "no Content-Security-Policy",
              "GitHub Pages sends no CSP header, so a <meta http-equiv> is the only place one "
              "can exist for this site. Without it an injected script inherits every capability "
              "the page has.", check="csp")
        else:
            pol = p.csp.lower()
            if "unsafe-eval" in pol:
                B(f, "CSP allows unsafe-eval", "That removes most of what a CSP is for.", check="csp")
            if "default-src" not in pol and "script-src" not in pol:
                W(f, "CSP sets neither default-src nor script-src",
                  "The policy is present but does not constrain script execution.")

# Staleness is only meaningful for entries this invocation could have consumed. Running
# `security-web.sh csp` on its own must not declare every pins: entry dead, and vice versa.
running = {"pins", "csp"} if want == "both" else {want}
in_scope = {k for k in baseline
            if ":" not in k or k.split(":", 1)[0] in running}
stale = sorted(in_scope - used)
for s in stale:
    block.append((s, "stale baseline entry", "Nothing in the tree matches it any more. Remove "
                  "the line, or the check it silences has quietly stopped applying."))

print("  scope: %d HTML file(s), %d remote subresource(s), %d baselined path(s)"
      % (len(files), nremote, len(baseline)))
for path, what, why in block:
    print("  BLOCK %s" % path)
    print("        %s" % what)
    print("        %s" % why)
for path, what, why in warn:
    print("  warn  %s — %s" % (path, what))
print("  %d blocking, %d advisory" % (len(block), len(warn)))
sys.exit(1 if block else 0)
PY
}

# ---- retire ----------------------------------------------------------------
# Vendored JS only. retire.js is a version-fingerprint scanner, so it has nothing useful to say
# about a file this repo wrote itself (web-twin/twin.js is emscripten output); it earns its place
# the moment anything third-party gets vendored in, which is exactly what the `pins` check above
# will push people towards doing.
gate_retire() {
	hdr "web retire · known-vulnerable vendored JavaScript"
	have retire || {
		missing retire "npm i -g --ignore-scripts retire, or bunx retire"
		return 1
	}
	local n
	n="$(git ls-files '*.js' '*.mjs' | grep -vE '^(workspace|build|site|node_modules)/' | wc -l | tr -d ' ')"
	printf '  %sscope: %s tracked JavaScript file(s)%s\n' "$DIM" "$n" "$RESET"
	# --exitwith 1 so a finding fails. No --js flag: retire 5.x removed it (5.2.7 exits with
	# "unknown option '--js'"), and scanning is scoped by --path and --ext instead. The npm-tree
	# scanner is not wanted here anyway — there is no node_modules to walk, and osv-scanner
	# already reads bun.lock in the `deps` gate.
	retire --path . --ext js,mjs --outputformat text --exitwith 1 \
		--ignore workspace,build,site,node_modules,deps 2>&1 | sed 's/^/  /'
	return "${PIPESTATUS[0]}"
}

# ---- install -----------------------------------------------------------------
# Installing a package is an arbitrary-code-execution step. npm runs preinstall/postinstall from
# every package in the resolved tree by default, so `npm i` on a runner executes code from
# whoever published any transitive dependency. Bun blocks lifecycle scripts by default but keeps
# a built-in allow-list, so it is not the same guarantee; --ignore-scripts is.
#
# This is the gate `deps` cannot be: osv-scanner answers "is this package known bad today", which
# a package that is bad and not yet reported passes cleanly. Refusing to execute it does not
# depend on anyone having noticed.
#
# Two rules, both mechanical:
#   1. every install command in a tracked file carries --ignore-scripts
#   2. a lockfile-backed install also carries --frozen-lockfile, so a resolution cannot drift
#      out from under the lockfile that was reviewed
# and one more, on manifests: no floating version specifier, because "latest" resolves at install
# time to whatever was published since the review.
gate_install() {
	hdr "web install · package installs cannot execute code"
	git ls-files -z \
		'*.yml' '*.yaml' '*.sh' 'Makefile' '*/Makefile' '*.md' '*.json' \
		| INSTALL_ROOT="$ROOT" python3 -c '
import json, os, re, sys

files = [f for f in sys.stdin.buffer.read().decode("utf-8", "replace").split("\0") if f]

# The command, not the word: `npm install` inside prose is not an install step, but the same
# string in a run: block is. Anchoring on the manager plus its install verb is what separates
# them, and every hit is printed with its file:line so a false positive is one look away.
CMD = re.compile(r"\b(npm|pnpm|yarn|bun)\s+(install|ci|add|i)\b(?P<rest>[^\n|&;]*)")


def is_prose(line, start):
    """An install command inside a backtick span is being talked about, not run.

    Every executable form in this tree -- a Makefile recipe, a `run:` block, a line in a .sh --
    is bare. Prose in a YAML message:, a comment or a markdown paragraph writes it as `npm i`.
    Matching on that distinction beats an allow-list of filenames, which would have to grow
    every time someone documents the rule this gate enforces.
    """
    return line.count("`", 0, start) % 2 == 1


bad, seen, manifests = [], 0, 0
for f in files:
    try:
        text = open(f, encoding="utf-8", errors="replace").read()
    except OSError:
        continue
    for n, line in enumerate(text.splitlines(), 1):
        for m in CMD.finditer(line):
            if is_prose(line, m.start()):
                continue
            seen += 1
            rest, mgr = m.group("rest"), m.group(1)
            if "--ignore-scripts" not in rest:
                bad.append((f, n, "no --ignore-scripts", line.strip()[:96]))
            # -g installs resolve globally and have no lockfile to freeze against.
            elif "-g" not in rest.split() and "--global" not in rest \
                    and "--frozen-lockfile" not in rest and mgr != "npm":
                bad.append((f, n, "no --frozen-lockfile", line.strip()[:96]))

FLOAT = re.compile(r"^(latest|\*|)$|^[\^~>=<]")
for f in files:
    if os.path.basename(f) != "package.json":
        continue
    try:
        doc = json.load(open(f, encoding="utf-8"))
    except (OSError, ValueError):
        continue
    manifests += 1
    # devDependencies and overrides are deliberately not checked for ranges: a caret there is
    # normal and the lockfile pins it. "latest" and "*" are checked everywhere, because they
    # resolve to whatever exists at install time and no review covers that.
    for section in ("dependencies", "devDependencies", "overrides", "optionalDependencies"):
        for name, spec in (doc.get(section) or {}).items():
            if not isinstance(spec, str):
                continue
            if spec.strip() in ("latest", "*", ""):
                bad.append((f, 0, "floating specifier", "%s: %s = %s" % (section, name, spec)))
    # A package bun is told it MAY run scripts for, which is the hole --ignore-scripts closes.
    if doc.get("trustedDependencies"):
        bad.append((f, 0, "trustedDependencies", ", ".join(doc["trustedDependencies"])))

print("  scope: %d install command(s), %d package manifest(s)" % (seen, manifests))
for f, n, why, ctx in bad:
    where = "%s:%d" % (f, n) if n else f
    print("  BLOCK %s — %s" % (where, why))
    print("        %s" % ctx)
print("  %d blocking, 0 advisory" % len(bad))
sys.exit(1 if bad else 0)'
	return "${PIPESTATUS[1]}"
}

# ---- dispatch --------------------------------------------------------------
run_one() {
	case "$1" in
	pins) gate_pins_csp pins ;;
	csp) gate_pins_csp csp ;;
	retire) gate_retire ;;
	install) gate_install ;;
	*)
		echo "security-web.sh: unknown check '$1' (pins csp retire install)" >&2
		return 2
		;;
	esac
}

CHECKS=("$@")
[ ${#CHECKS[@]} -gt 0 ] || CHECKS=(pins csp retire install)

failed=()
for c in "${CHECKS[@]}"; do
	run_one "$c" || failed+=("$c")
done

printf '\n'
if [ ${#failed[@]} -gt 0 ]; then
	printf '%s%s web: %s%s\n\n' "$RED" "$CRS" "${failed[*]}" "$RESET"
	exit 1
fi
printf '%s%s web: %s%s\n\n' "$GRN" "$CHK" "${CHECKS[*]}" "$RESET"
