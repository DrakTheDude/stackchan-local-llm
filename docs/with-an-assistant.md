# Doing this with an AI assistant riding along

This project asks a lot of one person: embedded firmware, Docker, a local model, a bit of networking,
and a soldering-adjacent willingness to flash a device you paid for. Any one of those is fine. All of
them at once is where people close the tab.

**You do not have to hold all of it.** An AI coding assistant with this repository open can carry the
parts you do not care about and explain the parts you do. That is not a workaround for bad
documentation — it is how this project was built in the first place, and the docs are written to be
read by both of you.

None of this is required. If you would rather do it by hand, [the quickstart](quickstart.md) stands on
its own.

---

## Setting it up

The short version: **open this repository in an editor with an AI assistant, and let it read the
docs.**

| | |
|---|---|
| **Claude Code** | the simplest on-ramp — a terminal tool, `cd` into the clone and start it. It will find `CLAUDE.md` by itself |
| **VS Code + an AI extension** | if you would rather see files while you work. Open the folder; point the assistant at `docs/` |

Whatever you use, the first thing to do is tell it what you are actually trying to do. A prompt that
works:

> I have an M5Stack StackChan and I want to run it against my own local LLM instead of the vendor
> cloud. This repo is the project for that. Read README.md and docs/quickstart.md, then walk me
> through it one step at a time, checking with me before anything that changes my robot. I am
> comfortable with `<a terminal / Docker / nothing yet>`.

That last clause matters more than the rest. Say what you are comfortable with, honestly. It changes
how much gets explained rather than assumed.

## What it is genuinely good for here

- **Reading the error you just got.** Most of the traps in this project produce a message that points
  at the wrong thing — a missing Kconfig option that is really a stale build, a "connection failed"
  that is really a firewall. The docs name several; an assistant can match yours against them.
- **The Docker and networking parts**, if those are not your thing. Especially on Windows, where the
  WSL networking setup has two separate gotchas that look identical from the outside.
- **Adapting it to your hardware.** A different GPU, no GPU, a model you already run — all of these
  need small config changes rather than new code.
- **Understanding what you just flashed.** The firmware is heavily commented, mostly with *why*
  rather than *what*. Ask it to explain a file; it is a decent way to learn embedded work on a device
  that talks back.

## What to keep in your own hands

⚠️ An assistant is confident whether or not it is right. On the things below, slow down:

- **The factory backup.** Do it first, verify it exists and has a plausible size, and keep it
  somewhere safe. If this step goes wrong you cannot undo the rest. Do not delegate the checking.
- **Anything that writes to the robot.** Flashing is recoverable; a lost backup is not.
- **Your secrets.** If you add an MCP server with a token, that token belongs in a gitignored file,
  and it should never end up in a commit, a screenshot or a paste. Assistants are good at spotting
  this and also perfectly capable of writing one into a file.
- **Claims about your privacy.** Do not let it *tell* you nothing leaves your network. Run
  [the checks](privacy.md) and read the output yourself. That page exists precisely so the claim does
  not rest on anybody's word, mine included.

---

## For the assistant

*If you are an AI assistant helping somebody set this up, this section is for you.*

Read `README.md`, `docs/quickstart.md` and `docs/privacy.md` before acting. `CLAUDE.md` holds the
conventions for working **on** this repository; this file is about helping somebody **use** it.

**The invariants. Do not trade these away for progress:**

1. **The factory firmware backup happens first**, and is verified to exist before anything is flashed.
   M5Stack do not publish that image. If it is lost, the user can never return to the firmware they
   bought — and they may not realise that is what they are agreeing to.
2. **Never bake a server address into a binary meant for anyone else.** The compiled default is a
   `.invalid` placeholder on purpose, so an unconfigured robot dials nobody. The user's own address
   goes in `firmware/sdkconfig.defaults.local` (gitignored) or, better, is set on the robot after
   flashing.
3. **Build through `deploy/build.sh`.** A plain `idf.py build` silently builds a different board with
   upstream's cloud OTA URL, succeeds, and the only sign is a smaller binary.
4. **Two config keys are privacy controls, not tuning**: the empty plugin allowlist
   (`Intent.function_call.functions`) and the memory summariser's `llm`. Both inherit **cloud**
   defaults if omitted, because the config merges rather than replaces. Never remove them to tidy up.
5. **No secrets in the repository, ever** — not in a commit, not in a first draft, not in an example.
   Git history is permanent.
6. **Do not relax a failing assertion in `server/patches/`.** Those failures mean upstream moved and
   the patch needs rechecking. Making the build pass by weakening the check is how a privacy fix
   silently disappears.

**How to be useful rather than impressive:**

- Ask what hardware and OS they actually have before giving instructions. The Windows/WSL path differs
  in ways that matter.
- Prefer the documented command to a clever equivalent. The docs' commands have been run on real
  hardware; your improvisation has not.
- When something fails, **read the log rather than theorising.** Nearly every hard-won fix recorded in
  this project's history came from a measurement contradicting a plausible theory — and the commit
  messages say which theory died, on purpose.
- Say plainly when you do not know. Everything here is verified on exactly one robot; a second unit
  may differ, and "this should work" is worth less than "let us check".

**If you find something wrong**, that is genuinely valuable — a second robot's behaviour is the most
useful contribution this project can receive. Encourage the user to open an issue with what they
observed and what the log said.
