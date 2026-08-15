#!/usr/bin/env python3
"""Phase 0 spike, local half.

Serves web-twin/ with a chosen Content-Security-Policy header and loads it
inside an iframe in headless Firefox, the same shape Discord uses for an
Activity. The wrapper (served with no CSP of its own) reads #selftest out of
the frame and POSTs it back here, so the result lands in stdout rather than in
a screenshot we have to squint at.

Usage: csp_probe.py [dir-to-serve]      (default: the directory holding this file)
"""
import http.server
import json
import os
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

TWIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(__file__))

# The browser is chosen from this fixed list rather than taken from argv or the
# environment. Nothing an outer process controls reaches the exec below, which
# is what keeps `make security GATES="semgrep"` green: its
# dangerous-subprocess-use-tainted-env-args rule blocks the whole sweep, and
# this repo suppresses no semgrep findings anywhere.
#
# Firefox and not Chrome because this tree has no Chrome; see the headless
# workflow the docs build already relies on.
FIREFOX_CANDIDATES = (
    "/Applications/Firefox.app/Contents/MacOS/firefox",
    "/usr/bin/firefox",
    "/usr/local/bin/firefox",
    "/snap/bin/firefox",
)


def find_firefox():
    for path in FIREFOX_CANDIDATES:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path
    sys.exit(
        "csp_probe: no Firefox found. Looked in:\n  "
        + "\n  ".join(FIREFOX_CANDIDATES)
        + "\nAdd the path to FIREFOX_CANDIDATES rather than passing it in: an\n"
        "argv-supplied binary trips semgrep's tainted-subprocess rule."
    )


FIREFOX = find_firefox()

# Each case: (label, CSP header value or None, note)
CASES = [
    ("baseline-no-csp", None,
     "sanity: proves the harness itself works"),
    ("no-wasm-unsafe-eval",
     "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'",
     "CSP present, wasm-unsafe-eval absent"),
    ("with-wasm-unsafe-eval",
     "default-src 'self'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'",
     "CSP present, wasm-unsafe-eval granted"),
    ("no-unsafe-inline",
     "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'",
     "wasm allowed but index.html's inline <script> blocked"),
]

WRAPPER = """<!doctype html><meta charset="utf-8"><title>probe</title>
<body style="margin:0">
<iframe id="f" src="/twin/index.html" style="width:1280px;height:900px;border:0"
        sandbox="allow-scripts allow-same-origin allow-forms allow-popups"></iframe>
<script>
var CASE = %s;
var errors = [], violations = [], hooked = false;
window.addEventListener("error", function(e){ errors.push(String(e.message)); }, true);

/* Attach to the framed document as early as it exists so CSP violations
 * raised while twin.js is still parsing are not missed. */
function hook(d){
  if (hooked || !d) return;
  hooked = true;
  d.addEventListener("securitypolicyviolation", function(e){
    violations.push(e.violatedDirective + " <- " + (e.blockedURI || "(inline/eval)"));
  });
  if (d.defaultView) {
    d.defaultView.addEventListener("error", function(e){
      errors.push("frame: " + String(e.message));
    }, true);
  }
}
function report(status, text){
  navigator.sendBeacon("/report", JSON.stringify({
    case: CASE, status: status, selftest: text,
    errors: errors, violations: violations
  }));
}
var t0 = Date.now();
var iv = setInterval(function(){
  var txt = "<unreadable>", cls = "";
  try {
    var d = document.getElementById("f").contentDocument;
    hook(d);
    var el = d && d.getElementById("selftest");
    if (el) { txt = el.textContent; cls = el.className; }
    else { txt = "<no #selftest>"; }
  } catch (e) { txt = "<cross-origin: " + e.message + ">"; }
  /* The page stamps #selftest with class pass/fail when the run completes;
   * that is the page's own settled signal, not a guess at its wording. */
  if (cls === "pass" || cls === "fail") { clearInterval(iv); report("settled:" + cls, txt); }
  else if (Date.now() - t0 > 12000) { clearInterval(iv); report("timeout", txt); }
}, 50);
</script>
</body>"""

results = []
current_csp = [None]
current_case = [""]


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_POST(self):
        if self.path == "/report":
            n = int(self.headers.get("Content-Length", 0))
            results.append(json.loads(self.rfile.read(n) or b"{}"))
            self.send_response(204)
            self.end_headers()
        else:
            self.send_error(404)

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path in ("/", "/index.html"):
            body = (WRAPPER % json.dumps(current_case[0])).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path.startswith("/twin/"):
            name = path[len("/twin/"):]
            fp = os.path.join(TWIN, name)
            if not os.path.isfile(fp) or not os.path.abspath(fp).startswith(TWIN):
                self.send_error(404)
                return
            data = open(fp, "rb").read()
            ctype = ("text/html; charset=utf-8" if name.endswith(".html")
                     else "text/javascript; charset=utf-8")
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            if current_csp[0]:
                self.send_header("Content-Security-Policy", current_csp[0])
            self.end_headers()
            self.wfile.write(data)
            return
        self.send_error(404)


srv = socketserver.TCPServer(("127.0.0.1", 0), Handler)
srv.allow_reuse_address = True
port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()

print(f"probe server on 127.0.0.1:{port}\n")
for label, csp, note in CASES:
    current_csp[0] = csp
    current_case[0] = label
    before = len(results)
    prof = tempfile.mkdtemp(prefix="ffprof-")
    p = subprocess.Popen(
        [FIREFOX, "--headless", "--profile", prof, "--no-remote",
         f"http://127.0.0.1:{port}/"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 40
    while len(results) == before and time.time() < deadline:
        time.sleep(0.25)
    p.terminate()
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
    shutil.rmtree(prof, ignore_errors=True)

    if len(results) == before:
        print(f"== {label}\n   {note}\n   RESULT: no report (browser never phoned home)\n")
        continue
    r = results[-1]
    print(f"== {label}")
    print(f"   {note}")
    print(f"   CSP: {csp or '(none)'}")
    print(f"   status:   {r['status']}")
    print(f"   selftest: {r['selftest']}")
    if r.get("violations"):
        for v in r["violations"]:
            print(f"   VIOLATION: {v}")
    if r.get("errors"):
        print(f"   errors:   {r['errors']}")
    print()

srv.shutdown()
print(json.dumps(results, indent=2))
