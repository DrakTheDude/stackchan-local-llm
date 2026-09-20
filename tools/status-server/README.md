# status-server

A status server for the robot's ambient ring. One file, no dependencies, Python 3.

```bash
python3 status_server.py            # report this machine's disk, memory, load, uptime
python3 status_server.py --demo     # cycle ok -> warn -> alert every 60s
python3 status_server.py --port 8899 --host 0.0.0.0
```

Then, over USB serial to the robot:

```
STATUS_URL http://<this machine's LAN address>:8899/mcp
```

[docs/ambient-status.md](../../docs/ambient-status.md) is the full story — the
contract, what the robot does with each level, and what to check when it does not
work.

## What it is for

Two things: proving the wiring without a homelab, and being something to copy.
Your own version reports whatever you actually care about. The robot does not
care what the numbers mean — it cares that the level is honest.

It also answers `tools/list`, so the same URL can be added to the server's
[MCP config](../../docs/mcp.md) and the model can *answer questions* about what
the ring is already showing.

## Two warnings worth reading before you point it at anything

🔴 **Read-only.** It exposes nothing that changes anything, and neither should
yours. The robot polls it with no confirmation step anywhere in the path.

⚠️ **No authentication, and it binds to `0.0.0.0`** so the robot can reach it.
That is the right trade for free disk space on a home LAN and the wrong one for
anything you would mind a stranger reading. If what you report is sensitive, put
it behind something that asks for a token — the robot will send one
(`STATUS_TOKEN`).

## The thresholds

At the top of the file, where they can be argued with:

```python
DISK_WARN_PCT, DISK_ALERT_PCT = 85, 95
LOAD_WARN, LOAD_ALERT = 1.5, 3.0   # per core
```

Memory and uptime are reported but never raise the level — they are read from
`/proc`, and on a machine without one those cards are simply omitted rather than
guessed at.
