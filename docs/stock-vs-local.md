# What you gain and what you lose

The firmware your robot shipped with is not a toy, and this project is not a superset of it. Flashing
this replaces a small application suite with one thing done thoroughly. Worth knowing before you
start, so nothing comes as a surprise afterwards — **he will not dance any more.**

You can go back. The factory image is recoverable *only if you backed it up first*, which is why
that is step zero of [the quickstart](quickstart.md) and not a footnote.

---

## What the stock firmware ships

It is a launcher with apps. Named from the shipped image itself; where a description says "appears
to", that is inference from the app's name and assets rather than something verified by running it:

| app | what it is |
|---|---|
| **AI Agent** | the voice assistant — cloud, and the reason this project exists |
| **Avatar** | a face for the robot |
| **Dance** | a servo routine. It dances |
| **App Center** | downloads more apps onto the device |
| **EzData** | M5Stack's cloud data service, paired with a code |
| **ESP-NOW Remote** | pairs two devices as sender and receiver over ESP-NOW |
| **Account** | binds the robot to an M5Stack account |

Plus an on-screen setup menu: Change Wi-Fi · Device · Brightness · Volume · Timezone · Hardware Test
· RGB Strip · Unbind & Reset · Firmware / Check for Updates.

Underneath, the stock AI Agent is built on the same open `xiaozhi-esp32` firmware this project forks —
the class names are still in the shipped binary. The difference was never the software. It was where
the microphone audio went.

---

## Dropped, and not coming back

| gone | why |
|---|---|
| **Dance** | a self-contained servo routine with no equivalent here. The head moves, but as behaviour during conversation, not as a performance. The nicest thing we removed |
| **App Center** | downloading and running code from a vendor catalogue is the opposite of this project's promise |
| **EzData** | a cloud data service |
| **Account / Unbind** | there is no account. Nothing to bind to, nothing to unbind from |
| **ESP-NOW Remote** | unrelated to a voice assistant; nobody has asked |
| **Firmware update over the air** | deliberate. OTA means the robot fetching and running code from a URL. You flash it yourself, from a binary you can rebuild |
| **The launcher itself** | this firmware is one application |

---

## Kept, rebuilt, or better

| | |
|---|---|
| **Voice assistant** | rebuilt on your hardware. Wake word on the robot, speech recognition, model and speech synthesis on a machine you own. Nothing leaves your network |
| **Avatar / face** | drawn rather than played back — it blinks, holds your gaze, squints, and reacts while he talks and thinks |
| **Brightness, Volume** | kept, and also settable by voice |
| **RGB strip** | kept as an ambient status ring rather than an effects demo |
| **Hardware test** | kept, and promoted: it runs at **every boot** and the chime is its result. The success sound means the speaker, microphone and servo rail were all confirmed. A different sound means something did not come up |
| **Change Wi-Fi** | kept, and it also carries the server address — hold the screen for five seconds |
| **Camera** | photos display on the robot's own screen and go nowhere. There is no cloud vision path, by construction |
| **Timezone, Device** | moved server-side, where the clock already lives |

### And things the stock firmware has no equivalent for

- **A model you choose.** Any OpenAI-compatible endpoint: swap a 4B for a 32B with one command.
- **Tools, over MCP.** He can be given the ability to actually check or do things.
- **Memory between conversations**, summarised locally.
- **An ambient status ring and idle screen** driven by whatever you point them at.
- **The privacy claim is checkable.** The firmware contains one endpoint — yours — and you can verify
  that yourself with `strings`.

---

## The honest trade

The stock firmware is a finished consumer product with a catalogue behind it. This is a robot that
does one thing, on hardware you control, that you can read the source of and change.

If what you wanted was a desk toy that dances and downloads apps, keep the factory firmware — it is
good at that, and there is no shame in it. If you want the microphone in your house to answer to a
machine in your house, you are in the right place.
