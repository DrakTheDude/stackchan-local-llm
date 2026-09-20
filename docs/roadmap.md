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
calibration. And the model floor is measured: fifteen models, tool calling, speed and VRAM, in
[model-floor.md](model-floor.md).

**What is left is other people's hardware.** Every hardware claim comes from one robot; the model
numbers now come from two GPUs, which was enough for them to disagree and for that to be the most
useful thing they said.

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

## 1. Firmware and platform ✅

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
- ✅ **"The mouth stops moving after a photo" — it never did.** Reported and closed 2026-09-19. The
  face is *hidden behind the photo*, and the photo was staying up for twenty seconds, so most of the
  reply was delivered with no face on screen at all. The mouth resumes the instant the photo goes away,
  which is what the capture shows and what watching it confirms:

  ```
  I (89844) preview shown (from main)
  I (100434) mode thinking -> speaking          <- 10.6s into the photo
  I (109844) preview cleared (from esp_timer)   <- face back, mouth moving
  I (122904) mode speaking -> listening
  ```

  🔴 **And it was self-inflicted.** `PREVIEW_IMAGE_DURATION_MS` went 5s → 20s in `07c2bdf`, because
  five seconds was too short to look at a photo. Nobody connected "the photo stays longer" with "the
  face is gone for longer", because the two are the same screen and only one of them was being thought
  about. **You cannot show a photograph and a face at the same time on a 2" screen**, so the number is
  a trade rather than a preference. Settled at **10s** — 20 was for the camera work, and it covers the
  description plus the first sentence of the reply, which is what the photo is for.

  Three suspects were ruled out by inspection first, including the one this item originally named —
  the `Tick()` screensaver guard cannot fire without a status source attached; the mouth is not
  covered by the chat bar; the preview teardown does un-hide the face on every path. The instrument
  that settled it stays: every mode change logs old → new and who asked, and both preview edges log
  their task. They are not the same task, which is worth knowing on its own.
- ✅ **The boot-time I²C glitch, diagnosed. It is not a glitch, and nothing NAKs.** Every failure in
  that window is `ESP_ERR_TIMEOUT`, and **the same device answers immediately on a 250ms retry**:

  ```
  W i2c watch: no answer from pmic=ESP_ERR_TIMEOUT(answered at 250ms)
  W i2c watch: back after 47ms
  W i2c watch: 3 outage(s) in 45s, worst 52ms
  ```

  🔑 **The bus is busy, not broken.** The devices are present and answering throughout; a 20ms budget
  is simply not enough to get a slot while Wi-Fi is associating. `ESP_ERR_TIMEOUT` means the
  transaction never got out; `ESP_ERR_NOT_FOUND` would mean the device declined to answer. **Not one
  NOT_FOUND has ever been recorded here.** Three outages a boot, ~50ms each, at the auth/assoc
  transitions — not the "few hundred milliseconds" this item claimed, which came from an earlier
  instrument that counted its own retries into the window it was measuring.

  That re-reads both historical faults as one thing: a **short timeout reported as a missing device**.
  The amplifier whose register writes "went nowhere" and the rail status register that was
  "unreadable" were both timeouts. Which is why the two existing mitigations are right and now have a
  mechanism — the codec retries, the PY32 refuses to write a guess — and why the fix for anything
  similar is a longer timeout, not a hardware hunt.

  ⬜ **Still open underneath it:** *which* task holds the bus. The LED ring writes 12 pixels and a
  latch to the PY32 continuously, the touch panel is polled, and the camera shares the bus — any of
  them could be the holder, and the correlation with Wi-Fi association suggests the holder simply
  takes longer to finish while the CPU is busy. Knowing the mechanism was the part that changes what
  anybody does about it.
- ✅ **Per-unit servo calibration.** Each robot's factory centre is read from its own NVS at boot —
  the keys `zero_pos_1` / `zero_pos_2`, found by searching the partition rather than by assuming a
  namespace name, since the name belongs to the vendor's app and one sample is not a convention.
  Flashing the app partition leaves NVS alone, so the values are there on every unit. The travel
  limits are now spans around *that* centre, which was the safety part: tilt has only ~90° before a
  mechanical stop. The old constants remain as a fallback that announces itself loudly. The separate
  bench trim is set on the robot, from the settings menu, with `CONFIG_STACKCHAN_PAN_TRIM` surviving
  only as its default — it is per-unit, and guessing makes it worse. Verified: read `servo/460/620` on
  the reference unit, matching its disassembled backup.
- ✅ **Server address configurable after flashing.** Mostly already built upstream, which is worth
  recording: `Ota::GetCheckVersionUrl()` reads `ota_url` from NVS with the compiled value as fallback,
  the config portal has the field that writes it, and this tree already enabled the flag that shows it.
  What was genuinely missing was a way back *in* — config mode is otherwise reached only with no saved
  Wi-Fi or on a connect timeout — so changing servers meant breaking the Wi-Fi on purpose. A five-second
  hold on the screen now enters setup. The robot also says "no server set yet" instead of looping a
  connection error, and the setup network is `StackChan-XXXX` rather than upstream's `Xiaozhi-XXXX`.
  **Both shipped**: prebuilt binaries and the browser flasher are live, and this is what made one
  binary serve everybody.
- ✅ **Local vision, optional.** A vision model on the owner's own machine describes what the camera
  photographs; with none configured the photo stays on the device exactly as before, and the firmware
  needs no switch for it — the server offers a vision URL only when it has a model. `qwen2.5vl:3b` is
  4.1 GB and ~0.35 s per frame, and sits alongside the chat model rather than swapping with it. See
  [vision.md](vision.md), and [privacy.md](privacy.md) for the check that replaced "GetCamera()
  returns nullptr".
- ✅ **Stream the camera only while taking a photo.** `StartStreaming`/`StopStreaming` bracket the
  capture, so the sensor is idle when nobody is taking a picture instead of burning 3 MB/s of PSRAM
  DMA from boot. This was also a correctness fix: at VGA the continuous load reaches 9.8 MB/s, the
  wake-word engine shares that bus, and detection went spotty the moment VGA was tried.
- ✅ **The photograph is good now** — metered in firmware rather than left to the sensor, which stops
  at exposure 480 with the frame three stops under and its own gamma hiding the shortfall. Exposure
  first, then gain, corrected by the cube law the ISP's gamma imposes. Black floor 24 → 6, median 71,
  full range in use, consistent run to run. See [vision.md](vision.md).

  ⚠️ **The VGA theory in the previous version of this item was wrong** and is recorded in vision.md so
  nobody retries it: VGA made the picture *darker* (mean 15 against 31), because the mode changes the
  PLL as well as the row length. `HalveUyvy()` and the whole YUV path went with it — the sensor emits
  RGB565 straight to the panel now.
- ✅ **Behaviour, documented** — [behaviour.md](behaviour.md): the face and its twenty-one expressions,
  the head including the thinking tell and why it is silent, every LED ring state, the camera, the wake
  word, standby, and the boot chime as a test result. Written as *what he does and why*, because most of
  it looks like decoration and several of them are the only feedback you get when something is wrong.
- ✅ **Hardware assumptions, each with a way to verify it** — the table in the
  [board README](../firmware/main/boards/m5stack/stackchan/README.md), now with a check per row and the
  sample size stated at the top rather than implied at the bottom. It also corrected itself: it still
  said the servo centre was hard-coded from the reference unit, which stopped being true when per-unit
  calibration shipped. A stale assumption table is worse than none — it is read as current.
- ✅ **Board variants: a diagnostic mode.** `BOARD_REPORT` over the USB serial console prints the I²C
  scan, the rail, the servos and their calibration source, the LED chain, the codec, the camera's own
  registers and the privacy switches — fixed order, fixed labels, so two units produce two reports that
  **diff**. Deliberately no MAC address and no Wi-Fi name: a report meant to be pasted into an issue
  must not carry an identifier for the person pasting it.
- ✅ **Settings live on the robot.** The on-screen menu has Wi-Fi & server, volume,
  brightness, body and mood, the self-check, About, the privacy switches — and now the **head trim**,
  which was the one number every owner had to set for their own robot by editing a Kconfig and
  rebuilding the firmware, for something you decide by looking at him. It is a live preview, as it had
  to be: two rows of `[-] value [+]`, the head re-centres on every tap, and he is **held still** for the
  whole page, because a head that glances away on its own schedule while you are judging whether it is
  straight makes the screen useless. Save writes to NVS; Back puts back what the page opened with. The
  build setting survives as the *default*, so an untrimmed robot behaves exactly as before.

  **Screen dims** and **Power off on battery** are rows too, stepping a few choices each — `never` is
  one of them, and it is `-1` rather than a very large number, because a robot that dims after nine
  hours is still a robot that dims and whoever turned it off would find out at the worst moment. Both
  persist, and changing one resets the idle counter: otherwise lengthening the time before he dims
  makes him dim on the next tick, which reads as the setting doing the opposite of what it says.

  The ambient **status source** stays on the serial cable, and that is the decision rather than the
  gap: the URL is now shown read-only on the About page, so you can check where he is polling while
  standing in front of him, and the token is only ever reported as *set*. A settings page is exactly
  where a guest would look for a credential, and serial provisioning already requires holding the
  robot.
  ⚠️ A page served on the LAN would be easier to type into, and is a listening socket on a device whose
  selling point is that it does not phone anywhere. If it is ever built, binding, authentication and
  being off by default are deliberate decisions, not afterthoughts.
  The Wi-Fi portal is NOT the place for any of it: its HTML lives in a managed component, so every field
  added there is a fork to maintain. It stays for what it is good at — the things you need *before* the
  robot is on the network.
- ✅ 🎭 **[Bodies and moods](characters.md).** Two bodies (Drax, Classic) and three moods, switchable
  from the settings menu with no reflash and no reboot. A body carries the face palette, the UI theme,
  the eye geometry, the LED ring, the voice and the persona; a mood carries `motion_unit` and
  `gesture_unit`.

  🔴 **They are two axes, and that was a correction rather than a design.** The first version had one
  list, which made "caffeinated Classic" a contradiction when it is obviously a thing somebody would
  want to be — *caffeinated Stacky is still Stacky, he just moves and talks fast*. A mood modulates a
  robot; a body replaces him.

  🔑 **Everything is a unit, not a list of values.** One number scales every duration, every
  amplitude, the whole face, and every corner. "Five in the morning" is not a new animation — it is
  the existing glance behaviour at `motion_unit 2.2`. `radius_unit 0` is the whole of the Classic
  identity: square everything with one number.

  **The two machines meet at the ID and nowhere else.** The firmware sends which body it is wearing in
  the *handshake headers* — not the hello message, because components are built from config the moment
  a connection is accepted, while hello is still in flight. The server never learns about brow
  thickness; the firmware never learns what a prompt is.

  ⚠️ Still open: the face LAYOUT does not scale with the shape (positions are compile-time), and
  `Repaint()` has to know about every property a body can touch — nothing enforces that, so the next
  token added will apply at construction and silently not on switch.

---

## 2. Local AI stack 🟡

Wake word → STT → model → TTS, with every backend a setting rather than a choice made for you.

- ✅ **`docker compose` stack with each stage replaceable**: VAD, STT, LLM, TTS. One file, one
  `.env`, and a single value that must change — this machine's LAN address.
- 🟡 **LLM: any OpenAI-compatible endpoint with tool calling.** Ollama is the default and llama.cpp
  is a compose profile; both are configured and documented, with LM Studio as a commented third.
  Only llama.cpp is *verified* in daily use. The template requirement is written down because it
  is the usual reason a model "cannot call tools" — llama.cpp needs `--jinja`.
- ✅ 🔑 **[An honest model floor](model-floor.md), measured.** Fifteen models on one RTX 4090:
  tool-calling score, time to the first *spoken* word, tokens/second and VRAM, with failures published.
  The guesses it replaced were wrong in the middle — `qwen3:8b` scores 100% where `qwen3:14b`, the
  model this project shipped, manages 83%. Three things worth carrying elsewhere:
  **size is not the variable** (a 24B scored 67% using 19.6 GB; a 3B scored 83% in 5.0 GB);
  **reasoning costs the conversation and bought nothing here** (identical scores, up to 17 seconds of
  silence before the first word); and **speed tracks *active* parameters**, so a 30B MoE generates at a
  4B's rate. The harness needed five fixes first, each of which produced a confident wrong number —
  they are documented at the foot of the page because none of them are specific to this project.
- ⬜ **The same numbers on a second GPU.** [`tools/model-bench/sweep.sh`](../tools/model-bench/sweep.sh)
  is one command against any Ollama; results from a smaller card are the most useful contribution to
  this page.
- ✅ **Reasoning can be switched off, without patching the server.** Its Ollama provider prepends
  `/no_think` to the user's message for any `qwen3*` model and calls the OpenAI-compatible endpoint;
  measured, neither does anything, and the instruction is visible to the model as part of what the
  user said. The reason is that the no-think branch of the chat template is gated on `$.Think`, which
  only Ollama's native API sets — so no wording can reach it. A template is just text, so
  [`server/no-think.sh`](../server/no-think.sh) builds a variant with that branch pinned. Verified at
  21/21 on tools and 0.07 s to first word, against 4.3 s for the stock model on an 8 GB card.
- ⬜ The `/no_think` injection is still there and still does nothing useful. Worth removing upstream,
  since it also puts the instruction in front of the model as part of the user's sentence.
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
- ✅ Generic persona, local memory, and a **[glossary for the Chinese server logs](server-logs.md)** —
  the real strings from a running server, grouped by what you are trying to find out, with the five
  lines worth learning at the top and a section for the ones that look alarming and are not.

  The logs stay in Chinese deliberately: the patch kit translates what the model *reads* and the user
  *hears*, because those reach somebody who did not ask for them, while a log is read by you, once,
  when something is wrong. Translating every line would mean re-doing it on every upstream bump for no
  gain. 🔴 One line in it earns its place on its own — `为记忆总结创建了专用LLM` names the model the
  memory summariser uses, which is how you confirm it has not silently inherited a cloud default.
- ✅ CPU-only / non-NVIDIA notes — [your-llm.md](your-llm.md) covers Apple Silicon, AMD and CPU,
  including the one check that tells you whether your GPU is really being used

## 3. Integration interface ✅

**Decided: MCP is the contract — no new API.** It's what most users will already know, the server already
mounts any MCP server as tools for the model, Home Assistant ships an official MCP server, and the reference
project proved the path end to end.

- ✅ **Tools for the model** — [mcp.md](mcp.md): the one JSON file, all three transports, where
  credentials go, how to tell whether it worked, and what makes a model actually *choose* a tool.
  Including the trap that `transport` defaults to `sse`, so pointing at a streamable-HTTP server
  without saying so connects and then silently offers no tools.
- ✅ **Ambient status contract** — [ambient-status.md](ambient-status.md). One stateless JSON-RPC POST
  to one named tool; a level (`ok` / `warn` / `alert`), a spoken sentence, up to six cards. Off until a
  URL is provisioned over USB serial, and the level is never inferred from the words in the summary.
  Not JSON at all still works, at reduced function: you get the sentence, you do not get a colour.
- ✅ **Reference status server** — [tools/status-server/](../tools/status-server/): one file, no
  dependencies, reports the machine it runs on, and `--demo` cycles ok → warn → alert so the ring, the
  chime and the double-take can all be seen in a minute with no homelab. It answers `tools/list` too,
  so the same URL doubles as an MCP server for the model.
- ✅ **The robot's own tools** — [robot-tools.md](robot-tools.md): all fourteen, what each takes,
  and the four upstream ships that this firmware removes rather than hides. Plus the three things
  tuning them taught, which are the useful part for anybody writing their own.
- ✅ 🔴 **Safety guidance.** [docs/tool-safety.md](tool-safety.md). Voice has no confirmation step, and speech recognition mishears. Recommend
  read-only tools by default and an explicit allowlist for anything that changes the world — a misheard
  sentence must not be able to unlock a door.
- ✅ 🔴 **Audit the upstream device tools the model can call.** Done, and the finding was that
  upstream's "user only" marking is not a gate: it keeps a tool out of `tools/list`, and then
  `DoToolCall` looks a tool up **by name** and runs it with no `user_only()` check anywhere. The
  protection was that the name was not advertised — which protects nothing on a device whose only
  input is a microphone with no confirmation step.

  Removed from the build: `self.upgrade_firmware` (URL → download → flash → reboot),
  `self.assets.set_download_url`, `self.screen.snapshot` (uploads a picture of the screen to a URL)
  and `self.screen.preview_image` (fetches and displays a URL). Kept the read-only ones and
  `self.reboot`. 18 tool names in the binary became 14, and `claims.ini` asserts the four stay out —
  a subtree update would restore them and nothing else would notice.

## 4. Reproducibility ✅

Full English docs, and a path from a factory device to a local robot in about 30 minutes.

- ✅ 🔑 **Quickstart: factory device → local robot in ~30 minutes.** [quickstart.md](quickstart.md):
  back up, bring up the server, flash, first run, troubleshoot. Conditions stated at the top — what the
  thirty minutes excludes is named rather than implied.
- ✅ 🔑 **Factory firmware backup as the first step**, plus the restore path — section 0 of the
  quickstart, before anything else, and repeated on the flasher page where the button is. The Wi-Fi
  credentials warning is there: plain text, in NVS, treat it like a password file.

  ⚠️ **"Verified" is not done**, and it is worth not pretending otherwise: nothing tells an owner how
  to confirm their backup is actually good. A backup you cannot check is a backup you find out about
  on the day you need it.
- ✅ **Browser flasher** (ESP Web Tools, served from GitHub Pages) so owners can flash without
  installing anything. Serves the binaries from the *Pages artefact*, not from the release: release
  assets carry no `Access-Control-Allow-Origin`, so a browser cannot fetch them however correct the
  URLs are. Writes each part at its own offset rather than a merged image, because a merged image pads
  over NVS and destroys the per-unit servo calibration.
- ✅ Building from source — [quickstart §2](quickstart.md), with the `deploy/build.sh` requirement
  and why a plain `idf.py build` silently produces a different board
- ✅ Troubleshooting: symptom → cause → fix — the table at the end of
  [quickstart.md](quickstart.md#troubleshooting), plus the flasher page's own. Worth splitting into
  its own page when it outgrows a table rather than before
- ✅ **Automated firmware builds and versioned releases** (app + assets), with the release blocked
  unless the artefact is right: chip and board first, then the OTA URL, then the assets partition
  inspected for the wake-word model and the face. Three releases shipped for the wrong chip before
  those existed, and the check that would have caught all three is a blunt size floor.
- ✅ License (MIT) with upstream notices preserved, and the attribution on the front page

## Privacy you can verify ✅

The reason the project exists, so it gets a page of its own rather than a promise.

- ✅ **[A checklist an owner can run themselves](privacy.md)** — six checks, each with the command and the
  expected output, plus what the project does **not** protect against. The firmware URL scan is run and its
  real output published: one configured address, the robot's own setup AP, an XML namespace and a doc link
  printed in a log message. No NTP server either, so the clock comes from your own server.
- ✅ **[What the factory firmware talks to](factory-firmware.md)**, as far as static analysis can
  establish it — the same two commands as check 1 of privacy.md, run against the backed-up factory app
  partition. Found: the hardcoded `api.tenclass.net` OTA endpoint, a WebSocket and four device
  endpoints at a **bare IP with no hostname** (four of them over plain `http://`), and a call that
  **registers the MAC address**. Plus the three that are *not* endpoints, listed so nobody
  re-derives them in alarm.

  🔑 **The OTA endpoint hands over the rest**, which makes that list a lower bound rather than a set —
  anything it returns is also a destination. That is the reason this project reflashes rather than
  reconfigures.

  🔴 **On the app partition, never a full dump**, and the page says so twice: NVS holds the Wi-Fi
  password in plain text. What it deliberately does *not* claim is also written down — no packet
  capture was made, a URL in a binary is a capability and not an event, and none of it establishes
  intent.

## Release ✅

- ✅ Private GitHub repo while the port was in progress
- ✅ Public, with the quickstart run end to end on a factory unit
- ✅ **Tagged releases with a browser flasher**, built and asserted by CI. How to cut one, and what
  each assertion is there to stop, is in [releasing.md](releasing.md)


## Waiting on a second robot

**Not roadmap items, because no amount of work here finishes them.** Every hardware claim in this
project comes from one unit, and these need a different one in somebody else's hands:

- **Hardware bring-up guide** — what a first power-on looks like, and what to check when it does not.
  Written from one robot it would be a description of this robot.
- **Known board variants, maintained from reports** — there is nothing to maintain until a second
  variant is reported.
- **Flashed and used by somebody who did not build it** — the one that matters most, and the one
  least in our control.

They sit here rather than as ⬜ so the open items above stay honestly actionable. A list where some
entries can never be closed teaches a reader to skim the ones that can.

> 🔑 **This is the most useful thing a second owner could contribute**, and it costs them a report
> rather than a pull request. See [`tools/model-bench`](../tools/model-bench) if it is a GPU rather
> than a robot.

## Upstream — ours to send, after it has run a while

**Not blocked on anybody. Waiting on a condition:** a week or two of daily use before these go to
somebody else's repository. Every one is already fixed and running here, and a patch pushed upstream
and then changed is worse than the same patch sent a fortnight later.

- `xiaozhi-esp32-server`: the TTS sentence splitter missing the ASCII full stop; `.rstrip()` eating the
  spaces between streamed chunks. Both have measured reproductions.
- `xiaozhi-esp32`: the StackChan board; a runtime-configurable server address
- `xiaozhi-esp32`: 14 language packs start `ACCESS_VIA_BROWSER` with a Chinese full-width comma
  (fixed here already)
- `xiaozhi-esp32`: `I2cDevice` aborts the whole device on one flaky I²C transfer (fixed here already)
- `xiaozhi-esp32`: CoreS3 trusts `esp_codec_dev_open`, which returns success when the amplifier is
  unreachable — the robot then plays every reply into a chip that is not listening, silently. Affects the
  stock CoreS3 board, not just this one; it only looks intermittent because a boot chime sometimes
  happens to retry the open at the right moment (fixed here already)
- Possibly: English logging / i18n for the server

The argument for sending them at all: each is a bug that bites everybody using those projects, not
just this robot. The `esp_codec_dev_open` one in particular affects the stock CoreS3 board and
presents as an intermittent fault, which is the worst kind to inherit.
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
