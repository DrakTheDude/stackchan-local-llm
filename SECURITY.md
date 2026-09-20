# Security and privacy reporting

This project exists because the firmware it replaces sends audio from an
always-on microphone, and photographs from a camera pointed at a room, to a
service the owner does not control. So a defect that **leaks data off the
owner's network is the most serious kind of bug this project can have**, and it
is treated that way — ahead of anything about the robot's behaviour.

## How to report

**Please do not open a public issue for anything that leaks data or gets code
onto a device.** Use either:

- GitHub's **[private vulnerability reporting](https://github.com/DrakTheDude/stackchan-local-llm/security/advisories/new)**
  on this repository, or
- **petrdraxler@gmail.com**

This is a hobby project maintained by one person, so there is no response SLA.
A realistic expectation is a few days. If something is being actively exploited
against real owners, say so in the subject line.

## What is in scope

- Anything that sends audio, images, transcripts or credentials off the local
  network, on any code path, including error and fallback paths
- A privacy switch that does not do what the UI says — the microphone mute is
  meant to close the input device, not merely discard samples
- Wi-Fi credentials, tokens or keys recoverable from a build artefact, a release
  binary, this repository, or its git history
- Anything in the flashing path that could serve a device an image other than
  the one the release published
- The documented [privacy checks](docs/privacy.md) passing on a build that is in
  fact talking to something remote — a check that cannot fail is the worst
  defect here, because it converts a verifiable claim into a false one

## What is out of scope

- **Upstream projects.** The firmware forks
  [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) and the server patches
  [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server).
  A flaw in their code should go to them — though please tell us too, since this
  project pins their versions and may need to move the pin.
- **The factory firmware.** What the stock robot talks to is documented in
  [docs/factory-firmware.md](docs/factory-firmware.md) as measured fact. It is
  not this project's to fix, and reports about it belong with M5Stack.
- **Physical access to the device.** Anyone holding the robot with a USB cable
  can reflash it. That is a property of the hardware and the reason the browser
  flasher works at all.
- **The model you point it at.** A local LLM can be prompted into saying
  something foolish, and it can call a tool badly. Tool exposure is the owner's
  decision — see [docs/tool-safety.md](docs/tool-safety.md), which is why the
  documented default is read-only.

## What this project does not protect you from

[docs/privacy.md](docs/privacy.md) has the honest version, and it is worth
reading before deciding this repository makes your robot private. Briefly: it
keeps traffic on your network, it does not make your network safe, it does not
encrypt what sits on your own disk, and anything you deliberately connect it to
over MCP can see what you send it.

## 🔴 Do not attach a firmware dump to your report

The natural thing to send with a hardware bug is a full-flash backup, and **it
contains your Wi-Fi password in plain text** — along with anything else NVS is
holding. The same goes for an NVS backup on its own.

`BOARD_REPORT` over the USB serial console prints what is usually needed: the
I²C scan, the rail, the servos and their calibration source, the LED chain, the
codec and the camera's registers. It carries **no MAC address and no network
name**, so it is safe to paste into a public issue. See the
[board README](firmware/main/boards/m5stack/stackchan/README.md).
