# Security

Report anything to **petrdraxler@gmail.com**. A GitHub issue is fine too for
things that are not sensitive — most of what follows is already written down in
public, because a privacy project that hides its weak points is not one.

This is a side project maintained by one person. Expect a reply in days rather
than hours, and expect honesty about what will and will not be fixed.

---

## What is in scope

- The **firmware** in `firmware/main/boards/m5stack/stackchan/` and the changes
  this project makes to the vendored tree.
- The **patch kit** in `server/patches/`, which rewrites parts of the server at
  image build time.
- The **browser flasher** and the released binaries.
- **Documentation that tells you to do something unsafe.** A wrong instruction
  in a guide is a real vulnerability when people follow it.

## What is not

Report these to the projects that own them — they are vendored or depended on,
not written here:

- `xiaozhi-esp32` and `xiaozhi-esp32-server` upstream
- ESP-IDF, Ollama, llama.cpp, Kokoro, Whisper/speaches
- Anything in `firmware/managed_components/`

## Things that are already known, and deliberate

These are design decisions with their reasoning written down. Telling us again
is welcome but will get this answer:

| | |
|---|---|
| **NVS is not encrypted.** Whoever holds the robot can read the ambient-status token out of flash with a cable | [mcp_status_source.h](firmware/main/boards/m5stack/stackchan/mcp_status_source.h) says so, and says to use a read-only, revocable, LAN-scoped credential and nothing more |
| **Voice has no confirmation step.** A misheard sentence reaches whatever a tool reaches | [tool-safety.md](docs/tool-safety.md). Read-only tools by default, and an allowlist for anything that changes the world |
| **The robot has an always-on microphone and a camera** | That is what it is. Both have switches that persist across a reboot, and the mute is visible on screen because one you cannot see is one you will forget |
| **A local vision model sees your room** | It is off unless you configure one, and the frames are not kept unless you turn that on. [privacy.md](docs/privacy.md) |
| **Four upstream tools were removed rather than disabled** | Upstream's "user only" marking hides a tool from the listing and then runs it by name anyway. [robot-tools.md](docs/robot-tools.md) |

## What would genuinely worry us

- A way for the **server** to make the robot do something the tool list does not
  allow — the firmware's MCP surface is meant to be the whole surface.
- Anything that makes the **microphone or camera switch lie** — they close the
  input device and refuse at capture respectively, and both are asserted.
- A path by which a **released binary differs from the source** it claims to
  come from. CI asserts the chip, the board, the OTA URL and the contents of the
  assets partition; [releasing.md](docs/releasing.md) has the checks and the
  releases each carry a `SHA256SUMS`.
- **Anything that phones home.** [privacy.md](docs/privacy.md) is six checks you
  can run yourself; a result that contradicts it is the report we most want.

## If you are reporting about your own robot

Please do not send us a **full-flash dump** or an NVS backup. They contain your
Wi-Fi password in plain text. `BOARD_REPORT` over the serial console prints
what we usually need and deliberately carries no MAC address and no network
name — see the [board README](firmware/main/boards/m5stack/stackchan/README.md).
