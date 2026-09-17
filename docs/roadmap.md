# Roadmap

Taking a working, hardware-verified private build and making it something any StackChan owner can
install. No deadline — done properly beats done fast.

Status key: ⬜ not started · 🟡 in progress · ✅ done

---

## 1. Foundation 🟡

- ✅ Repository scaffold, project brief, publishability rule
- ✅ Import upstream `xiaozhi-esp32` at `66bf9f7` as a squashed subtree under `firmware/` — tree hash
  verified identical to upstream (`b940d7e`)
- ⬜ Translate upstream's Chinese firmware comments to English (~233 lines, 34 files — measured)
- ⬜ Port the StackChan board from the reference project, **with personal and homelab specifics removed
  as it comes in** — never committed first and cleaned later

## 2. Make it anyone's ⬜

- ⬜ **Server address configurable after flashing.** The biggest technical item, and the one prebuilt
  binaries depend on. Today the OTA URL is compiled in and upstream has no on-device setting, so a
  downloaded `.bin` can't know the owner's server. Likely a field on the Wi-Fi setup page the robot already
  serves on first boot, stored in NVS, with the compiled value only as a default. A good upstream candidate.
- ⬜ Generic persona — a desk companion, not a homelab operator.
- ⬜ **Optional** MCP health source: poll any MCP endpoint for a status and drive the LED ring and idle
  screen from it. Off by default. The reference project's homelab integration becomes one example.
- ⬜ Wake phrase: "Hi, Stack Chan" (`wn9l_histackchan_tts3`) — the robot's own name.

## 3. Server kit ⬜

- ⬜ `docker compose` stack: llama.cpp + Whisper + Kokoro + xiaozhi, with the model as a setting
- ⬜ Asserted patch scripts, carried over: the English pass (prompts, few-shot examples, tool schemas,
  spoken fallbacks, memory summariser, sentence splitter) and the upstream bug fixes
- ⬜ Our own fully English, commented `config.yaml` — with the privacy-relevant keys set explicitly
- ⬜ Log glossary: the server logs in Chinese; document the lines that matter
- ⬜ **Measure an honest hardware floor.** Only a 32B model on a 24 GB GPU has been verified, and tool
  calling is exactly what small models get wrong. Test 14B and 8B before stating a minimum.
- ⬜ Non-NVIDIA / CPU-only notes, if anything works acceptably

## 4. Safe to flash ⬜

- ⬜ Factory firmware backup as the **first** step, and the restore path
- ⬜ Brick recovery
- ⬜ Plain statement of what was verified, and on how many units. Servo calibration is known to be per-unit.
- ⬜ Hardware revision notes as reports come in

## 5. Release ⬜

- ⬜ Automated firmware builds (app + assets) and versioned releases
- ⬜ License (MIT), with upstream notices preserved
- ⬜ Quickstart: flash → start server → "Hi, Stack Chan"
- ⬜ Make the repo public

## 6. Upstream ⬜

- ⬜ `xiaozhi-esp32-server`: the TTS sentence splitter missing the ASCII full stop; `.rstrip()` eating the
  spaces between streamed chunks. Both are plain bugs with measured reproductions.
- ⬜ `xiaozhi-esp32`: the StackChan board; a runtime-configurable server address
- ⬜ Possibly: English logging / i18n for the server

---

## Open questions

- ✅ **Public repo name: `stackchan-local-llm`.** "Stack-chan" is, as far as we know, the name of the
  original open-source robot project by Shinya Ishikawa that M5Stack's product builds on, so a bare
  `stackchan` could read as the official one. This says what the project is and can't be mistaken for it.
- **License copyright holder.** MIT needs a named holder; attribution ("by Drax and Claude, built with
  Claude Code") can say more than the copyright line.
- **Other upstream boards.** `firmware/main/boards` is ~3.5 MB of boards that aren't StackChan. Pruning
  shrinks the fork but makes every upstream merge conflict-prone. Keep for now.
- **Upstream base.** `66bf9f7` is six weeks behind upstream. Bump before release, or after the port works?
