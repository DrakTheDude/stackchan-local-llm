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
> Verified on **one** StackChan, and the model measurements on **one** GPU. If you have either, a
> report from a second is the most useful thing you could contribute — see
> [docs/roadmap.md](docs/roadmap.md), and [tools/model-bench](tools/model-bench) if it is the GPU.

> ⚠️ **Flashing writes to the device, and an interrupted write can brick it.** Most failures are
> recoverable — the ESP32 has a bootloader that survives a bad app image, and M5Burner can restore a
> device that will not start. But a write interrupted at the wrong moment, by a cable nudged out, a
> power cut, a failing USB port or a hub that drops under load, can leave a robot that does not boot
> and does not come back easily. Those are circumstances outside anyone's control, including this
> project's.
>
> **So: back up the factory firmware before you flash anything.** M5Stack do not publish it, and it is
> the only copy you will ever have. Use a cable you trust, a port directly on the machine rather than
> through a hub, and do not unplug anything until the flasher says it has finished.
>
> This software comes with no warranty of any kind — see [LICENSE](LICENSE). You are flashing your own
> hardware at your own risk.

## What it does

| | |
|---|---|
| **Talks to you, locally** | wake word on the robot; speech-to-text, model and speech synthesis on a machine you own. Any OpenAI-compatible endpoint with tool calling — Ollama by default, llama.cpp for speed |
| **Runs on the card you have** | fifteen models measured for tool calling, speed and VRAM, from 5 GB up. [What to run on yours](docs/model-floor.md) |
| **Has a face** | drawn rather than played back: it blinks, holds your gaze, squints, reacts while he thinks and speaks, and the head moves with him |
| **Uses tools** | [MCP](https://modelcontextprotocol.io) is the contract. Point him at Home Assistant or anything else with an MCP server, and an ambient status can drive his LED ring and idle screen |
| **Takes photos, and can describe them** | metered and tone-mapped on the device and shown on his own screen. Give him a [vision model on your own card](docs/vision.md) and he says what he saw; give him none and the photo simply stays on the device. There is no cloud vision path either way |
| **Can be switched off** | microphone and camera have real switches in the settings menu. The mute closes the input device — the wake word stops too — and survives a reboot |

## Start here

| | |
|---|---|
| **[Flash it from your browser](https://drakthedude.github.io/stackchan-local-llm/flash/)** | Chrome or Edge, a USB-C data cable, about two minutes. **Back up the factory firmware first** — the flasher cannot do that for you |
| **[Quickstart](docs/quickstart.md)** | factory robot → local robot, the whole path including the server. Back up first; that step is not optional |
| **[What you gain and lose](docs/stock-vs-local.md)** | honestly, against the firmware it shipped with. He will not dance any more |
| **[Standing up your own model](docs/your-llm.md)** | NVIDIA, Apple Silicon, AMD, or something else entirely — and the one check that tells you whether your GPU is really being used |
| **[Which model do you need](docs/model-floor.md)** | fifteen models measured on one card: tool calling, time to first spoken word, VRAM. Most of our guesses were wrong |
| **[Giving him tools](docs/mcp.md)** | one JSON file, three transports, and what makes a model actually choose a tool |
| **[Giving him tools, safely](docs/tool-safety.md)** | read-only by default, and why hiding a tool is not the same as disabling it |
| **[What he does, and why](docs/behaviour.md)** | the face, the head, the ring, the wake word — and which of them are telling you something is wrong |
| **[Making him speak first](docs/ambient-status.md)** | the ring carries something's health, and a change gets announced without anyone asking. Off until you point him at a server |
| **[Letting him see](docs/vision.md)** | a vision model on your own card, about 4 GB and a third of a second per photo — and what it costs you in the privacy checks |
| **[Check the privacy claim](docs/privacy.md)** | six checks you can run yourself, and what this does *not* protect you from |
| **[What the factory firmware talks to](docs/factory-firmware.md)** | the same scan run against the firmware it shipped with — a hardcoded OTA endpoint, a bare IP, and a call that registers the MAC |
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
