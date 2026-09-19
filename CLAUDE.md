# stackchan-local-llm — project brief

**Local-LLM firmware and server for the M5Stack StackChan.** The robot talks to a voice pipeline and a
language model running on the owner's own machine, and nothing leaves their network.

By Drax and Claude. Built with Claude Code.

🚧 **Early work in progress.** Nothing here is usable yet; see [docs/roadmap.md](docs/roadmap.md).

---

## Where this comes from

This is the clean, public version of a private predecessor that made one StackChan a local voice
front-end for a specific homelab. **Almost everything hard was solved and verified on hardware there**:
the servo rail, the LED ring, the camera, the English passes over the server, memory, latency.

**Port from it; don't copy it.** It is the reference implementation and its commit messages are the
record of what was measured. But it is full of one person's setup, and none of that belongs here.

That repo is private and stays private. It is not a place to send anyone, and its path on the
maintainer's machine is deliberately not written down here — an instruction to go and read something
nobody else can open is worse than no instruction.

## 🔴 Hard rule: everything committed here must be publishable

This repo will be public, and **git history is forever**. A secret or personal detail removed in a later
commit is still in the history. So nothing enters a commit that couldn't be public — including first
drafts, comments, examples, and test fixtures:

- no LAN IP addresses, hostnames, Wi-Fi network names or MAC addresses — use placeholders like
  `192.168.x.y` / `your-server`
- no tokens, keys or passwords, not even ones that look fake but have a real shape
- no references to the maintainer's homelab or to Agent Drax as a *requirement*. An MCP health source may
  be an optional feature; Drax may appear only as one example of it
- no factory firmware dumps, NVS backups (they contain Wi-Fi credentials) or disassembly listings.
  Register maps and measured behaviour are fine to publish
- no personal names in code or comments

**Before every commit, scan the diff for all of the above.**

## Decisions already made

| | |
|---|---|
| **firmware** | fork of `78/xiaozhi-esp32`, brought in with `git subtree --squash` under `firmware/`, with the StackChan board inside it. Prebuilt `.bin` releases for owners who won't install ESP-IDF |
| **server** | **not** a fork. A pinned upstream `xiaozhi-esp32-server` image plus asserted patch scripts, our own English `config.yaml`, and a log glossary |
| **language** | all firmware comments in English — **translate** upstream's Chinese, keep the reasoning. Don't strip comments |
| **attribution** | "by Drax and Claude", built with Claude Code |
| **upstream** | pinned on purpose. Bumping is a deliberate step, never a side effect |
| **structure** | four pillars — firmware/platform, local AI stack, integration interface, reproducibility — plus verifiable privacy. See [docs/roadmap.md](docs/roadmap.md) |
| **backends** | nothing model- or vendor-specific. The LLM is any OpenAI-compatible endpoint with tool calling; every pipeline stage is a setting. Which models actually work is measured, not assumed — [model floor](docs/model-floor.md) |
| **integration** | **MCP is the contract** — no new API. It's what users already know, the server already mounts any MCP server, and it was proven end to end in the reference project |
| **safety** | voice has no confirmation step — documented guidance is read-only tools by default, with an explicit allowlist for anything that acts |

## Layout

| path | pillar | |
|---|---|---|
| `firmware/` | platform | the xiaozhi-esp32 fork (subtree). Our board goes in `firmware/main/boards/m5stack/stackchan/` |
| `server/` | local AI stack | docker compose, patch scripts, English config |
| `integrations/` | integration | *planned:* the status contract, reference status server, MCP examples |
| `docs/` | reproducibility | [roadmap](docs/roadmap.md), [quickstart](docs/quickstart.md), [privacy checklist](docs/privacy.md), [stock vs local](docs/stock-vs-local.md), [characters](docs/characters.md), [model floor](docs/model-floor.md), [releasing](docs/releasing.md). Backup/restore and board variants still to come |
| `tools/model-bench/` | reproducibility | measures whether a model can call tools, how fast it speaks and what it costs in VRAM. `sweep.sh` runs the lot against any Ollama. Results are committed: they are the evidence behind the model floor, and the table is generated from them |
| `docs/flash/` | reproducibility | the browser flasher. Published by CI on a tag, from the binaries that tag built — never committed, so the page and the firmware cannot drift |

### Updating the upstream firmware base

```bash
git subtree pull --prefix=firmware --squash https://github.com/78/xiaozhi-esp32.git <rev>
```

Current base: **`66bf9f7`** (v2.4.1, 2026-08-04) — the revision the reference board was verified against.

---

## Carried over from the reference project — each of these cost real time

**Firmware**
- **Regenerating `sdkconfig` needs `-DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.<board>'`.**
  Without it the build silently produces a different board with upstream's **vendor-cloud OTA URL**, and
  reports success. Check the board type and OTA URL in `sdkconfig` before every flash.
- **Never `ESP_ERROR_CHECK` an I²C call.** The shared bus NACKs under load; an abort reboots the robot.
- **No LVGL, `std::string` or `std::vector` work on the `esp_timer` task** — 3584-byte stack shared with the
  LED animation and touch poll.
- **`lv_timer` callbacks already hold the LVGL lock.** Don't take it again inside one.
- **The wake-word model is on the assets partition**, and its build rule ignores the SR config. Delete
  `build/generated_assets.bin` and flash assets after changing `CONFIG_SR_WN_*`. Exactly one `SR_WN_*` enabled.
- **The wake phrase is sent to the model as the first user message.** Choose it for what it says.
- 🔴 **The camera streams from boot, not from when you ask for a photo.** `VIDIOC_STREAMON` runs at
  init, so the sensor is filling PSRAM over DMA permanently — 3 MB/s at QVGA. Switching to the VGA
  mode for the light (its rows are twice as long, so the same exposure collects twice as much) takes
  that to 9.8 MB/s, the wake-word engine shares the bus, and **voice detection goes spotty within
  minutes**. It reads as a microphone fault. Fix streaming first, then the picture.
- 🔴 **Offering a tool-calling model NO tools does not stop it calling one.** Suppressing tools on the
  wake turn made it invent `get_welcome_message`, write the call as prose, and the robot read the JSON
  aloud. Upstream's `DIRECT_ANSWER_TOOL` exists for exactly this — it turns "call a tool or not" into
  "which tool". Give the model one legitimate thing to reach for rather than nothing.
- 🔴 **Reasoning is the biggest latency term in the stack, and the usual way to switch it off does not
  work.** `/no_think` in a prompt does nothing here — in the system prompt it makes the model think
  *more* — and `think: false` on an OpenAI-compatible endpoint is accepted and ignored. Only Ollama's
  own `/api/chat` honours it. The server currently does the first of these and believes it worked.
- **Flash from native Windows**, not WSL — RTS/DTR don't survive usbip.

**Server**
- **Config merges over upstream defaults.** A key you leave out silently inherits upstream's value — which
  for the memory summariser's LLM is a *cloud* API. Set privacy-relevant keys explicitly.
- **Never set a vision (VLLM) API key** — the default endpoint is cloud; photos stay on the robot.
- **Empty the built-in plugin allowlist** — weather, news and web search call third-party APIs.
- **A demonstration beats an instruction.** Chinese few-shot examples beat every English rule; a realistic
  example schema got copied into real memory. Check what the model is *shown*.
- **Every patch asserts.** A replacement that matches nothing must fail the build.
- **One llama.cpp slot** (`--parallel 1`) for a single robot — concurrent requests divide one GPU.

**Working on this machine**
- WSL2 for everything except flashing. Repos live in the WSL filesystem, never under `/mnt/c/`.
- Don't pass `$(...)`, `$VAR` or nested quotes through `wsl.exe -- bash -lc "..."` — the outer shell expands
  them. Write a script file and run it.
- Never leave an unbounded serial capture running; use a bounded one that waits for an event.
