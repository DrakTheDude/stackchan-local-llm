# M5Stack StackChan board

Board support for the M5Stack StackChan: a CoreS3 on a servo base with two
LED strips and a camera. Not an upstream xiaozhi-esp32 board (yet).

**The comments in these files are the real documentation.** Most constants and
guard clauses carry a note recording the measurement behind them, or the crash
that put them there. Read the note before changing the line.

🔴 **Verified on one unit so far.** Two things are known to be specific to it —
see *Hardware assumptions* below.

---

## Files

| file | owns |
|---|---|
| `m5stack_stackchan.cc` | board bring-up: I²C bus, PMIC, IO expander, **servo rail** (`EnsureServoRail`), camera (`gc0308`, `yuv422`, `show_photo`), touch, theme, wiring everything together, optional status alerts |
| `stacky_face.cc/.h` | the drawn face (`StackyFace : SpiLcdDisplay`): expressions, blink, gaze, mouth, **thinking detection**, full-screen photo preview, one-line caption, optional idle status screen |
| `stackchan_leds.cc/.h` | the 12 WS2812C LEDs via the PY32 at `0x6F`: state effects, optional status colour, `set_led_color` |
| `stackchan_head.cc/.h` | head motion task: glances while speaking, thinking pose, double-take, **rail supervisor** |
| `status_source.h` | the interface an ambient status provider implements. None is attached in this build |
| `cores3_audio_codec.cc/.h` | ES7210 mics + AW88298 amp; non-fatal open; speaker boost toggle |
| `scs_servo.cc/.h` | Feetech SCS servo bus on UART1 (big-endian, 1 Mbaud, IDs 1/2) |
| `config.h`, `config.json` | pin map and board build config |

Build defaults: `firmware/sdkconfig.defaults.stackchan` — read its header first.

## Device-side MCP tools

| tool | |
|---|---|
| `self.robot.set_head_angles` / `get_head_angles` / `center_head` | names match the factory firmware |
| `self.robot.set_led_color` | lavender / blue / green / amber / red / grey / off or `#rrggbb`; solid, breathe, blink, comet |
| `self.camera.show_photo` | captures and shows on **this screen only** |
| `self.audio_speaker.set_boost` | amp boost converter, default off |

Deliberately **absent**:

- `self.camera.take_photo` — the stock tool uploads frames to a vision API, and
  the server's default is a cloud one. `GetCamera()` returns `nullptr`, so it is
  never registered and a vision URL is never accepted.
- any digital speaker gain — there is no echo reference on this board
  (`AUDIO_INPUT_REFERENCE=false`), so a louder robot hears itself and aborts
  mid-sentence.

---

## Invariants — each of these has rebooted or broken the robot

### 🔴 Never `ESP_ERROR_CHECK` an I²C call

The shared bus NACKs under load, and it carries the PMIC, IO expander, amp, mic
codec, PY32, touch controller and the camera's SCCB. An abort on one flaky read
has rebooted the robot from the battery gauge and from the codec's volume write.
**Retry, log, carry on.** `boards/common/i2c_device.cc` is patched the same way.

### 🔴 No LVGL, `std::string` or `std::vector` work on the `esp_timer` task

Every `ESP_TIMER_TASK` callback shares **one** task with a 3584-byte stack
(`CONFIG_ESP_TIMER_TASK_STACK_SIZE`), already carrying the LED animation and the
20 ms touch poll. LVGL work there crashed the face, and later the idle screen.
Set a flag there; do the work elsewhere.

### 🔴 `lv_timer` callbacks already hold the LVGL lock

The face animates on an `lv_timer`. The port lock is **already held** when it
fires, so `Tick()` must never take `DisplayLockGuard`. And never talk to the
servos while holding that lock — the face releases it before notifying the head.

### 🔴 Don't write identical geometry to LVGL

`lv_obj_set_size` / `lv_obj_align` invalidate even when nothing changed,
including the blurred eye bloom. A still face repainting 20×/s stole enough CPU
to make the LED writes NACK. Every write is cached.

### 🔴 Don't pin theme or font pointers

`theme->text_font()->font()` does not stay valid. It crashed 90 s after boot:
hidden objects are never laid out, so the bug detonated the first time the idle
screen was shown. Inherit styles instead.

### 🔴 Don't call `SetTheme()` during board construction

`SetupUI()` hasn't run, so it restyles null objects — `LoadProhibited` on every
boot. Select the theme through settings before the display is built, edit the
registered theme object, and let `SetupUI()` paint it.

### 🔴 Never reset the I²C bus with a device handle registered

`i2c_master_bus_reset()` with any long-lived handle registered breaks the bus for
the rest of the boot, and every later failure looks like a device that went away.

---

## Tasks and timers

| what | runs on | notes |
|---|---|---|
| face animation, idle screen | `lv_timer` (LVGL task) | lock already held |
| head motion + rail supervisor | `head_motion` task, prio 2, 3072 B | 150 ms poll; rail check every 200 steps (30 s). Starts **even if the head is dead**, so it can recover |
| LED animation | `esp_timer` | small work only |
| touch poll | `esp_timer`, 20 ms | small work only |

## I²C addresses that matter

| addr | device | |
|---|---|---|
| `0x6F` | PY32 **IO expander** | drives the servo rail (pins 0 and 13) and the LED chain. **Not `0x41`** — the vendor boot log's "Version: 0x41" is a value, not an address |
| `0x41` | status chip | register 1 **bit 0** reports the rail: 0 = on. A bit field, not a boolean |
| `0x21` | GC0308 camera (SCCB) | already registered by the camera driver — never add it again through `I2cDevice` (that constructor aborts on a duplicate) |
| `0x34` | AXP2101 PMIC | |
| `0x58` | AW9523 IO expander | resets the amp and the display |
| `0x36` | AW88298 amplifier | `AW88298_CODEC_DEFAULT_ADDR` is `0x36 << 1` — an **8-bit** address. Halve it before handing it to `i2c_master_probe`, which takes 7-bit |
| `0x40` | ES7210 microphone codec | same again: the header's `0x80` is 8-bit |

---

## Hardware assumptions

What this board file assumes about the hardware, and how to check each one.

🔴 **Every row below was verified on ONE unit.** That is not a hardware
specification, it is a sample of size one written down carefully. Anything here
could turn out to be true only of the robot it was measured on, and the honest
label for that is the one at the top of this section rather than a footnote at
the bottom.

**Send `BOARD_REPORT` over the USB serial console** and the robot prints all of
it, read back from the hardware, in a fixed order — so two units produce two
reports that diff. It deliberately prints no MAC address and no Wi-Fi name, so
the output is safe to paste into an issue.

| assumption | check |
|---|---|
| the I²C map in the table above | `BOARD_REPORT` → `i2c`, which lists what answered a scan of `0x08`–`0x77` |
| servo rail comes up via PY32 `0x6F` pins 0 and 13, reported by `0x41` | boot log: `servo motor rail ON`; `BOARD_REPORT` → `rail` |
| servos answer on UART1 (TX 6, RX 7) at 1 Mbaud, IDs 1 and 2 | boot log: `ping id 1: OK`, `ping id 2: OK`; `BOARD_REPORT` → `servos`, which also prints the live angles |
| 12 LEDs, two strips, second strip wired in reverse | the boot sweep runs down both sides together; `BOARD_REPORT` → `leds` |
| GC0308 camera with no XCLK / reset / power-down line | boot log: `camera:` line; `BOARD_REPORT` → `camera`, which reads the sensor's own registers |
| amplifier and microphone codec answer on the shared bus | `BOARD_REPORT` → `audio`. ⚠️ Both go unreachable for a few hundred ms about ten seconds into every boot — see the open roadmap item |
| **servo centre calibration is per-unit**, read from this robot's own NVS (`zero_pos_1` / `zero_pos_2`) | `BOARD_REPORT` → `servos` says `this unit's factory NVS` or **`FALLBACK`**. Fallback means the compiled-in centre is in use, the travel limits are a guess around it, and the tilt clamp — which exists because tilt has ~90° before a mechanical stop — is guessing too |
| **pan trim** `CONFIG_STACKCHAN_PAN_TRIM`, default 0 | per-unit and unguessable, so it defaults to no trim rather than to this robot's value. A rebuild, which is the open half of the settings roadmap item |

## Things that surprise

- **The PY32 keeps its own power and registers across a soft reset**, and
  flashing is a soft reset. A value written by old firmware can outlive it;
  `RestoreDefaultPinsLocked()` resets the pins on every boot for that reason.
- **The camera free-runs off its own crystal** and can't be reset or powered
  down from here, so a soft reset can land mid-frame. `WaitForVsyncGap()` arms
  the driver just after a VSYNC edge.
- **PY32 register 36 is a pixel count, not a brightness.** Brightness is applied
  in software.
- **The wake word is sent to the model as the first user message.** Choose the
  phrase for what it says, not only for how well it triggers.
- **"Thinking" has no device state.** The device stays in Listening until the
  first audio arrives; the face detects think time from
  `SetChatMessage("user", …)`.
