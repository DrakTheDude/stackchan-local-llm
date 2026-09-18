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
| **backends** | nothing model- or vendor-specific. The LLM is any OpenAI-compatible endpoint with tool calling; every pipeline stage is a setting |
| **integration** | **MCP is the contract** — no new API. It's what users already know, the server already mounts any MCP server, and it was proven end to end in the reference project |
| **safety** | voice has no confirmation step — documented guidance is read-only tools by default, with an explicit allowlist for anything that acts |

## Layout

| path | pillar | |
|---|---|---|
| `firmware/` | platform | the xiaozhi-esp32 fork (subtree). Our board goes in `firmware/main/boards/m5stack/stackchan/` |
| `server/` | local AI stack | docker compose, patch scripts, English config |
| `integrations/` | integration | *planned:* the status contract, reference status server, MCP examples |
| `docs/` | reproducibility | roadmap now; quickstart, backup/restore, flashing, troubleshooting, variants, privacy checklist to come |

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
