# Giving him tools

He can use [MCP](https://modelcontextprotocol.io) servers. That is what turns a robot you talk to
into a robot that does something — and it is one JSON file.

Read [tool-safety.md](tool-safety.md) first if you are about to connect anything that changes the
world. The short version: **a misheard sentence must not be able to unlock a door.**

---

## The file

`~/xiaozhi-data/.mcp_server_settings.json`, mounted into the server at
`data/.mcp_server_settings.json`. It is read at connection time, so a change takes effect on the next
wake — no restart.

```json
{
  "mcpServers": {
    "notes": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/you/notes"]
    }
  }
}
```

That is the whole shape. Each key under `mcpServers` is a name you choose; it appears nowhere except
your logs, so make it something you will recognise at four in the morning.

> 🔴 **If the file does not exist, nothing happens and nothing complains** beyond one line in the log.
> A robot with no tools behaves exactly like a robot whose tool file has a typo in it.

---

## The three transports

Which one you get is decided by **which keys you use**, not by a `type` field.

### stdio — the server runs as a child process

```json
{
  "mcpServers": {
    "notes": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/data/notes"],
      "env": { "SOME_TOKEN": "..." }
    }
  }
}
```

Presence of `command` selects stdio. `npx` is special-cased and resolved on `PATH`; anything else is
run as given.

⚠️ **The process runs inside the server container**, not on your host. If the command is not in that
image it will not be found, and a path in `args` is a path *in there* — which is why the example
above says `/data/notes` rather than a host path. Mount what it needs.

### streamable HTTP — a server you can reach over the network

```json
{
  "mcpServers": {
    "house": {
      "url": "https://10.0.0.5/mcp",
      "transport": "streamable-http",
      "timeout": 30,
      "headers": { "Authorization": "Bearer ..." }
    }
  }
}
```

`"http"` is accepted as a synonym. This is the one the reference robot uses.

### SSE — the default when `url` is present and `transport` is not

```json
{
  "mcpServers": {
    "house": { "url": "https://10.0.0.5/sse" }
  }
}
```

> 🔴 **`transport` defaults to `sse`, so omitting it against a streamable-HTTP server fails in a
> confusing way** — the connection is made, the handshake is not, and the tools simply never appear.
> If a server you can `curl` produces no tools, this is the first thing to check.

---

## Credentials

Put them in `headers`, not in the old `API_ACCESS_TOKEN` field — that still works and logs a warning
telling you to move it.

```json
"headers": { "Authorization": "Bearer your-token-here" }
```

🔴 **This file holds secrets. Treat it as one:**

- `chmod 600` it, keep it out of any repository, and out of cloud sync.
- **Scope the credential at the source.** If the server authenticates to something else, give it the
  least it can do the job with — a read-only token, not your own account. The robot cannot tell a
  narrow token from a wide one, and a misheard sentence reaches whatever the token reaches.

---

## Checking it worked

The server lists every function it offers the model on each connection:

```bash
docker compose logs xiaozhi | grep '当前支持的函数列表'
```

Your tools appear there by name. If they do not:

| what you see | what it usually is |
|---|---|
| no mention of your server at all | the file is not where the server looks, or is not valid JSON |
| the server connects, no tools appear | `transport` wrong — see the SSE default above |
| tools appear, the model never calls them | a naming problem, not a wiring one. See below |
| `command not found` | the command is not in the server's image, not missing from your host |

---

## Making the model actually use them

Wiring a tool up is the easy half. The model has to *choose* it, from a name and a sentence.

**The description is the interface.** It is the only thing the model sees at the moment it decides,
and it is read as instructions rather than documentation. On this project a tool description that
said what a field *was* got the field recited aloud; saying what to *do* with it fixed it.

**Anything a tool returns can be spoken.** The result goes back to a model whose output reaches a
text-to-speech engine — so do not return anything you would not want read out. This robot has, at
various points, read a diagnostic blob, a fabricated URL and a JSON envelope aloud one character at a
time.

**Return refusals as normal results with a reason.** "I can't do that while the door is locked" is a
sentence he can say. An exception becomes a sentence about a failure nobody can act on.

⚠️ **Small models pick badly.** On the reference robot, "tell me a story" once produced a call to the
camera tool. Fewer, clearer tools beat more, similar ones — and a tool whose name and description
overlap another's will be chosen by coin toss.

---

## Worked example: the robot's own tools

The firmware exposes fourteen of its own — head, LED ring, camera, screen, volume, battery — over the
same protocol, in the other direction. They are documented in
[robot-tools.md](robot-tools.md), and they are worth reading before writing your own: they are the
ones this project has actually tuned the descriptions of.
