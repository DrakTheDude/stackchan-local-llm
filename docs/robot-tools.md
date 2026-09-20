# The robot's own tools

Fourteen, exposed over the same [MCP](https://modelcontextprotocol.io) protocol you use to give him
*other* tools — but in the other direction: the firmware offers these, and the model on your server
calls them.

You do not configure any of this. It is listed because it is the half of the surface people forget
exists, and because these descriptions are the ones this project has actually tuned. If you are
writing tools of your own, [mcp.md](mcp.md) has what was learned doing it.

---

## What he can do

| tool | what it does |
|---|---|
| `self.robot.set_head_angles` | `pan` ±70°, `tilt` ±35°, `duration_ms`. Negative pan is left, negative tilt is down |
| `self.robot.get_head_angles` | where the head *actually* is, read from the servos, as `{"pan":…,"tilt":…}` |
| `self.robot.center_head` | back to centre |
| `self.robot.set_led_color` | `color` (a name or `#rrggbb`), `effect` (solid / breathe / blink / comet), `brightness` 0–100 |
| `self.camera.show_photo` | take a picture, show it on his own screen, and — if a vision model is configured — describe it |
| `self.camera.take_photo` | upstream's cloud-shaped variant. Disabled on this board; see below |
| `self.audio_speaker.set_volume` | 0–100 |
| `self.audio_speaker.set_boost` | the amplifier's boost converter on or off. For "louder" when volume is already at its limit |
| `self.screen.set_brightness` | 0–100 |
| `self.screen.set_theme` | light or dark |
| `self.get_device_status` | volume, screen, battery, network — the first call before changing anything |
| `self.get_system_info` | firmware version, chip, memory |
| `self.screen.get_info` | width, height |
| `self.reboot` | reboots |

Read-only ones are safe by construction. The rest change only the robot itself: nothing here reaches
your network, your files, or anything outside the case.

---

## What is deliberately absent

Four tools upstream ships are **removed from this firmware**, and it is worth knowing why before you
add something similar of your own:

| removed | what it could do from a string |
|---|---|
| `self.upgrade_firmware` | fetch firmware from a URL, flash it, reboot |
| `self.assets.set_download_url` | the same, one step back |
| `self.screen.snapshot` | capture the screen and upload it to a URL |
| `self.screen.preview_image` | fetch a URL and display it |

🔴 **They were not disabled, they were removed, because upstream's "user only" marking is not a
gate.** It keeps a tool out of the listing, and then the call handler looks a tool up **by name** and
runs it with no such check. The entire protection was that the name was not advertised — which
protects nothing on a device whose only input is a microphone with no confirmation step, driven by a
model that invents plausible names as a matter of routine.

`claims.ini` asserts those four stay out of the built binary, because a subtree update would restore
them and nothing else would notice.

`self.camera.take_photo` is upstream's cloud-shaped photo tool — it posts the frame to an explain
endpoint. It is left registered but refused on this board; [vision.md](vision.md) covers what
replaced it.

---

## Three things worth copying

These came out of tuning the tools above, and every one of them cost something to learn.

**The description is read as instructions, not documentation.** `show_photo` returns a `view=` field
with the camera's notes in it. When the description explained what the field *was*, the model read it
out verbatim. When it said what to *do* with it — *"notes for you, never a line to repeat"* — and
showed an example, he started describing what he saw instead. A demonstration beats an instruction,
which is now the fifth time that has been true here.

**Anything a tool returns can end up spoken.** The result reaches a model whose output reaches a
speech engine. `show_photo` used to append a block of sensor diagnostics; the robot read them aloud.
They are behind a flag now.

**Return a refusal as a result, not an error.** When the camera is switched off, the tool returns a
sentence saying so, phrased so he does not apologise for a deliberate setting. An exception would
become a sentence about a failure nobody can act on.
