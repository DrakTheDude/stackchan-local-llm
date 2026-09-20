# Making him speak first

Everything else in this project happens because you said something. This is the
one thing that does not.

Point him at a status server and the LED ring carries its health, the idle screen
becomes a small dashboard, and when the level *changes* he does a double-take,
chimes and says the sentence out loud — with nobody having spoken to him.

It is **off** until you give him a URL. No URL, no polling task, no traffic.

---

## Five minutes, with nothing else running

There is a complete status server in [tools/status-server/](../tools/status-server/) —
one file, no dependencies. Run it in demo mode, which cycles `ok → warn → alert`
once a minute so you can see all three without breaking anything real:

```bash
python3 tools/status-server/status_server.py --demo
```

Then tell the robot where it is, over the USB serial console (the same cable you
flash with — see [below](#talking-to-him-over-serial) for how to send a line):

```
STATUS_URL http://10.0.0.5:8899/mcp
```

Use the machine's LAN address, not `localhost` — `localhost` on the robot is the
robot. Reboot once, wait 45 seconds, and the ring turns green. A minute later it
goes amber and he tells you so.

Drop `--demo` and it reports that machine's real disk, memory, load and uptime.

---

## The contract

One HTTP POST containing one JSON-RPC `tools/call`, one JSON reply. That is the
whole protocol, and a server that speaks it is about a hundred lines.

**He sends:**

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call",
 "params":{"name":"status.get","arguments":{}}}
```

**You answer** with the MCP envelope, whose text is itself the status JSON:

```json
{"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":
  "{\"level\":\"ok\",\"summary\":\"All healthy - 3 nodes.\",\"cards\":[]}"
}]}}
```

| field | |
|---|---|
| `level` | **required** — `ok`, `warn` or `alert`. Anything else is treated as *unknown* |
| `summary` | **required** — one sentence. It gets **spoken** |
| `cards` | optional, first 6 used. `{"label","value","sub"}`, shown in rotation on the idle screen. A `value` shaped `"28 / 61 GB"` also draws a bar |

Three rules that are worth more than the schema:

🔴 **An unrecognised level stays unknown, and unknown mutes the ring.** He never
infers a colour from the words in the summary. A ring that guesses green is the
exact failure this feature exists to prevent.

🔴 **The summary is spoken aloud, so write it to be heard.** `"disk 96% full"`,
not `"STATUS: DEGRADED (2 conditions)"`. Say the problem, not the count of
problems — the count is the part nobody can act on.

🔴 **Not JSON at all? The whole text becomes the summary and the level is
unknown.** That is deliberate: point him at any existing tool that returns a
sentence and you get the words on the idle screen, without a colour. Reduced
function beats a hard failure, and the colour was the only part that would have
been a guess.

### One stateless POST — no handshake

No `initialize` first, no session id, no SSE. A server that insists on a session
before it will answer cannot be used here.

That is a floor, not an oversight. A poller that holds a session is a poller that
can quietly *lose* one, and the failure would show up as a confidently green ring
rather than as an error.

---

## What he does with it

| level | ring | idle screen | on a change |
|---|---|---|---|
| `ok` | green, slow breath | cards, green dot | chime + the sentence |
| `warn` | amber, faster breath | cards, amber dot | chime + the sentence |
| `alert` | **red, blinking** | cards, red dot | **double-take** + chime + the sentence |
| unknown | muted | the summary, grey dot | nothing |

**On a change, not on a level.** Announcing the current state every few minutes
is how a useful alert becomes a thing you unplug. And never over a reply in
progress — if he is mid-sentence the announcement is deferred, not dropped.

### Staleness is part of it

🔴 A confidently green ring in front of a server that stopped answering twenty
minutes ago is worse than no ring at all.

After **12 minutes** with no successful reading the ring drops to muted and the
idle screen says **NO CONTACT** with the age of the last reading. Every screen
that shows a reading also shows how old it is.

⚠️ That window is fixed, so a poll interval above about ten minutes makes him go
stale *between* successful readings. `STATUS_SECS` is capped at an hour, but
anything over ~600 will look broken.

---

## Talking to him over serial

Config lives in NVS on the robot and is typed in over USB — the same cable you
flash with. One line at a time, at 115200 baud, from a serial terminal
(`idf.py monitor`, `screen`, PuTTY, the Arduino serial monitor):

| line | |
|---|---|
| `STATUS_URL http://10.0.0.5:8899/mcp` | where to poll. **This is the on switch** |
| `STATUS_TOOL fleet.status` | if your tool is not called `status.get` |
| `STATUS_TOKEN <secret>` | optional, sent as `Authorization: Bearer` |
| `STATUS_SECS 180` | poll interval, 30–3600. Default 180 |
| `STATUS_CA -----BEGIN CERTIFICATE-----\nMII...` | a private CA, with `\n` for the line breaks |
| `STATUS_SHOW` | print the config. Never prints the token |
| `STATUS_OFF` | forget all of it |

Everything but the URL takes effect on the next poll. The first URL needs a
reboot, because the poller is not started on a robot that has none.

`https://` is verified against the built-in public CA bundle, or against
`STATUS_CA` if you stored one. `http://` is fine on a LAN you trust.

### Where the credential goes, and what it is worth

🔴 **Never put a token in a file in this repository, a build file, or a chat
message.** It goes over the serial cable, which already requires holding the
robot, and it lands in NVS.

Be honest about what that buys you: **NVS is not encrypted here.** Whoever holds
the robot can read the token back out of flash. That is acceptable for a
read-only, revocable, LAN-scoped credential and for nothing else. Scope it at the
source, give it the least it can do the job with, and revoke it if the robot
leaves your desk.

⚠️ And a token sent over `http://` is a token sent in the clear. If it is worth
protecting, it is worth `https://`.

---

## Writing your own

Copy [`status_server.py`](../tools/status-server/status_server.py) and change
what it reports. The robot does not care what the numbers mean — a homelab, a CI
pipeline, a freezer temperature, whose turn it is to be on call. It cares that
the level is honest.

🔴 **Read-only.** Expose nothing from a status server that changes anything. He
has no confirmation step, and this endpoint is polled by a machine that cannot be
asked "are you sure".

Because it also answers `tools/list`, the same URL can go in the server's
[MCP config](mcp.md) — then the model can *answer questions* about what the ring
is already showing, and you have written one small server instead of two.

---

## When it does not work

`idf.py monitor` and look for the `McpStatus` tag. Every failure says which.

| what you see | what it is |
|---|---|
| nothing at all from `McpStatus` | no URL stored. `STATUS_SHOW` to confirm, and reboot after the first one |
| `connect failed` | wrong address, wrong port, or a firewall. Check from another machine first — the port has to be reachable *from the robot*, and a server bound to `127.0.0.1` never is |
| `HTTP 401` / `403` | token missing, wrong or revoked |
| `HTTP 404` | usually the path. The MCP endpoint is normally `/mcp` |
| `server returned an error - no tool named …` | `STATUS_TOOL` does not match what that server offers |
| `no text content in the reply` | the server answered, but not with MCP's `{"content":[{"type":"text"…}]}` envelope |
| `level "…" is not ok/warn/alert` | exactly what it says. The ring stays muted until it is one of the three |
| ring stays lavender | lavender is "no source and nothing read yet" — it is not a status colour |
| ring goes muted after a while | stale: no successful reading for 12 minutes |
