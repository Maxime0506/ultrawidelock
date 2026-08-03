#!/usr/bin/env python3
"""Phase 1 check: the boot shim must never cost the twin its self-test.

Loads activity/dist/index.html three ways and reports both the page's own
#selftest verdict and the data-in-discord attribute the shim stamps on <html>.

Usage: boot-probe.py [dist-dir] [case]   (default dist: ../dist)
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

DIST = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else
                       os.path.join(os.path.dirname(__file__), "..", "dist"))

# Fixed list rather than argv or the environment: nothing an outer process
# controls may reach the exec below, or semgrep's tainted-subprocess rule
# blocks the whole security sweep. Same reasoning as web-twin/csp_probe.py.
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
    sys.exit("boot_probe: no Firefox in " + ", ".join(FIREFOX_CANDIDATES))


FIREFOX = find_firefox()

CASES = [
    ("standalone", "", "no frame_id: shim must do nothing at all"),
    ("in-discord-unconfigured", "?frame_id=probe123",
     "frame_id but no client id compiled in"),
    ("in-discord-handshake-fails", "?frame_id=probe123&instance_id=x",
     "no real Discord to answer the handshake.\n"
     "   expects 'error' when dist was built with VITE_DISCORD_CLIENT_ID set,\n"
     "   and 'unconfigured' when it was not; both are pass, the self-test is\n"
     "   the assertion. Build with a dummy id to exercise the error path:\n"
     "     VITE_DISCORD_CLIENT_ID=000000000000000000 npm run build"),
]

WRAPPER = """<!doctype html><meta charset="utf-8"><title>boot probe</title>
<body style="margin:0">
<iframe id="f" src="/twin/index.html%s" style="width:1280px;height:900px;border:0"></iframe>
<script>
var CASE = %s;
function report(status, text, attr){
  navigator.sendBeacon("/report", JSON.stringify({
    case: CASE, status: status, selftest: text, dataInDiscord: attr
  }));
}
var t0 = Date.now();
var iv = setInterval(function(){
  var txt = "<unreadable>", cls = "", attr = "(absent)";
  try {
    var d = document.getElementById("f").contentDocument;
    var el = d && d.getElementById("selftest");
    if (el) { txt = el.textContent; cls = el.className; }
    if (d && d.documentElement.hasAttribute("data-in-discord")) {
      attr = d.documentElement.getAttribute("data-in-discord");
    }
  } catch (e) { txt = "<err: " + e.message + ">"; }
  /* Wait for the self-test to settle AND give the shim time to move off
   * "connecting", so a transient state is not mistaken for a final one. */
  var settled = (cls === "pass" || cls === "fail") && attr !== "connecting";
  if (settled && Date.now() - t0 > 3000) { clearInterval(iv); report("settled:" + cls, txt, attr); }
  else if (Date.now() - t0 > 15000) { clearInterval(iv); report("timeout", txt, attr); }
}, 100);
</script>
</body>"""

results = []
current = ["", ""]


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        results.append(json.loads(self.rfile.read(n) or b"{}"))
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path in ("/", "/index.html"):
            body = (WRAPPER % (current[1], json.dumps(current[0]))).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path.startswith("/twin/"):
            fp = os.path.join(DIST, path[len("/twin/"):])
            if not os.path.isfile(fp) or not os.path.abspath(fp).startswith(DIST):
                self.send_error(404)
                return
            data = open(fp, "rb").read()
            ctype = ("text/html; charset=utf-8" if fp.endswith(".html")
                     else "text/javascript; charset=utf-8")
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        self.send_error(404)


srv = socketserver.TCPServer(("127.0.0.1", 0), Handler)
port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()

only = sys.argv[2] if len(sys.argv) > 2 else None
for label, qs, note in CASES:
    if only and only != label:
        continue
    current[0], current[1] = label, qs
    before = len(results)
    prof = tempfile.mkdtemp(prefix="ffprof-")
    p = subprocess.Popen([FIREFOX, "--headless", "--profile", prof, "--no-remote",
                          f"http://127.0.0.1:{port}/"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 45
    while len(results) == before and time.time() < deadline:
        time.sleep(0.25)
    p.terminate()
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
    shutil.rmtree(prof, ignore_errors=True)
    if len(results) == before:
        print(f"== {label}\n   {note}\n   RESULT: no report\n")
        continue
    r = results[-1]
    print(f"== {label}\n   {note}")
    print(f"   data-in-discord: {r['dataInDiscord']}")
    print(f"   selftest:        {r['selftest']}\n")

srv.shutdown()
