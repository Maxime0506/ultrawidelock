#!/usr/bin/env python3
"""One-terminal HITL: RTT SIDE peer= → ADDR on witnesses → WR1 → SF1 back to the lock.

Owns the three witness ACM ports (no port fights). Holds the DWM3001CDK probe
through JLinkExe and talks to it over SEGGER's bidirectional RTT server, so the
same connection reads the lock's console AND writes the SF1 windows its
woz_side gate needs. Pushes ADDR when Aliro CoC opens, prints WR1 + optional
JSONL.

A SIDE=1 lock never passive-unlocks until SF1 arrives — that is the fail-closed
design, not a fault. Run this tool, or tap.

Probe and witnesses on different machines (lock on a laptop, dongles on a Pi):

  laptop: ssh -R 19021:127.0.0.1:19021 <pi>   # after JLinkExe holds the probe
  pi:     python3 side_hitl.py --rtt-attach ...

Do NOT type while walking. Prefer fixed --pose for static stands, or --choreo
for a timed auto-labelled walk you memorize before starting.

Static (recommended):

  # stand at OUTSIDE stick for the whole ADDR hold after unlock
  python3 side_hitl.py --pose outside_pause --label outside_static \\
      --out ~/captures/pose_out.jsonl --addr-hold-s 45

  # stand at INSIDE stick for the whole ADDR hold
  python3 side_hitl.py --pose inside_pause --label inside_static \\
      --out ~/captures/pose_in.jsonl --addr-hold-s 45

Timed walk (no typing — follow the printed countdown):

  python3 side_hitl.py --choreo --out ~/captures/choreo.jsonl --label choreo

Optional --keys enables o/i/n typing (second person at the Pi only).

Ctrl-C stops (second Ctrl-C forces exit). Requires: pyserial, JLinkExe.
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

SIDE_PEER = re.compile(
    r"SIDE peer=([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})\s+type=\w+",
    re.I,
)
SIDE_CLEAR = re.compile(r"SIDE peer=clear", re.I)
ADDR_OK = re.compile(r"ADDR ok", re.I)
WR1_RE = re.compile(
    r"WR1 role=(?P<role>inside|outside|threshold) obs=(?P<obs>\d+) "
    r"filt=(?P<filt>[0-9a-fA-F]+) "
    r"(?:addr=(?P<addr>[0-9a-fA-F]{12}) )?"
    r"n=(?P<n>\d+) mean=(?P<mean>-?\d+) "
    r"min=(?P<min>-?\d+) max=(?P<max>-?\d+) var=(?P<var>-?\d+)"
)
WINDOW_S = 3.0
CDK_CHIP = "NRF52833_XXAA"
# SEGGER's RTT telnet server. Unlike JLinkRTTLogger (up-buffer only) this is
# bidirectional: what we send lands in down-buffer 0, which is where the lock's
# side_feed_rtt_poll() reads SF1 from.
RTT_TELNET_PORT = 19021


def discover_ports() -> list[Path]:
    # macOS names the same CDC ACM dongle /dev/cu.usbmodem*, so a Linux-only
    # glob silently finds nothing when the rig moves to a laptop.
    pats = ("ttyACM*", "ttyUSB*", "cu.usbmodem*")
    found: list[Path] = []
    for pat in pats:
        found += sorted(Path("/dev").glob(pat))
    return found


def probe_witness_ports(candidates: list[Path], baud: int, seconds: float = 4.0) -> list[Path]:
    """Keep only ACM ports that emit WR1 (skip J-Link VCOM / CDK)."""
    import serial

    opened: list[tuple[Path, object]] = []
    bufs: dict[str, str] = {}
    for p in candidates:
        try:
            ser = serial.Serial(str(p), baud, timeout=0.15)
            opened.append((p, ser))
            bufs[str(p)] = ""
        except Exception as e:
            print(f"! skip {p}: {e}", file=sys.stderr, flush=True)

    hit: set[str] = set()
    deadline = time.time() + seconds
    print(f"probing {len(opened)} port(s) for WR1 ({seconds:.0f}s)…", flush=True)
    while time.time() < deadline and len(hit) < 3:
        for p, ser in opened:
            key = str(p)
            if key in hit:
                continue
            try:
                chunk = ser.read(256).decode("utf-8", errors="replace")
            except Exception:
                continue
            if not chunk:
                continue
            bufs[key] += chunk
            if WR1_RE.search(bufs[key]):
                hit.add(key)
                print(f"  witness {p}", flush=True)
        time.sleep(0.05)

    for _, ser in opened:
        try:
            ser.close()
        except Exception:
            pass

    return [Path(p) for p in sorted(hit)]


def parse_wr1(line: str) -> dict | None:
    m = WR1_RE.search(line)
    if not m:
        return None
    d = {
        "role": m.group("role"),
        "obs_session_id": int(m.group("obs")),
        "filter": m.group("filt"),
        "n": int(m.group("n")),
        "mean": int(m.group("mean")),
        "min": int(m.group("min")),
        "max": int(m.group("max")),
        "var": int(m.group("var")),
    }
    if m.group("addr"):
        d["addr"] = m.group("addr")
    return d


def diffs(anchors: dict) -> dict:
    inn = anchors.get("inside", {}).get("mean")
    out = anchors.get("outside", {}).get("mean")
    thr = anchors.get("threshold", {}).get("mean")
    d = {}
    if inn is not None and out is not None:
        d["outside_minus_inside"] = out - inn
    if inn is not None and thr is not None:
        d["threshold_minus_inside"] = thr - inn
    if out is not None and thr is not None:
        d["outside_minus_threshold"] = out - thr
    return d


def find_segger_tool(name: str) -> str | None:
    p = shutil.which(name)
    if p:
        return p
    globs = [Path("/opt/SEGGER").glob("JLink*"), Path("/Applications/SEGGER").glob("JLink*")]
    for it in globs:
        for base in sorted(it):
            cand = base / name
            if cand.is_file():
                return str(cand)
    return None


def find_rtt_logger() -> str | None:
    return find_segger_tool("JLinkRTTLogger")


RTT_ALL = False


def dispatch_rtt_line(line: str, q: queue.Queue) -> None:
    """Classify one RTT console line into queue events."""
    if SIDE_CLEAR.search(line):
        q.put(("side_clear", line))
    m = SIDE_PEER.search(line)
    if m:
        q.put(("side_peer", m.group(1).upper(), line))
    # Keep unlock-related RTT visible without flooding. The keyword list is a
    # diagnosis hazard: a grant logs nothing at all, so "no withheld line" reads
    # the same as "granted". --rtt-all turns the filter off.
    if RTT_ALL and line.strip():
        q.put(("rtt", line))
        return
    if any(
        k in line
        for k in (
            "SIDE peer=",
            "L2CAP CoC",
            "Aliro session",
            "UWB session",
            "BLE disconnected",
            "passive unlock",
            "side feed:",
            "side.deny",
        )
    ):
        q.put(("rtt", line))


class RttLink:
    """Bidirectional RTT over SEGGER's telnet server.

    JLinkRTTLogger can only read the up-buffer, so it can never deliver SF1 to
    the lock, and it holds the probe exclusively so nothing else can either.
    JLinkExe holds the probe once and exposes both directions on port 19021.

    With the lock's J-Link on a different machine than the witnesses, run
    JLinkExe next to the probe and forward the port, e.g.
        ssh -R 19021:127.0.0.1:19021 <pi>
    then start this tool there with --rtt-attach.
    """

    def __init__(self, host: str, port: int, chip: str, speed: int, spawn: bool):
        self.host = host
        self.port = port
        self.proc: subprocess.Popen | None = None
        self.sock: socket.socket | None = None
        self.log_path: Path | None = None
        self._tmpdir: str | None = None
        self._send_lock = threading.Lock()
        if spawn:
            self._spawn(chip, speed)
        self._connect()

    def _spawn(self, chip: str, speed: int) -> None:
        exe = find_segger_tool("JLinkExe") or find_segger_tool("JLink")
        if not exe:
            raise RuntimeError("JLinkExe not found — install SEGGER J-Link, or use --rtt-attach")
        self._tmpdir = tempfile.mkdtemp(prefix="side_jlink_")
        self.log_path = Path(self._tmpdir) / "jlink.log"
        cmd = [
            exe,
            "-device", chip,
            "-if", "SWD",
            "-speed", str(speed),
            "-autoconnect", "1",
            "-nogui", "1",
        ]
        print(f"rtt: {' '.join(cmd)}", flush=True)
        fh = self.log_path.open("w")
        # JLinkExe treats stdin EOF as "quit", so keep the pipe open.
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=fh, stderr=subprocess.STDOUT)

    def _connect(self) -> None:
        deadline = time.time() + 15.0
        last: Exception | None = None
        while time.time() < deadline:
            if self.proc is not None and self.proc.poll() is not None:
                tail = ""
                if self.log_path and self.log_path.exists():
                    tail = self.log_path.read_text(errors="replace")[-1500:]
                raise RuntimeError(f"JLinkExe exited {self.proc.returncode}\n{tail}")
            try:
                s = socket.create_connection((self.host, self.port), timeout=2.0)
                s.settimeout(0.2)
                self.sock = s
                print(f"rtt: connected {self.host}:{self.port} (bidirectional)", flush=True)
                return
            except OSError as e:
                last = e
                time.sleep(0.4)
        raise RuntimeError(f"no RTT server on {self.host}:{self.port} ({last})")

    def send(self, line: str) -> bool:
        """Write one line into RTT down-buffer 0."""
        s = self.sock
        if s is None:
            return False
        try:
            with self._send_lock:
                s.sendall((line + "\n").encode("ascii"))
            return True
        except OSError as e:
            print(f"! rtt send: {e}", file=sys.stderr, flush=True)
            return False

    def reader(self, q: queue.Queue, stop: threading.Event) -> None:
        buf = ""
        while not stop.is_set():
            s = self.sock
            if s is None:
                return
            try:
                chunk = s.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                if not stop.is_set():
                    q.put(("err", "RTT connection lost"))
                return
            if not chunk:
                time.sleep(0.05)
                continue
            buf += chunk.decode("utf-8", errors="replace")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                dispatch_rtt_line(line.rstrip("\r"), q)

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None
        if self.proc is not None and self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=1)
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass
        if self._tmpdir:
            shutil.rmtree(self._tmpdir, ignore_errors=True)
            self._tmpdir = None


class WitnessBus:
    """One Serial per witness; ADDR push asserts DTR and fans out in parallel."""

    def __init__(self, ports: list[Path], baud: int):
        import serial

        self._serial = serial
        self.baud = baud
        self.lock = threading.Lock()
        self._addr_lock = threading.Lock()
        self.pause = threading.Event()
        self.abort = threading.Event()
        self.ports: dict[str, object] = {}
        for p in ports:
            self.ports[str(p)] = self._open(str(p))
            print(f"+ open {p}", flush=True)

    @staticmethod
    def _arm_line(ser) -> None:
        # Zephyr CDC ACM often drops host RX until DTR is asserted.
        try:
            ser.dtr = True
            ser.rts = True
        except Exception:
            pass

    def _open(self, path: str):
        ser = self._serial.Serial(
            path,
            self.baud,
            timeout=0.15,
            write_timeout=1.0,
            rtscts=False,
            dsrdtr=False,
            xonxoff=False,
        )
        self._arm_line(ser)
        return ser

    def close(self) -> None:
        self.abort.set()
        self.pause.set()
        # Don't deadlock behind a stuck write_addr; best-effort close.
        got = self.lock.acquire(timeout=0.5)
        try:
            items = list(self.ports.items())
            for _, ser in items:
                if ser is None:
                    continue
                try:
                    ser.cancel_write()
                except Exception:
                    pass
                try:
                    ser.close()
                except Exception:
                    pass
            self.ports.clear()
        finally:
            if got:
                self.lock.release()

    def _exchange_addr(self, ser, line: str) -> bool:
        self._arm_line(ser)
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        ser.write((line + "\n").encode("ascii"))
        ser.flush()
        t0 = time.time()
        buf = ""
        while time.time() - t0 < 1.2 and not self.abort.is_set():
            chunk = ser.read(256).decode("utf-8", errors="replace")
            if not chunk:
                continue
            buf += chunk
            if ADDR_OK.search(buf):
                return True
        return False

    def _send_one(self, path: str, line: str) -> None:
        if self.abort.is_set():
            return

        # Prefer the already-open handle (readers are paused). Close/reopen is a
        # fallback — sequential reopen was too slow for the short CoC window.
        with self.lock:
            ser = self.ports.get(path)
        if ser is not None:
            try:
                if self._exchange_addr(ser, line):
                    print(f"  {path}: {line} -> ok", flush=True)
                    return
                print(f"  {path}: {line} -> no ack (retry reopen)", flush=True)
            except Exception as e:
                print(f"  {path}: {line} -> {e} (retry reopen)", flush=True)

        if self.abort.is_set():
            return
        with self.lock:
            old = self.ports.pop(path, None)
        if old is not None:
            try:
                old.close()
            except Exception:
                pass
        try:
            ser = self._open(path)
        except Exception as e:
            print(f"  ! {path}: reopen {e}", flush=True)
            return
        with self.lock:
            self.ports[path] = ser
        try:
            ok = self._exchange_addr(ser, line)
            print(f"  {path}: {line} -> {'ok' if ok else 'no ack'}", flush=True)
        except Exception as e:
            print(f"  ! {path}: {e}", flush=True)

    def write_addr(self, addr: str | None) -> str:
        """Push ADDR to every witness in parallel (CoC window is short)."""
        line = "ADDR 0" if not addr else f"ADDR {addr}"
        if self.abort.is_set():
            return line
        if not self._addr_lock.acquire(timeout=0.5):
            print(f"  ! ADDR busy, skip {line}", flush=True)
            return line
        try:
            self.pause.set()
            time.sleep(0.05)
            with self.lock:
                paths = list(self.ports.keys())
            threads = [
                threading.Thread(target=self._send_one, args=(path, line), daemon=True)
                for path in paths
            ]
            for t in threads:
                t.start()
            for t in threads:
                t.join(timeout=2.5)
            if not self.abort.is_set():
                self.pause.clear()
        finally:
            self._addr_lock.release()
        return line

    def push_addr(self, addr: str | None) -> None:
        self.write_addr(addr)

    def reader(self, path: str, q: queue.Queue, stop: threading.Event) -> None:
        buf = ""
        while not stop.is_set():
            if self.pause.is_set():
                time.sleep(0.02)
                continue
            ser = self.ports.get(path)
            if ser is None:
                time.sleep(0.05)
                continue
            try:
                with self.lock:
                    if self.pause.is_set():
                        continue
                    ser = self.ports.get(path)
                    if ser is None:
                        continue
                    chunk = ser.read(256).decode("utf-8", errors="replace")
            except Exception:
                time.sleep(0.05)
                continue
            if not chunk:
                time.sleep(0.02)
                continue
            buf += chunk
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                w = parse_wr1(line)
                if w:
                    w["port"] = path
                    w["ts"] = time.time()
                    q.put(("wr1", w))


def follow_rtt(path: Path, q: queue.Queue, stop: threading.Event) -> None:
    """Tail RTT log; emit side/peer events and echo interesting lines."""
    # Wait for logger to create the file.
    for _ in range(50):
        if path.exists() or stop.is_set():
            break
        time.sleep(0.1)
    if not path.exists():
        q.put(("err", f"RTT log not created: {path}"))
        return
    with path.open("r", errors="replace") as fh:
        fh.seek(0, os.SEEK_END)
        buf = ""
        while not stop.is_set():
            chunk = fh.read()
            if not chunk:
                time.sleep(0.05)
                continue
            buf += chunk
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                dispatch_rtt_line(line, q)


def sf1_line(anchors: dict) -> str:
    """Render one window as the lock's SF1 ingest line (see side_feed.h).

    Absent anchors go out as n=0, which is what makes the lock treat them as
    missing rather than as a real -0 dBm reading.
    """
    fields = []
    for key, role in (("in", "inside"), ("out", "outside"), ("th", "threshold")):
        a = anchors.get(role) or {}
        fields.append(f"{key}={int(a.get('mean') or 0)}")
    for key, role in (("ni", "inside"), ("no", "outside"), ("nt", "threshold")):
        a = anchors.get(role) or {}
        fields.append(f"{key}={max(0, min(255, int(a.get('n') or 0)))}")
    return "SF1 " + " ".join(fields)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ports", nargs="*", type=Path, help="witness ACM ports (default: all)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--label", default="unlabelled")
    ap.add_argument("--out", type=Path, help="JSONL capture path")
    ap.add_argument("--chip", default=CDK_CHIP, help="J-Link device name")
    ap.add_argument("--no-rtt", action="store_true", help="skip RTT entirely (WR1 only)")
    ap.add_argument("--rtt-log", type=Path, help="read-only: tail an existing RTT log (no SF1 feed)")
    ap.add_argument(
        "--rtt-logger",
        action="store_true",
        help="read-only: spawn JLinkRTTLogger instead of JLinkExe (no SF1 feed)",
    )
    ap.add_argument("--rtt-host", default="127.0.0.1", help="RTT telnet host (default 127.0.0.1)")
    ap.add_argument("--rtt-port", type=int, default=RTT_TELNET_PORT)
    ap.add_argument("--rtt-speed", type=int, default=4000, help="SWD kHz")
    ap.add_argument(
        "--rtt-all",
        action="store_true",
        help="log every RTT console line, not just unlock-related ones",
    )
    ap.add_argument(
        "--rtt-attach",
        action="store_true",
        help="connect to an RTT server someone else started (probe on another host + ssh -R)",
    )
    ap.add_argument(
        "--force-sf1",
        choices=("outside", "inside"),
        help="LAB BISECT: pump a synthetic side into the gate on a timer, ignoring the "
        "witnesses entirely. Answers 'is the side gate the only thing blocking the unlock?' "
        "Never use this to demonstrate the gate working.",
    )
    ap.add_argument(
        "--force-sf1-period-s", type=float, default=1.5, help="synthetic SF1 cadence"
    )
    ap.add_argument(
        "--no-feed-sf1",
        action="store_true",
        help="do not push SF1 windows into the lock (capture/observe only)",
    )
    ap.add_argument(
        "--addr-hold-s",
        type=float,
        default=45.0,
        help="keep ADDR after SIDE peer=clear (default 45; long enough for a static stand)",
    )
    ap.add_argument("--walk", type=int, default=1, help="starting walk id for phase markers")
    ap.add_argument(
        "--pose",
        choices=("outside_pause", "inside_pause", "threshold_pause", "walking"),
        help="fixed phase for the whole run — use this when capturing alone (no typing)",
    )
    ap.add_argument(
        "--choreo",
        action="store_true",
        help="after ADDR, auto-mark outside→walk→inside→walk→outside on a timer (no typing)",
    )
    ap.add_argument("--choreo-out-s", type=float, default=8.0, help="choreo outside_pause seconds")
    ap.add_argument("--choreo-walk-s", type=float, default=12.0, help="choreo walking seconds")
    ap.add_argument("--choreo-in-s", type=float, default=8.0, help="choreo inside_pause seconds")
    ap.add_argument("--keys", action="store_true", help="enable o/i/n keyboard marks")
    ap.add_argument(
        "--role-map",
        default="",
        help="override the firmware role per port when a dongle sits at the wrong post, "
        "e.g. ttyACM1=outside,ttyACM2=threshold (substring match on the port path)",
    )
    ap.add_argument(
        "--simulate-gate",
        action="store_true",
        help="print live GATE unlock decisions from ADDR-filtered WR1 (mirrors woz_side defaults)",
    )
    args = ap.parse_args()

    global RTT_ALL
    RTT_ALL = bool(args.rtt_all)

    ports = args.ports or probe_witness_ports(discover_ports(), args.baud)
    if len(ports) < 1:
        print("no witness WR1 ports found — plug the three dongles in", file=sys.stderr)
        return 2
    if len(ports) < 3:
        print(f"warning: only {len(ports)} witness port(s); expected 3", file=sys.stderr)

    # A dongle's role is baked into its firmware, so a dongle standing at the
    # wrong post reports the wrong role and no amount of replugging fixes it.
    role_overrides: dict[str, str] = {}
    for item in args.role_map.split(","):
        if not item.strip():
            continue
        frag, _, role = item.partition("=")
        if role not in ("inside", "outside", "threshold"):
            print(f"bad --role-map entry {item!r} (role must be inside|outside|threshold)",
                  file=sys.stderr)
            return 2
        role_overrides[frag.strip()] = role

    print(f"witnesses: {' '.join(str(p) for p in ports)}", flush=True)
    for frag, role in role_overrides.items():
        print(f"role override: *{frag}* -> {role}", flush=True)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        print(f"jsonl -> {args.out}", flush=True)

    try:
        bus = WitnessBus(ports, args.baud)
    except Exception as e:
        print(f"serial open failed: {e}", file=sys.stderr)
        return 2

    stop = threading.Event()
    q: queue.Queue = queue.Queue()
    role_port: dict[str, str] = {}
    buckets: dict[int, dict] = {}
    out_fh = args.out.open("a") if args.out else None
    out_lock = threading.Lock()
    current_peer: str | None = None
    clear_timer: threading.Timer | None = None
    rtt_proc: subprocess.Popen | None = None
    rtt_path: Path | None = None
    rtt_link: RttLink | None = None
    tmpdir = None
    phase = {
        "name": args.pose or "walking",
        "walk": max(1, int(args.walk)),
        "choreo_gen": 0,
    }
    gate = {"cand": "UNKNOWN", "cand_n": 0, "committed": "UNKNOWN"}

    def gate_on_bucket(b: dict) -> None:
        """Mirror woz_side defaults for live HITL feedback (not the lock itself)."""
        if not args.simulate_gate:
            return
        # Same reason feed_sf1 waits for ADDR: an unfiltered window averages
        # every advertiser in the room, so printing a GATE verdict off it is a
        # confident statement about nobody in particular.
        if current_peer is None:
            return
        an = b.get("anchors") or {}
        inn, out, thr = an.get("inside") or {}, an.get("outside") or {}, an.get("threshold") or {}
        ni, no, nt = int(inn.get("n") or 0), int(out.get("n") or 0), int(thr.get("n") or 0)
        if ni < 3 or no < 3 or "mean" not in inn or "mean" not in out:
            return
        omi = int(out["mean"]) - int(inn["mean"])
        if omi >= 6:
            side, conf = "OUTSIDE", min(100, 60 + (omi - 6) * 4)
        elif omi <= -6:
            side, conf = "INSIDE", min(100, 60 + (-omi - 6) * 4)
        else:
            side, conf = ("THRESHOLD" if nt >= 3 else "UNKNOWN"), 55
        if side == "UNKNOWN":
            gate["cand"], gate["cand_n"] = "UNKNOWN", 0
        elif side == gate["cand"]:
            gate["cand_n"] += 1
        else:
            gate["cand"], gate["cand_n"] = side, 1
        if gate["cand_n"] >= 3 and side != "UNKNOWN":
            gate["committed"] = side
        unlock = gate["committed"] == "OUTSIDE" and conf >= 70 and gate["cand_n"] >= 3
        print(
            f"GATE side={side} omi={omi:+d} committed={gate['committed']} "
            f"unlock={'YES' if unlock else 'no'} phase={b.get('phase')}",
            flush=True,
        )

    def force_sf1_loop() -> None:
        """Bisect aid: drive the gate from a constant, so the only remaining
        explanation for a missing unlock is somewhere other than woz_side."""
        line = (
            "SF1 in=-60 out=-40 th=0 ni=6 no=6 nt=0"
            if args.force_sf1 == "outside"
            else "SF1 in=-40 out=-60 th=0 ni=6 no=6 nt=0"
        )
        while not stop.is_set():
            if rtt_link is not None and rtt_link.send(line):
                print(f">> FORCED {line}", flush=True)
            time.sleep(max(0.2, float(args.force_sf1_period_s)))

    def feed_sf1(b: dict) -> None:
        """Push one window into the lock's woz_side gate over RTT down-buffer 0."""
        if rtt_link is None or args.no_feed_sf1 or args.force_sf1:
            return
        # Without an ADDR filter the witness means cover every advertiser in
        # range, so they say nothing about where THIS phone is. Feeding those
        # would hand the gate a confident answer about the wrong device.
        if current_peer is None:
            return
        line = sf1_line(b.get("anchors") or {})
        if rtt_link.send(line):
            print(f">> {line}", flush=True)

    def write_jsonl(obj: dict) -> None:
        if out_fh is None:
            return
        with out_lock:
            out_fh.write(json.dumps(obj, sort_keys=True) + "\n")
            out_fh.flush()

    def set_phase(name: str, *, bump_walk: bool = False) -> None:
        if args.pose:
            name = args.pose
        if bump_walk:
            phase["walk"] = int(phase["walk"]) + 1
        phase["name"] = name
        mark = {
            "schema": 1,
            "type": "phase",
            "ts_host": datetime.now(timezone.utc).isoformat(),
            "label": args.label,
            "walk": phase["walk"],
            "phase": name,
        }
        write_jsonl(mark)
        print(f"## walk={phase['walk']} phase={name}", flush=True)

    def run_choreo(gen: int) -> None:
        """Hands-free labelled walk after ADDR. Memorize before you leave the Pi."""
        steps = [
            ("outside_pause", float(args.choreo_out_s), "STAND AT OUTSIDE STICK"),
            ("walking", float(args.choreo_walk_s), "WALK TO DOOR / UNLOCK IF NEEDED"),
            ("inside_pause", float(args.choreo_in_s), "STAND AT INSIDE STICK"),
            ("walking", float(args.choreo_walk_s), "WALK BACK OUTSIDE"),
            ("outside_pause", float(args.choreo_out_s), "STAND AT OUTSIDE STICK AGAIN"),
        ]
        for name, secs, human in steps:
            if stop.is_set() or phase["choreo_gen"] != gen:
                return
            set_phase(name)
            print(f">>> {human} ({secs:.0f}s)", flush=True)
            end = time.time() + secs
            while time.time() < end:
                if stop.is_set() or phase["choreo_gen"] != gen:
                    return
                time.sleep(0.2)
        if not stop.is_set() and phase["choreo_gen"] == gen:
            set_phase("walking", bump_walk=True)
            print(">>> choreo done — next unlock starts walk+1", flush=True)

    def start_choreo() -> None:
        if not args.choreo:
            return
        phase["choreo_gen"] = int(phase["choreo_gen"]) + 1
        gen = int(phase["choreo_gen"])
        threading.Thread(target=run_choreo, args=(gen,), daemon=True).start()

    def stdin_phases() -> None:
        help_s = "keys+Enter: o=outside_pause i=inside_pause t=threshold_pause w=walking n=new_walk"
        print("keyboard marks enabled (--keys); second person at Pi only", flush=True)
        print(help_s, flush=True)
        while not stop.is_set():
            try:
                line = sys.stdin.readline()
            except Exception:
                break
            if line == "":
                break
            cmd = line.strip().lower()
            print(f"[stdin] {cmd!r}", flush=True)
            if cmd in ("o", "out", "outside"):
                set_phase("outside_pause")
            elif cmd in ("i", "in", "inside"):
                set_phase("inside_pause")
            elif cmd in ("t", "thr", "threshold"):
                set_phase("threshold_pause")
            elif cmd in ("w", "walk", "walking"):
                set_phase("walking")
            elif cmd in ("n", "new"):
                set_phase(args.pose or "walking", bump_walk=True)
            elif cmd in ("?", "h", "help"):
                print(help_s, flush=True)
            elif cmd:
                print(f"! unknown phase cmd {cmd!r} ({help_s})", flush=True)
            else:
                print(help_s, flush=True)

    def cancel_clear_timer() -> None:
        nonlocal clear_timer
        if clear_timer is not None:
            clear_timer.cancel()
            clear_timer = None

    def schedule_clear() -> None:
        nonlocal clear_timer, current_peer
        cancel_clear_timer()
        hold = max(0.0, float(args.addr_hold_s))

        def _do_clear() -> None:
            nonlocal current_peer
            print(f">> ADDR 0 (clear after {hold:.0f}s hold)", flush=True)
            bus.push_addr(None)
            current_peer = None

        if hold <= 0:
            _do_clear()
            return
        print(f">> holding ADDR for {hold:.0f}s after disconnect…", flush=True)
        if args.pose:
            print(f">>> stay at {args.pose} until hold ends", flush=True)
        clear_timer = threading.Timer(hold, _do_clear)
        clear_timer.daemon = True
        clear_timer.start()

    sig_hits = {"n": 0}

    def shutdown(*_a) -> None:
        sig_hits["n"] += 1
        stop.set()
        bus.abort.set()
        cancel_clear_timer()
        if rtt_proc and rtt_proc.poll() is None:
            try:
                rtt_proc.terminate()
            except Exception:
                pass
        # First Ctrl-C: cooperative stop. Second: hard exit (serial can wedge).
        if sig_hits["n"] >= 2:
            print("\nforced exit", file=sys.stderr, flush=True)
            try:
                bus.close()
            except Exception:
                pass
            if rtt_link is not None:
                try:
                    rtt_link.close()
                except Exception:
                    pass
            if rtt_proc and rtt_proc.poll() is None:
                try:
                    rtt_proc.kill()
                except Exception:
                    pass
            os._exit(130)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # RTT source. Default is the bidirectional telnet server, because the SF1
    # feed has to travel toward the lock and JLinkRTTLogger cannot carry it.
    # The read-only paths below stay for capture-only runs and for a Pi that
    # only has the logger binary.
    read_only_rtt = bool(args.rtt_log or args.rtt_logger)
    if not args.no_rtt and not read_only_rtt:
        try:
            rtt_link = RttLink(
                args.rtt_host,
                args.rtt_port,
                args.chip,
                args.rtt_speed,
                spawn=not args.rtt_attach,
            )
        except Exception as e:
            print(f"rtt: {e}", file=sys.stderr)
            bus.close()
            return 2
        threading.Thread(target=rtt_link.reader, args=(q, stop), daemon=True).start()
        if args.no_feed_sf1:
            print("rtt: SF1 feed disabled (--no-feed-sf1)", flush=True)
        else:
            print("rtt: SF1 feed armed — windows go to the lock while ADDR is set", flush=True)
    elif not args.no_rtt:
        if args.rtt_log:
            rtt_path = args.rtt_log
            print(f"rtt log (existing): {rtt_path}", flush=True)
        else:
            logger = find_rtt_logger()
            if not logger:
                print("JLinkRTTLogger not found — install SEGGER J-Link or pass --rtt-log", file=sys.stderr)
                bus.close()
                return 2
            rtt_path = Path.home() / ".config" / "SEGGERRTTLogger_Channel_Terminal.log"
            rtt_path.parent.mkdir(parents=True, exist_ok=True)
            # Truncate so we only see new session lines.
            rtt_path.write_text("")
            err_path = Path(tempfile.mkdtemp(prefix="side_rtt_")) / "logger.err"
            tmpdir = str(err_path.parent)
            cmd = [
                logger,
                "-Device",
                args.chip,
                "-If",
                "SWD",
                "-Speed",
                "4000",
                "-RTTChannel",
                "0",
                str(rtt_path),
            ]
            print(f"rtt: {' '.join(cmd)}", flush=True)
            print(f"rtt log: {rtt_path}", flush=True)
            err_fh = err_path.open("w")
            # Keep a live stdin pipe: logger treats EOF as "key pressed" and exits.
            rtt_proc = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=err_fh,
                stderr=subprocess.STDOUT,
            )
            time.sleep(1.5)
            if rtt_proc.poll() is not None:
                err_fh.flush()
                print(f"! JLinkRTTLogger exited {rtt_proc.returncode}", file=sys.stderr)
                print(err_path.read_text(errors="replace")[-2000:], file=sys.stderr)
                bus.close()
                return 2
            print(f"rtt: logger pid {rtt_proc.pid} ok", flush=True)
        threading.Thread(target=follow_rtt, args=(rtt_path, q, stop), daemon=True).start()
    else:
        print("rtt: disabled (--no-rtt)", flush=True)

    # Witnesses keep the last ADDR across runs, and a phone's random address is
    # long dead by the next session -- inheriting it filters every window down
    # to n=0. Start unfiltered; the real ADDR arrives on the next SIDE peer=.
    print(">> ADDR 0 (clearing any filter left by an earlier run)", flush=True)
    bus.write_addr(None)

    for path in bus.ports:
        threading.Thread(target=bus.reader, args=(path, q, stop), daemon=True).start()

    if args.force_sf1:
        print(
            f"!! LAB BISECT: forcing side={args.force_sf1.upper()} into the gate; "
            "witness data is NOT being used",
            flush=True,
        )
        threading.Thread(target=force_sf1_loop, daemon=True).start()

    if args.keys:
        threading.Thread(target=stdin_phases, daemon=True).start()
    set_phase(args.pose or "walking")
    if args.pose:
        print(
            f"ready — fixed pose={args.pose}; unlock then stay put for ADDR hold; Ctrl-C to stop",
            flush=True,
        )
    elif args.choreo:
        print(
            "ready — --choreo will auto-label after ADDR; memorize: "
            f"OUT {args.choreo_out_s:.0f}s → walk {args.choreo_walk_s:.0f}s → "
            f"IN {args.choreo_in_s:.0f}s → walk {args.choreo_walk_s:.0f}s → "
            f"OUT {args.choreo_out_s:.0f}s; Ctrl-C to stop",
            flush=True,
        )
    else:
        print(
            "ready — add --pose outside_pause|inside_pause or --choreo for labels; Ctrl-C to stop",
            flush=True,
        )

    try:
        while not stop.is_set():
            try:
                item = q.get(timeout=0.2)
            except queue.Empty:
                continue
            kind = item[0]
            if kind == "err":
                print(f"! {item[1]}", file=sys.stderr, flush=True)
            elif kind == "rtt":
                print(f"RTT {item[1]}", flush=True)
            elif kind == "side_clear":
                print(f"RTT {item[1]}", flush=True)
                if current_peer is not None:
                    schedule_clear()
            elif kind == "side_peer":
                peer, line = item[1], item[2]
                print(f"RTT {line}", flush=True)
                cancel_clear_timer()
                if peer != current_peer:
                    print(f">> ADDR {peer}", flush=True)
                    current_peer = peer
                    # Background so RTT keep printing; write_addr pauses readers itself.
                    threading.Thread(target=bus.write_addr, args=(peer,), daemon=True).start()
                    if args.pose:
                        print(f">>> ADDR armed — stay at {args.pose}", flush=True)
                    elif args.choreo:
                        print(">>> ADDR armed — follow choreo countdowns (no typing)", flush=True)
                        start_choreo()
                    else:
                        print(">>> ADDR armed — unlock/stand; use --pose or --choreo to label", flush=True)
            elif kind == "wr1":
                w = item[1]
                for frag, role in role_overrides.items():
                    if frag in w["port"]:
                        w["role"] = role
                        break
                role_port[w["role"]] = w["port"]
                addr = w.get("addr")
                addr_s = f" addr={addr}" if addr else ""
                print(
                    f"{w['port']}  WR1 role={w['role']:9} obs={w['obs_session_id']:<5} "
                    f"n={w['n']:<3} mean={w['mean']:>4} var={w['var']}{addr_s}",
                    flush=True,
                )
                # Bucket unconditionally: the SF1 feed and --simulate-gate both
                # need windows, and neither implies a JSONL capture.
                key = int(w["ts"] // WINDOW_S)
                bucket = buckets.setdefault(
                    key,
                    {
                        "schema": 1,
                        "ts_host": datetime.now(timezone.utc).isoformat(),
                        "label": args.label,
                        "walk": phase["walk"],
                        "phase": phase["name"],
                        "obs_session_id": key,
                        "anchors": {},
                        "ports": {},
                    },
                )
                bucket["anchors"][w["role"]] = {
                    "n": w["n"],
                    "mean": w["mean"],
                    "min": w["min"],
                    "max": w["max"],
                    "var": w["var"],
                }
                if addr:
                    bucket["anchors"][w["role"]]["addr"] = addr
                bucket["ports"][w["role"]] = w["port"]
                stale = [k for k in buckets if k < key - 1]
                for k in stale + ([key] if len(bucket["anchors"]) >= 3 else []):
                    b = buckets.get(k)
                    if not b or len(b["anchors"]) < 2:
                        if k in stale:
                            buckets.pop(k, None)
                        continue
                    b["diff"] = diffs(b["anchors"])
                    write_jsonl(b)
                    feed_sf1(b)
                    gate_on_bucket(b)
                    verb = "saved" if out_fh is not None else "window"
                    print(
                        f"  -> {verb} walk={b.get('walk')} phase={b.get('phase')} {b['diff']}",
                        file=sys.stderr,
                        flush=True,
                    )
                    buckets.pop(k, None)
    finally:
        stop.set()
        bus.abort.set()
        cancel_clear_timer()
        # Never block forever clearing ADDR on the way out — that wedged Ctrl-C.
        bus.close()
        if rtt_link is not None:
            try:
                rtt_link.close()
            except Exception:
                pass
        if rtt_proc and rtt_proc.poll() is None:
            try:
                rtt_proc.terminate()
                rtt_proc.wait(timeout=1)
            except Exception:
                try:
                    rtt_proc.kill()
                except Exception:
                    pass
        if out_fh:
            try:
                out_fh.close()
            except Exception:
                pass
        if role_port:
            print("\nstopped — role → port:", file=sys.stderr)
            for role, p in sorted(role_port.items()):
                print(f"  {role:9} {p}", file=sys.stderr)
        if tmpdir:
            try:
                shutil.rmtree(tmpdir, ignore_errors=True)
            except Exception:
                pass
        sys.stderr.flush()
        sys.stdout.flush()
        os._exit(0)


if __name__ == "__main__":
    raise SystemExit(main())
