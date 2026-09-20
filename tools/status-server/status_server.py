#!/usr/bin/env python3
"""A status server for the robot's ambient ring, in one file and no dependencies.

    python3 status_server.py                 # serve this machine's health
    python3 status_server.py --demo          # cycle ok -> warn -> alert
    python3 status_server.py --port 8899

Then, over USB serial to the robot:

    STATUS_URL http://<this machine>:8899/mcp

It speaks the smallest useful slice of MCP: one JSON-RPC POST, answered
immediately. `initialize` and `tools/list` are implemented too, so the same URL
can be handed to the xiaozhi server as a normal MCP server and the model can ask
out loud what the robot is already showing on the ring.

WHAT IT IS FOR. Proving the wiring, and giving you something to copy. Your own
version reports what you actually care about - a homelab, a CI pipeline, a
freezer temperature, an on-call rota. The robot does not care what the numbers
mean. It cares that the level is honest.

🔴 READ-ONLY. It exposes no tool that changes anything, and yours should not
   either. The robot has no confirmation step.

⚠️ NO AUTHENTICATION, and it binds to 0.0.0.0 so the robot can reach it. That is
   the right trade for a thing that reports free disk space on a home LAN, and
   the wrong one for anything you would mind a stranger reading. If what you
   report is sensitive, put it behind something that asks for a token - the
   robot sends one if you give it one (STATUS_TOKEN).
"""

import argparse
import json
import os
import shutil
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TOOL = "status.get"

# 🔴 The thresholds ARE the contract. Everything else here is decoration: the
#    ring is green or red because of these two numbers, so they are at the top
#    where they can be argued with, not buried in the function that uses them.
DISK_WARN_PCT, DISK_ALERT_PCT = 85, 95
LOAD_WARN, LOAD_ALERT = 1.5, 3.0  # per core


def read_meminfo():
    """Total and available memory in GB, or None where /proc does not exist."""
    try:
        vals = {}
        with open("/proc/meminfo") as f:
            for line in f:
                key, _, rest = line.partition(":")
                vals[key] = int(rest.split()[0]) * 1024
        return vals["MemTotal"] / 1e9, vals["MemAvailable"] / 1e9
    except (OSError, KeyError, ValueError, IndexError):
        return None


def uptime_text():
    try:
        with open("/proc/uptime") as f:
            secs = float(f.read().split()[0])
    except (OSError, ValueError, IndexError):
        return None
    d, rem = divmod(int(secs), 86400)
    h, m = divmod(rem // 60, 60)
    return f"{d}d {h}h {m}m" if d else f"{h}h {m}m"


def real_status():
    """This machine, as level + one sentence + cards."""
    cards, problems = [], []
    level = "ok"

    def worsen(to):
        nonlocal level
        order = {"ok": 0, "warn": 1, "alert": 2}
        if order[to] > order[level]:
            level = to

    disk = shutil.disk_usage("/")
    used_pct = round(100 * disk.used / disk.total)
    cards.append({
        "label": "DISK",
        # "a / b" draws a bar on the idle screen as well as printing the value.
        "value": f"{disk.used / 1e9:.0f} / {disk.total / 1e9:.0f} GB",
        "sub": f"{used_pct}% used",
    })
    if used_pct >= DISK_ALERT_PCT:
        worsen("alert")
        problems.append(f"disk {used_pct}% full")
    elif used_pct >= DISK_WARN_PCT:
        worsen("warn")
        problems.append(f"disk {used_pct}% full")

    mem = read_meminfo()
    if mem:
        total, avail = mem
        cards.append({
            "label": "MEMORY",
            "value": f"{total - avail:.1f} / {total:.1f} GB",
            "sub": f"{avail:.1f} GB available",
        })

    try:
        load = os.getloadavg()[0]
        cores = os.cpu_count() or 1
        per_core = load / cores
        cards.append({
            "label": "LOAD",
            "value": f"{load:.2f}",
            "sub": f"{cores} cores",
        })
        if per_core >= LOAD_ALERT:
            worsen("alert")
            problems.append(f"load {load:.1f}")
        elif per_core >= LOAD_WARN:
            worsen("warn")
            problems.append(f"load {load:.1f}")
    except (OSError, AttributeError):
        pass

    up = uptime_text()
    if up:
        cards.append({"label": "UPTIME", "value": up, "sub": "since last boot"})

    # 🔴 The summary gets SPOKEN, out loud, when the level changes. So it is a
    #    sentence a person would say - not "STATUS: DEGRADED (2)". Say the
    #    problem, not the count of problems, because the count is the part
    #    nobody can act on.
    host = os.uname().nodename if hasattr(os, "uname") else "this machine"
    if problems:
        summary = f"{host}: " + ", ".join(problems) + "."
    else:
        summary = f"{host} is healthy."

    return {"level": level, "summary": summary, "cards": cards}


# --demo. A fixed rotation, so the ring, the chime, the double-take and the idle
# screen can all be seen in about a minute without breaking anything real.
DEMO = [
    {"level": "ok", "summary": "All quiet. Nothing wants anything from you.",
     "cards": [{"label": "DEMO", "value": "ok", "sub": "this is a demonstration"}]},
    {"level": "warn", "summary": "The demo server is pretending something is wrong.",
     "cards": [{"label": "DEMO", "value": "warn", "sub": "still pretending"}]},
    {"level": "alert", "summary": "And now it is pretending something is badly wrong.",
     "cards": [{"label": "DEMO", "value": "alert", "sub": "nothing is actually on fire"}]},
]
DEMO_SECONDS = 60


def status_for(demo):
    if not demo:
        return real_status()
    return DEMO[int(time.time() // DEMO_SECONDS) % len(DEMO)]


class Handler(BaseHTTPRequestHandler):
    demo = False

    def _send(self, payload, code=200):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _result(self, req_id, text):
        # The MCP envelope: a result, whose content is one text part. The robot
        # parses THAT text as the status JSON.
        self._send({"jsonrpc": "2.0", "id": req_id,
                    "result": {"content": [{"type": "text", "text": text}]}})

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        try:
            req = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            self._send({"jsonrpc": "2.0", "id": None,
                        "error": {"code": -32700, "message": "parse error"}}, 400)
            return

        method, req_id = req.get("method"), req.get("id")

        if method == "initialize":
            self._send({"jsonrpc": "2.0", "id": req_id, "result": {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "status-server", "version": "1.0.0"},
            }})
        elif method == "notifications/initialized":
            # A notification has no id and takes no reply.
            self.send_response(204)
            self.end_headers()
        elif method == "ping":
            self._send({"jsonrpc": "2.0", "id": req_id, "result": {}})
        elif method == "tools/list":
            self._send({"jsonrpc": "2.0", "id": req_id, "result": {"tools": [{
                "name": TOOL,
                # Written for a model to READ ALOUD from, which is why it says
                # what to do with the answer rather than describing its shape.
                "description": ("How this machine is doing right now. Returns a level "
                                "(ok, warn or alert), a one-sentence summary you can "
                                "say as it is, and some numbers. Use it when asked how "
                                "things are, or whether anything is wrong."),
                "inputSchema": {"type": "object", "properties": {}},
            }]}})
        elif method == "tools/call":
            name = req.get("params", {}).get("name")
            if name == TOOL:
                self._result(req_id, json.dumps(status_for(self.demo)))
            else:
                # Name the tool that was asked for, not the method. A wrong tool
                # name is the commonest mistake here by a distance, and "no such
                # method: tools/call" sends you looking in the wrong place.
                self._send({"jsonrpc": "2.0", "id": req_id, "error": {
                    "code": -32602,
                    "message": f"no tool named {name!r} - this server has {TOOL!r}"}})
        else:
            self._send({"jsonrpc": "2.0", "id": req_id,
                        "error": {"code": -32601, "message": f"no such method: {method}"}})

    def log_message(self, fmt, *args):
        # One line per poll, with the level in it - because the first question is
        # always "is it even being asked?", and the second is "what did it say?".
        print(f"{self.address_string()} {fmt % args} -> {status_for(self.demo)['level']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=8899)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--demo", action="store_true",
                    help="cycle ok/warn/alert every minute instead of reporting real health")
    args = ap.parse_args()

    Handler.demo = args.demo
    print(f"{TOOL} on http://{args.host}:{args.port}/mcp"
          + ("  [demo: cycling every 60s]" if args.demo else ""))
    print(json.dumps(status_for(args.demo), indent=2))
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
