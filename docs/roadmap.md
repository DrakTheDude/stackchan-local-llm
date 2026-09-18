# Roadmap

Taking a working, hardware-verified private build and making it something any StackChan owner can
install. No deadline — done properly beats done fast.

Status key: ⬜ not started · 🟡 in progress · ✅ done

The project is organised around **four pillars**, with privacy verification running through all of them.
The **critical path** to a usable release is marked 🔑.

## Where it stands

**The firmware is done** — not feature-complete, but stable enough that further work is features rather
than fixes. It is in daily use on the reference robot: repeated power cycles with no boot failure and no
lock-up, after a week of chasing intermittent audio faults that turned out to be four separate bugs
around one undiagnosed I²C stall. Both release blockers are closed: each robot reads its own factory
servo calibration, and the server address is set on the robot rather than compiled in.

**Packaging is done too.** Tagged builds are produced by CI, asserted against the chip, the board, the
OTA URL and the *contents* of the assets partition, and published with a
[browser flasher](https://drakthedude.github.io/stackchan-local-llm/flash/) that writes each part at its own offset so a robot keeps its factory servo
calibration. What is left is honest numbers for which models are actually good enough.

⚠️ **All of it is verified on exactly one robot.** Every hardware claim here — the I²C map, the servo
rail, the camera's behaviour, the audio quirks — comes from a single unit. A report from a second one is
worth more to this project than any feature on this list.

---

## Foundation ✅

- ✅ Repository scaffold, project brief, publishability rule
- ✅ Import upstream `xiaozhi-esp32` at `66bf9f7` as a squashed subtree under `firmware/` — tree hash
  verified identical to upstream (`b940d7e`)
- ✅ Name: `stackchan-local-llm`
- ✅ Translate upstream's Chinese firmware comments to English — 244 lines in 35 files, code verified
  unchanged outside comments. Language packs and bilingual brand names kept on purpose.

---

## 1. Firmware and platform 🟡

English-first board support, with every hardware assumption written down next to how to check it.

- ✅ **Port the StackChan board from the reference project**, with personal and homelab specifics removed
  as it came in — never committed first and cleaned later. Face, head, LEDs, camera, audio, power and
  wake word ported; investigation instruments removed; the homelab client replaced by a `StatusSource`
  interface with nothing attached. In daily use, and the port found several real bugs the reference
  firmware had been surviving by luck — a silent amplifier, a deaf microphone, and a `Read()` that
  claimed to have filled a buffer it never touched.
- ✅ **On-screen settings menu**, reached by holding the screen for five seconds: Wi-Fi & server, volume,
  brightness, a self-check, About, and the privacy switches. Full screen, themed, and the only board in
  the tree that gives LVGL a pointer — enabled solely while the menu is open.
- ✅ **Privacy switches.** The microphone switch closes the input device, so the wake word stops too; the
  camera switch refuses at the point of capture. Both persist across a reboot and the mute shows on
  screen, because a mute you cannot see is one you will forget.
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
- ✅ **Server address configurable after flashing.** Mostly already built upstream, which is worth
  recording: `Ota::GetCheckVersionUrl()` reads `ota_url` from NVS with the compiled value as fallback,
  the config portal has the field that writes it, and this tree already enabled the flag that shows it.
  What was genuinely missing was a way back *in* — config mode is otherwise reached only with no saved
  Wi-Fi or on a connect timeout — so changing servers meant breaking the Wi-Fi on purpose. A five-second
  hold on the screen now enters setup. The robot also says "no server set yet" instead of looping a
  connection error, and the setup network is `StackChan-XXXX` rather than upstream's `Xiaozhi-XXXX`.
  **Both shipped**: prebuilt binaries and the browser flasher are live, and this is what made one
  binary serve everybody.
- ⬜ Behaviour, documented: face and expressions, head motion and the thinking pose, LED ring states,
  camera (on-screen only), wake word ("Hi, Stack Chan" — runs on the robot, not the server)
- ⬜ **Hardware assumptions, each with a way to verify it:** I²C device map, servo rail at `0x6F`, servo
  IDs and per-unit calibration, LED chain order, camera sensor. Verified on one unit so far — say so.
- ⬜ Board variants: a diagnostic mode that prints the I²C scan and rail / servo / codec checks, so a report
  from a different unit can be compared against a known-good one
- 🟡 **Finish moving settings onto the robot.** The on-screen menu exists and took the ones that matter
  most — Wi-Fi & server, volume, brightness, the self-check, About, and the privacy switches. What is
  still scattered: the **pan/tilt trim** is a rebuild, and it is the one number every owner has to set for
  their own robot; the ambient **status source** URL and token have no home at all; screen and standby
  timeouts are compiled in.
  The trim is the awkward one — it wants a live preview ("move until he looks straight"), which is a
  different kind of screen from a list of switches.
  ⚠️ A page served on the LAN would be easier to type into, and is a listening socket on a device whose
  selling point is that it does not phone anywhere. If it is ever built, binding, authentication and
  being off by default are deliberate decisions, not afterthoughts.
  The Wi-Fi portal is NOT the place for any of it: its HTML lives in a managed component, so every field
  added there is a fork to maintain. It stays for what it is good at — the things you need *before* the
  robot is on the network.
- ⬜ 🎭 **[Characters — a skin for the whole robot](characters.md).** Not a colour scheme: look, motion,
  light, voice and persona swapped *together*, so a character is a different robot to be in a room with
  rather than a repaint. Design note written; nothing built. The prerequisite is tokenising the look and
  motion constants currently spread across `stacky_face.cc`, `stackchan_head.cc`, `stackchan_leds.cc` and
  `InitializeTheme()` — and the hard part is that a character **spans two machines**, since look and motion
  live on the robot while voice and persona live in server config.

## 2. Local AI stack 🟡

Wake word → STT → model → TTS, with every backend a setting rather than a choice made for you.

- ✅ **`docker compose` stack with each stage replaceable**: VAD, STT, LLM, TTS. One file, one
  `.env`, and a single value that must change — this machine's LAN address.
- 🟡 **LLM: any OpenAI-compatible endpoint with tool calling.** Ollama is the default and llama.cpp
  is a compose profile; both are configured and documented, with LM Studio as a commented third.
  Only llama.cpp is *verified* in daily use. The template requirement is written down because it
  is the usual reason a model "cannot call tools" — llama.cpp needs `--jinja`.
- 🟡 🔑 **[Measure an honest model floor](model-floor.md).** The page exists and is explicit about the
  split: one configuration verified (Qwen3-32B Q4, tool calls reliable, 33–34 tok/s) and the rest
  reasoning rather than measurement. Still to do: actually run ~14B, ~8B and a ~4B and publish what
  happens, failures included. Tool calling is what breaks first, and it breaks silently.
- ✅ **STT: English-only Whisper by default**, with the reason in the config: the provider never sends a
  language, so a multilingual model auto-detects per utterance and short ones are where that fails.
- ✅ **TTS: Kokoro by default**, and the voice documented as a **loudness** decision rather than a taste
  one — with the measurements, and the warning that Kokoro normalises level so choosing by ear on good
  speakers picks wrong for this one.
- ✅ **Server patch kit carried over, every replacement asserted** — an upstream bump fails the build
  rather than silently restoring Chinese or a bug. English pass plus the language-neutral fixes, and
  now the clock format, which had to move into the substitution because a persona rule lost to the
  literal string in the prompt.
- ✅ **Our own fully English, commented config**, with the privacy-relevant keys set explicitly and
  marked as such: the empty plugin allowlist and the memory summariser's `llm`, both of which inherit
  cloud defaults if omitted, because the config merges rather than replaces.
- 🟡 Generic persona ✅ and local memory ✅; a log glossary for the Chinese server logs is still missing,
  and it is the next thing somebody reading their own logs will want.
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
- ✅ **Browser flasher** (ESP Web Tools, served from GitHub Pages) so owners can flash without
  installing anything. Serves the binaries from the *Pages artefact*, not from the release: release
  assets carry no `Access-Control-Allow-Origin`, so a browser cannot fetch them however correct the
  URLs are. Writes each part at its own offset rather than a merged image, because a merged image pads
  over NVS and destroys the per-unit servo calibration.
- ⬜ Building from source: ESP-IDF version, the `SDKCONFIG_DEFAULTS` requirement, pre-flash checks
- ⬜ Hardware bring-up guide
- ⬜ Troubleshooting: symptom → cause → fix, generalised from the reference project
- ⬜ Known board variants, maintained from reports
- ✅ **Automated firmware builds and versioned releases** (app + assets), with the release blocked
  unless the artefact is right: chip and board first, then the OTA URL, then the assets partition
  inspected for the wake-word model and the face. Three releases shipped for the wrong chip before
  those existed, and the check that would have caught all three is a blunt size floor.
- ⬜ License (MIT) with upstream notices preserved; "by Drax and Claude, built with Claude Code"

## Privacy you can verify ⬜

The reason the project exists, so it gets a page of its own rather than a promise.

- ✅ **[A checklist an owner can run themselves](privacy.md)** — six checks, each with the command and the
  expected output, plus what the project does **not** protect against. The firmware URL scan is run and its
  real output published: one configured address, the robot's own setup AP, an XML namespace and a doc link
  printed in a log message. No NTP server either, so the clock comes from your own server.
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

## Release 🟡

- ✅ Private GitHub repo while the port was in progress
- ✅ Public, with the quickstart run end to end on a factory unit
- ✅ **Tagged releases with a browser flasher**, built and asserted by CI. How to cut one, and what
  each assertion is there to stop, is in [releasing.md](releasing.md)
- ⬜ Flashed and used by somebody who did not build it. Verified on one robot, by one person

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
- ✅ **Browser flasher: no, ESP Web Tools cannot take the factory backup.** It writes; it does not
  read. So the backup stays a separate step with M5Burner or `esptool`, and both the quickstart and the
  flasher page say so before the first button — it is the only step in the whole process with no undo.
