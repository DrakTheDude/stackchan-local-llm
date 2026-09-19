# Giving him tools, safely

He can use [MCP](https://modelcontextprotocol.io) servers, which is the point of him. This page is
about what to connect and what not to, and it is short because the rule is short.

> **A misheard sentence must not be able to unlock a door.**

---

## Why this robot is different from a chat window

Three properties stack up, and each is harmless alone:

1. **There is no confirmation step.** In a chat window a destructive suggestion is a message you read
   before acting on. Here the model's decision *is* the action. Nothing sits between "I think you
   asked me to" and it happening.
2. **The input is a microphone in a room.** Speech recognition mishears, and it mishears most on the
   short commanding sentences that tool calls come from. It also hears the television, a phone call,
   a guest, and anyone outside an open window.
3. **The model is small.** A 12B is a good size for a desk robot's personality and a modest one for
   judgement. On this project a model has already invented a tool name unprompted and read its own
   JSON aloud, and answered a question about a photograph with confidently fabricated data.

None of those is a flaw to be fixed. They are what a voice robot *is*. So the safety has to sit in
what you connect, not in the robot being careful.

## The rule

**Read-only by default. Anything that changes the world goes on an explicit allowlist, one tool at a
time, chosen by you.**

Not "avoid dangerous servers" — most MCP servers are a mix, and the dangerous tool is usually next to
the one you wanted. Mount the server, then decide which of its tools he may call.

| category | examples | default |
|---|---|---|
| **Read** | status, weather, calendar, "is the washing machine done", search | fine |
| **Change, reversible** | lights, a playlist, a scene, a note | consider it, per tool |
| **Change, irreversible or physical** | locks, garage doors, heating, payments, deletion, anything with a motor | **no** |

The middle row is where judgement lives. "Turn the lights off" is recoverable and delightful. "Turn
the heating off" is recoverable and could still freeze a pipe while you are away.

## The thing that bit us, so you can check for it

Hiding a tool is not the same as disabling it.

Upstream's firmware marks several device tools "user only", which keeps them out of the `tools/list`
response — and then the handler that executes a call looks the tool up **by name** and runs it, with
no check that it was allowed to be listed. The whole protection was that the name was not advertised.

That is fine on a device with a confirmation step and a trusted operator. It is not fine here, so this
firmware **removes** them rather than hiding them:

| removed | what it could do from a string |
|---|---|
| `self.upgrade_firmware` | fetch firmware from a URL, flash it, reboot — code execution on the device |
| `self.assets.set_download_url` | the same, one step back |
| `self.screen.snapshot` | capture the screen and upload it to a URL |
| `self.screen.preview_image` | fetch a URL and display it |

**When you evaluate an MCP server, ask the same question**: not "what does it advertise", but "what
will it do if something asks for a tool by name". A server that relies on not mentioning a capability
is relying on the model never guessing it, and models guess.

## What he ships with

Fourteen tool names, all local to the robot: his head, his LED ring, the camera, screen brightness and
theme, speaker volume, battery and system info, and `self.reboot`. No tool in the firmware reaches the
network on its own.

`claims.ini` asserts the removed four stay out of the built binary, because a subtree update would put
them back and nothing else would notice — the robot would behave identically and every test would pass.

## Practical setup

- **Start read-only and live with it for a week.** You will find out what you actually ask him, which
  is rarely what you expected.
- **Prefer a narrow server over a broad one.** A server exposing four tools you chose beats one
  exposing forty where you use four.
- **Use a scoped credential**, and scope it at the source. If the server authenticates to something
  else, give it the least it can do the job with — a read-only token, not your own account.
- **Put irreversible things behind something with a confirmation step**, not behind a prompt
  instruction. On this project an instruction has lost to a demonstration five separate times; a
  prompt that says "always confirm before deleting" is a preference, not a control.
- **He is on your network.** Everything he can reach, anything that can talk to him can reach through
  him.

## If you are writing the server

- Make destructive tools *hard to call by accident*: distinct names, required arguments without
  defaults, no "do the obvious thing" fallbacks.
- Return refusals as normal results with a reason, not as errors. He will read a reason aloud and the
  person will understand; an error becomes a sentence about a failure nobody can act on.
- Do not put anything in a tool description that you would not want spoken. The result of a tool call
  reaches a text-to-speech engine, and this project has had the robot read a diagnostic blob, a
  fabricated URL and a JSON envelope out loud, one character at a time.
