# stackchan-local-llm

**Your StackChan, your language model, your network.**

Firmware and a local server kit that turn an M5Stack StackChan into a voice assistant running entirely on
your own machine: speech recognition, a local LLM, text-to-speech and the robot's face, head, lights and
camera — with nothing sent to a cloud service.

By Drax and Claude. Built with [Claude Code](https://claude.com/claude-code).

> **Working, on real hardware, daily.** Voice, face, head, LED ring, camera, wake word, an on-screen
> settings menu and the privacy switches are all in use on the reference robot.
> **[Flash it from your browser](https://drakthedude.github.io/stackchan-local-llm/flash/)** — no toolchain, nothing to install — or build it
> yourself from this tree.
>
> Verified on **one** StackChan. If you have one, reports from a second are the most useful thing you
> could contribute. See [docs/roadmap.md](docs/roadmap.md).

## What it does

| | |
|---|---|
| **Talks to you, locally** | wake word on the robot; speech-to-text, model and speech synthesis on a machine you own. Any OpenAI-compatible endpoint with tool calling — Ollama by default, llama.cpp for speed |
| **Has a face** | drawn rather than played back: it blinks, holds your gaze, squints, reacts while he thinks and speaks, and the head moves with him |
| **Uses tools** | [MCP](https://modelcontextprotocol.io) is the contract. Point him at Home Assistant or anything else with an MCP server, and an ambient status can drive his LED ring and idle screen |
| **Takes photos** | metered and tone-mapped on the device, shown on his own screen. There is no cloud vision path, by construction |
| **Can be switched off** | microphone and camera have real switches in the settings menu. The mute closes the input device — the wake word stops too — and survives a reboot |

## Start here

| | |
|---|---|
| **[Flash it from your browser](https://drakthedude.github.io/stackchan-local-llm/flash/)** | Chrome or Edge, a USB-C data cable, about two minutes. **Back up the factory firmware first** — the flasher cannot do that for you |
| **[Quickstart](docs/quickstart.md)** | factory robot → local robot, the whole path including the server. Back up first; that step is not optional |
| **[What you gain and lose](docs/stock-vs-local.md)** | honestly, against the firmware it shipped with. He will not dance any more |
| **[Check the privacy claim](docs/privacy.md)** | six checks you can run yourself, and what this does *not* protect you from |
| **[Roadmap](docs/roadmap.md)** | what is done, what is next, and what is still verified on only one robot |

### Feels like a lot?

It is — embedded firmware, Docker, a local model and a bit of networking, all at once. **You do not
have to hold all of it.** [Doing this with an AI assistant riding along](docs/with-an-assistant.md)
covers opening this repo in Claude Code or VS Code and letting it carry the parts you do not care
about. The docs are written to be read by both of you, and that page also tells the assistant which
steps it must *not* take on your behalf.

## Why

The StackChan's factory "AI agent" firmware sends your voice to a remote service, with an always-on
microphone and a camera pointed at your room. The XiaoZhi stack it is built on is open source and can be
self-hosted — but not on this robot out of the box, and not in English without work.

If you own one and have not thought about this, the most useful thing on this site is not our
comparison table — it is [the `strings` one-liner](docs/privacy.md#1-read-every-url-in-the-firmware)
pointed at your own factory backup. Look at what your robot dials before you decide whether to change it.

## Built on

- [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) — ESP32 voice assistant firmware (MIT)
- [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) — the voice pipeline server (MIT)

Licensed MIT — see [LICENSE](LICENSE), which also carries the upstream notices.
