# stackchan-local-llm

**Your StackChan, your language model, your network.**

Firmware and a local server kit that turn an M5Stack StackChan into a voice assistant running entirely on
your own machine: speech recognition, a local LLM, text-to-speech and the robot's face, head, lights and
camera — with nothing sent to a cloud service.

By Drax and Claude. Built with [Claude Code](https://claude.com/claude-code).

> 🚧 **Early work in progress — not usable yet.** The design is proven on real hardware in a private
> predecessor project; this repo is where it becomes something anyone can install. Follow along in
> [docs/roadmap.md](docs/roadmap.md).

## What it will be

| | |
|---|---|
| **Firmware and platform** | English-first board support for the StackChan — face, head, LED ring, camera, on-device wake word — with every hardware assumption documented alongside a way to check it |
| **Local AI stack** | wake word → speech-to-text → model → text-to-speech, each stage swappable. Bring any OpenAI-compatible model server that supports tool calling |
| **Integration interface** | [MCP](https://modelcontextprotocol.io) as the contract: give the model your own tools (Home Assistant, anything with an MCP server), and let the robot show a status on its ring and screen |
| **Reproducibility** | full English docs: firmware backup and restore, flashing, bring-up, troubleshooting, known board variants — and a path from a factory robot to a local one in about 30 minutes |

Plus a checklist you can run yourself to **verify nothing leaves your network.**

## Why

The StackChan's factory "AI agent" firmware sends your voice to a remote service, with an always-on
microphone and a camera pointed at your room. The XiaoZhi stack it's built on is open source and can be
self-hosted — but not on this robot out of the box, and not in English without work.

## Built on

- [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) — ESP32 voice assistant firmware (MIT)
- [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) — the voice pipeline server (MIT)
