#!/usr/bin/env python3
"""Two Activity checklist items that only mean anything inside an iframe.

1. single-step mode advances the real RX state machine leg by leg
2. the theme toggle still works when localStorage is unavailable

Both matter because a Discord Activity is a sandboxed iframe: storage can be
refused there, and single-step is the mode people actually use to explain a
DS-TWR round to someone else.

Both are driven in a same-origin iframe so the twin's own DOM can be read back.
Storage is disabled through a Firefox profile pref rather than by faking it, so
the page hits the real exception path its try/catch was written for.

Usage: iframe-checks.py [dist-dir]   (default: ../dist)
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

FIREFOX_CANDIDATES = (
    "/Applications/Firefox.app/Contents/MacOS/firefox",
    "/usr/bin/firefox",
)
FIREFOX = next((p for p in FIREFOX_CANDIDATES
                if os.path.isfile(p) and os.access(p, os.X_OK)), None)
if not FIREFOX:
    sys.exit("no firefox")

WRAPPER = """<!doctype html><meta charset="utf-8"><title>checks</title>
<body style="margin:0">
<iframe id="f" src="/twin/index.html" style="width:1280px;height:900px;border:0"></iframe>
<script>
var out = [], t0 = Date.now();
function ok(n, c, d){ out.push({name:n, pass:!!c, detail:String(d===undefined?"":d)}); }

function go(){
  var d = document.getElementById("f").contentDocument;
  var w = document.getElementById("f").contentWindow;
  var st = d.getElementById("selftest");
  if (!st || !(st.className === "pass" || st.className === "fail")) {
    if (Date.now() - t0 > 20000) { ok("boot", false, "never settled"); return done(); }
    return setTimeout(go, 200);
  }

  ok("selftest", st.className === "pass", st.textContent);

  /* --- storage availability, as the page actually sees it --- */
  var storageBlocked = false;
  try { w.localStorage.setItem("oa-probe", "1"); w.localStorage.removeItem("oa-probe"); }
  catch (e) { storageBlocked = true; }
  ok("storage.blocked", storageBlocked, storageBlocked ? "localStorage throws" : "storage available");

  /* --- theme toggle still flips with storage unavailable --- */
  var root = d.documentElement;
  var before = root.dataset.theme || "(unset)";
  var btn = d.getElementById("themeBtn");
  ok("theme.button", !!btn);
  if (btn) {
    btn.click();
    var after = root.dataset.theme || "(unset)";
    ok("theme.toggles", after !== before, before + " -> " + after);
    var mid = root.dataset.theme;
    btn.click();
    ok("theme.toggles.again", root.dataset.theme !== mid, mid + " -> " + root.dataset.theme);
  }

  /* --- single-step mode, leg by leg --- */
  var pause = d.getElementById("b-pause");
  var stepLeg = d.getElementById("b-stepleg");
  var legs = d.getElementById("legs");
  ok("step.controls", !!pause && !!stepLeg && !!legs);

  if (!(pause && stepLeg && legs)) { return done(); }

  /* stepLeg() refuses without an active UWB session (index.html:880): the phone
   * starts at 9 m, outside BLE range. Walk up first, wait for the session to
   * come up and a block to complete, and only then freeze and step. */
  d.getElementById("b-walk").click();
  /* blockNo is already non-zero at boot because the footer self-test drives
   * blocks through the firmware, so it says nothing about the live session.
   * Wait for the FIRST_RANGE phase chip to light: that only happens once the
   * Aliro handshake has completed and real ranging is under way. */
  var waited = 0;
  function litPhase(name){
    var all = d.querySelectorAll('.phase');
    for (var k = 0; k < all.length; k++) {
      if (all[k].textContent.trim() === name) return all[k].classList.contains('lit');
    }
    return false;
  }
  (function waitForSession(){
    if (litPhase('FIRST_RANGE') || waited > 30000) {
      ok("session.up", litPhase('FIRST_RANGE'),
         "ble=" + d.getElementById("v-ble").textContent.trim()
         + " first_range=" + litPhase('FIRST_RANGE'));
      return stepping();
    }
    waited += 200;
    setTimeout(waitForSession, 200);
  })();
  return;

  function stepping(){
  pause.click();
  var seen = [], i = 0;
  (function stepOnce(){
    if (i++ >= 6) {
      var distinct = seen.filter(function (v, k, a) { return a.indexOf(v) === k; });
      ok("step.advances", distinct.length > 1, seen.join(" | ").slice(0, 160));
      ok("step.cycles.all.legs", distinct.length >= 4, distinct.join(",").slice(0, 120));
      ok("step.selftest.intact", d.getElementById("selftest").className === "pass");
      return done();
    }
    stepLeg.click();
    setTimeout(function(){
      var active = legs.querySelector('.leg.next');
      seen.push(active ? active.textContent.trim() : '(none)');
      if (i === 1) {
        var lg = d.getElementById("log");
        ok("step.log", true, "log tail: " + lg.textContent.trim().slice(-200));
        ok("paused.state", d.getElementById("b-pause").getAttribute("aria-pressed"),
           "aria-pressed=" + d.getElementById("b-pause").getAttribute("aria-pressed"));
      }
      stepOnce();
    }, 160);
  })();
  }
}
function done(){ navigator.sendBeacon("/report", JSON.stringify(out)); }
setTimeout(go, 400);
</script>
</body>"""

results = []


class H(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        results.extend(json.loads(self.rfile.read(n) or b"[]"))
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        p = urllib.parse.urlparse(self.path).path
        if p in ("/", "/index.html"):
            b = WRAPPER.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
            return
        if p.startswith("/twin/"):
            fp = os.path.join(DIST, p[len("/twin/"):])
            if not os.path.isfile(fp):
                self.send_error(404)
                return
            data = open(fp, "rb").read()
            self.send_response(200)
            self.send_header("Content-Type",
                             "text/html; charset=utf-8" if fp.endswith(".html")
                             else "text/javascript; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        self.send_error(404)


srv = socketserver.TCPServer(("127.0.0.1", 0), H)
port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()

prof = tempfile.mkdtemp(prefix="ffprof-")
# Kill DOM storage entirely: this is the condition the page's try/catch exists for.
with open(os.path.join(prof, "user.js"), "w") as f:
    f.write('user_pref("dom.storage.enabled", false);\n')

p = subprocess.Popen([FIREFOX, "--headless", "--profile", prof, "--no-remote",
                      f"http://127.0.0.1:{port}/"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
deadline = time.time() + 60
while not results and time.time() < deadline:
    time.sleep(0.25)
p.terminate()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()
shutil.rmtree(prof, ignore_errors=True)
srv.shutdown()

if not results:
    print("no report")
    sys.exit(1)
fails = [r for r in results if not r["pass"]]
for r in results:
    print(f"  {'ok  ' if r['pass'] else 'FAIL'} {r['name']}"
          + (f"  {r['detail']}" if r["detail"] else ""))
print()
print(f"{len(results) - len(fails)}/{len(results)} pass" if not fails
      else f"{len(fails)}/{len(results)} FAILED")
sys.exit(1 if fails else 0)
