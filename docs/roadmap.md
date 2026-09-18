# Roadmap

Taking a working, hardware-verified private build and making it something any StackChan owner can
install. No deadline — done properly beats done fast.

Status key: ⬜ not started · 🟡 in progress · ✅ done

The project is organised around **four pillars**, with privacy verification running through all of them.
The **critical path** to a usable release is marked 🔑.

---

## Foundation ✅

- ✅ Repository scaffold, project brief, publishability rule
- ✅ Import upstream `xiaozhi-esp32` at `66bf9f7` as a squashed subtree under `firmware/` — tree hash
  verified identical to upstream (`b940d7e`)
- ✅ Name: `stackchan-local-llm`
- ✅ Translate upstream's Chinese firmware comments to English — 244 lines in 35 files, code verified
  unchanged outside comments. Language packs and bilingual brand names kept on purpose.

---

## 1. Firmware and platform ⬜

English-first board support, with every hardware assumption written down next to how to check it.

- 🟡 Port the StackChan board from the reference project, **with personal and homelab specifics removed
  as it comes in** — never committed first and cleaned later. Face, head, LEDs, camera, audio, power and
  wake word ported; investigation instruments removed; the homelab client replaced by a `StatusSource`
  interface with nothing attached. Tested on hardware: voice, face, head, idle dim and the wake word all
  work, and the port found two real bugs that the reference firmware had been surviving by luck (below).
- ⬜ **Diagnose the boot-time I²C glitch, rather than only surviving it.** About ten seconds into every
  boot, as Wi-Fi associates and the wake-word engine starts, the shared bus NAKs for a few hundred
  milliseconds. Both chips on it go unreachable, and two separate faults came out of that one window: a
  silent amplifier (`esp_codec_dev_open` reports success while its register writes go nowhere) and a
  servo rail "failure" that was really an unreadable status register. Both are now handled — the codec
  checks the amp answered and retries, the PY32 refuses to write a guess — but nothing here explains
  *why* the bus stalls. Candidates: the PMIC, contention from the LED latch, or the radio's power draw.
  Worth knowing before telling other people their hardware is fine.
- ✅ **Per-unit servo calibration.** Each robot's factory centre is read from its own NVS at boot —
  the keys `zero_pos_1` / `zero_pos_2`, found by searching the partition rather than by assuming a
  namespace name, since the name belongs to the vendor's app and one sample is not a convention.
  Flashing the app partition leaves NVS alone, so the values are there on every unit. The travel
  limits are now spans around *that* centre, which was the safety part: tilt has only ~90° before a
  mechanical stop. The old constants remain as a fallback that announces itself loudly. The separate
  bench trim is `CONFIG_STACKCHAN_PAN_TRIM`, defaulting to 0 — it is per-unit and guessing makes it
  worse. Verified: read `servo/460/620` on the reference unit, matching its disassembled backup.
- ⬜ 🔑 **Server address configurable after flashing.** Today it's compiled in and upstream has no
  on-device setting, so a prebuilt `.bin` can't know the owner's server. Likely a field on the Wi-Fi setup
  page the robot already serves on first boot, stored in NVS, with the compiled value as a default. This
  blocks prebuilt binaries and the 30-minute path. Strong upstream candidate.
- ⬜ Behaviour, documented: face and expressions, head motion and the thinking pose, LED ring states,
  camera (on-screen only), wake word ("Hi, Stack Chan" — runs on the robot, not the server)
- ⬜ **Hardware assumptions, each with a way to verify it:** I²C device map, servo rail at `0x6F`, servo
  IDs and per-unit calibration, LED chain order, camera sensor. Verified on one unit so far — say so.
- ⬜ Board variants: a diagnostic mode that prints the I²C scan and rail / servo / codec checks, so a report
  from a different unit can be compared against a known-good one
- ⬜ **Tokenise the on-screen styling, then ship a second theme.** Colours, spacing and the face's own
  palette are currently constants spread across `stacky_face.cc`, `stackchan_leds.cc` and
  `InitializeTheme()`. Pulling them into one named set turns "change how he looks" from a hunt into an
  edit, and a second theme proves the tokens are real rather than decorative — the same exercise done on
  the reference project's web UI, where a System 7 theme was what shook out the values that had been
  hard-coded. Worth doing because a desk robot people own is a thing they will want to restyle.

## 2. Local AI stack ⬜

Wake word → STT → model → TTS, with every backend a setting rather than a choice made for you.

- ⬜ `docker compose` stack with each stage replaceable: VAD, STT, LLM, TTS
- ⬜ **LLM: any OpenAI-compatible endpoint with tool calling.** Test and document at least llama.cpp,
  Ollama and LM Studio, including each one's tool-calling template requirements.
- ⬜ **Measure an honest model floor.** Only a 32B on a 24 GB GPU is verified, and tool calling is exactly
  what small models get wrong. Test ~8B and ~14B on real tasks and publish the results, failures included.
- ⬜ STT: English-only Whisper by default. The upstream provider never sends a language, so multilingual
  models drift into other languages on short utterances — document it, or patch it.
- ⬜ TTS: Kokoro by default. Voice choice documented as a **loudness** decision on a 1 W speaker, with the
  measurement script.
- ⬜ Server patch kit carried over, every replacement asserted: English pass (prompts, few-shot examples,
  tool schemas, spoken fallbacks, memory summariser, sentence splitter) and upstream bug fixes
- ⬜ Our own fully English, commented `config.yaml`, **privacy-relevant keys set explicitly** — config
  merges over upstream defaults, and some of those defaults are cloud services
- ⬜ Generic persona; local memory; a log glossary for the Chinese server logs
- ⬜ CPU-only / non-NVIDIA notes, if anything is acceptably fast

## 3. Integration interface ⬜

**Decided: MCP is the contract — no new API.** It's what most users will already know, the server already
mounts any MCP server as tools for the model, Home Assistant ships an official MCP server, and the reference
project proved the path end to end.

- ⬜ **Tools for the model:** how to add an MCP server (stdio / SSE / streamable HTTP), with worked
  examples — Home Assistant, a notes or files server, a custom one
- ⬜ **Ambient status contract:** the small tool shape the robot polls to drive its ring, idle screen and
  spoken alerts — e.g. a level (`ok` / `warn` / `alert`), a one-line summary, optional cards. Off by default.
- ⬜ **Reference status server** — a few dozen lines, so the ring and idle screen can be tried with no
  homelab. The reference project's homelab integration becomes one example, not a requirement.
- ⬜ **The robot's own tools**, documented as an API: head, LED ring, camera, test hooks
- ⬜ 🔴 **Safety guidance.** Voice has no confirmation step, and speech recognition mishears. Recommend
  read-only tools by default and an explicit allowlist for anything that changes the world — a misheard
  sentence must not be able to unlock a door.
- ⬜ 🔴 **Audit the upstream device tools the model can call.** The firmware compiles in, among others,
  `self.upgrade_firmware` and `self.assets.set_download_url` (both fetch from a URL), `self.reboot`,
  `self.screen.snapshot` and `self.screen.preview_image`. Decide which a voice-driven local robot should
  expose, and remove or gate the rest.

## 4. Reproducibility ⬜

Full English docs, and a path from a factory device to a local robot in about 30 minutes.

- ⬜ 🔑 **Quickstart: factory device → local robot in ~30 minutes.** Conditions stated up front: excludes the
  model download (a 32B is ~20 GB), assumes Docker is installed, defaults to a smaller model.
- ⬜ 🔑 **Factory firmware backup as the first step**, verified, plus the restore path. Backups contain Wi-Fi
  credentials — say where *not* to keep them.
- ⬜ Flashing on Windows / macOS / Linux. Investigate a **browser flasher** (ESP Web Tools on GitHub Pages)
  so owners can flash from the Releases page without installing anything.
- ⬜ Building from source: ESP-IDF version, the `SDKCONFIG_DEFAULTS` requirement, pre-flash checks
- ⬜ Hardware bring-up guide
- ⬜ Troubleshooting: symptom → cause → fix, generalised from the reference project
- ⬜ Known board variants, maintained from reports
- ⬜ Automated firmware builds and versioned releases (app + assets)
- ⬜ License (MIT) with upstream notices preserved; "by Drax and Claude, built with Claude Code"

## Privacy you can verify ⬜

The reason the project exists, so it gets a page of its own rather than a promise.

- ⬜ A checklist an owner can run themselves: scan the firmware for URLs, confirm the cloud vision path is
  disabled, confirm the server's built-in plugin allowlist is empty, confirm memory summarisation is local,
  read the patch assertions
- ⬜ What the factory firmware sends, and where, as far as can be established

## Upstream ⬜

- ⬜ `xiaozhi-esp32-server`: the TTS sentence splitter missing the ASCII full stop; `.rstrip()` eating the
  spaces between streamed chunks. Both have measured reproductions.
- ⬜ `xiaozhi-esp32`: the StackChan board; a runtime-configurable server address
- ⬜ `xiaozhi-esp32`: 14 language packs start `ACCESS_VIA_BROWSER` with a Chinese full-width comma
  (fixed here already)
- ⬜ `xiaozhi-esp32`: `I2cDevice` aborts the whole device on one flaky I²C transfer (fixed here already)
- ⬜ `xiaozhi-esp32`: CoreS3 trusts `esp_codec_dev_open`, which returns success when the amplifier is
  unreachable — the robot then plays every reply into a chip that is not listening, silently. Affects the
  stock CoreS3 board, not just this one; it only looks intermittent because a boot chime sometimes
  happens to retry the open at the right moment (fixed here already)
- ⬜ Possibly: English logging / i18n for the server

## Release ⬜

- ⬜ Private GitHub repo while the port is in progress
- ⬜ Public once the quickstart works end to end on a factory unit

---

## Suggested order

Foundation → board port → 🔑 runtime server address → compose stack with swappable backends → model floor →
integration contract and reference server → 🔑 quickstart with backup/restore → privacy checklist → public
release. Upstream PRs whenever a piece is solid.

---

## Open questions

- ✅ **Public repo name: `stackchan-local-llm`.** "Stack-chan" is, as far as we know, the name of the
  original open-source robot project by Shinya Ishikawa that M5Stack's product builds on, so a bare
  `stackchan` could read as the official one. This says what the project is and can't be mistaken for it.
- ✅ **Integration contract: MCP.** Familiar to users, already supported by the server, proven in practice.
- **License copyright holder.** MIT needs a named holder; attribution can say more than the copyright line.
- **Other upstream boards.** `firmware/main/boards` is ~3.5 MB of boards that aren't StackChan. Pruning
  shrinks the fork but makes every upstream merge conflict-prone. Keep for now.
- **Upstream base.** `66bf9f7` is six weeks behind upstream. Bump before release, or after the port works?
- **Browser flasher.** Can ESP Web Tools take the factory backup first? If not, the quickstart needs a
  second tool for that step.
