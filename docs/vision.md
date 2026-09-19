# Letting him see, locally

**It works, and it is cheaper than expected: about 4 GB of VRAM and a third of a second per photo.**
He takes the picture, shows it on his own screen, and tells you what is in it — with the model on a
machine you own.

## Turning it on

Two things, and the second is what actually switches it:

```bash
./server/use-model.sh qwen2.5vl:3b      # or: ollama pull qwen2.5vl:3b
```

Then in `server/data/.config.yaml`:

```yaml
selected_module:
  VLLM: LocalVLM          # add this line

VLLM:                     # and this block
  LocalVLM:
    type: openai
    base_url: http://ollama:11434/v1
    model_name: qwen2.5vl:3b
    api_key: ollama       # Ollama ignores it; the client library insists
    max_tokens: 200       # it is going to be spoken aloud
    temperature: 0.4
```

Restart the server. **Remove those and he goes back to simply showing you the photo** — that is not a
fallback, it is the other supported mode, and the firmware needs no change either way: the server
offers a vision URL only when it has a model, so with none configured the camera has nowhere to send
anything.

## What it costs

The camera works today: he takes a photo, tone-maps it on the device and shows it on his own screen.
What he cannot do is *describe* it. That is deliberate — see [the privacy checklist](privacy.md). The
firmware's `GetCamera()` returns `nullptr` on purpose and there is no vision API key, because the only
vision path on offer pointed at somebody else's cloud, and a camera in your room that phones out is the
thing this project exists to avoid.

A local vision model removes that objection rather than working around it.

## What it costs

Measured on an RTX 4090, with the chat model already resident, using a 320×240 frame — the size the
robot's camera actually produces:

| model | VRAM | per photo, warm | what it said about a red square and a yellow circle |
|---|---|---|---|
| **`qwen2.5vl:3b`** | **4.1 GB** | **0.32–0.36 s** | *"A red square and a yellow circle on a dark background."* |
| `moondream:1.8b` | 1.2 GB | 0.19–0.43 s | *"urn of yellow liquid on a black background."* |

`qwen2.5vl:3b` was right every time and `moondream` was wrong every time, on a picture with two shapes
in it. The extra 2.9 GB buys a model that can be believed.

⚠️ **This was a synthetic frame, and that limits what it proves.** It shows a model of that size can
describe a picture of that size, and what it costs to keep resident. A real GC0308 frame is noisy, low
contrast and tone-mapped on the device before it is sent — whether these models cope with *that* needs
a real photo, which needs the camera path to exist first.

⚠️ **The first measurement said 0.1 s and was wrong.** Three identical requests measure Ollama's prompt
cache, not the model. Camera frames are never identical, so the honest test varies the image — which
moved the figure to 0.35 s. Any vision benchmark that reuses one picture is measuring a cache.

## It fits alongside the chat model

Both stay resident together; nothing is evicted and no swap cost is paid per photo:

| card | chat model | vision model | total |
|---|---|---|---|
| **8 GB** | `qwen3:8b` 5.6 GB | `moondream:1.8b` 1.2 GB | 6.8 GB — fits, but with the weaker eye |
| **12 GB** | `qwen3:8b` 5.6 GB | `qwen2.5vl:3b` 4.1 GB | 9.7 GB |
| **16 GB** | `gpt-oss:20b` 12.9 GB | `moondream:1.8b` 1.2 GB | 14.1 GB |
| **24 GB** | `mistral-nemo:12b` 12.4 GB | `qwen2.5vl:3b` 4.1 GB | **16.5 GB**, room to spare |

The 8 GB row is the awkward one: it fits only by taking the vision model that gets things wrong. At
12 GB you can have both a chat model that scores 100% and an eye that can be trusted.

## What it took, and what nearly went wrong

**1. The server already had a provider, and it spoke Chinese.** `core/providers/vllm/openai.py` takes
a `base_url`, a `model_name` and an `api_key`, so pointing it at a local Ollama is configuration rather
than code. But it does this to every question:

```python
def response(self, question, base64_image):
    question = question + "(请使用中文回复)"       # "please reply in Chinese"
```

Hardcoded, unconditional, and invisible until now because this code path has never run. It belongs in
the same asserted patch kit as every other Chinese string the model is shown.

**2. `GetCamera()` returned `nullptr`, and that one line was doing two jobs.** It refused the stock
cloud-shaped `take_photo` tool *and* refused to accept any vision URL. Local vision needs the second,
so the camera is handed back now and a new `UseStockCameraTool()` holds the first door instead. The
control is the same size; it was attached to the wrong thing. [privacy.md](privacy.md) has the check
that replaced the old one.

**3. The model and the person were looking at different pictures.** `Explain()` JPEG-encodes the raw
sensor frame, which on this camera meters at **mean luma 31** in a lit room. The screen showed the
output of the board's tone curve, which is why it looked fine to the owner. So the owner saw a lit room and
the model said "a dark room", and both were describing what they were given. The board now hands the
*prepared* frame to `Explain()`.

**4. The first version read JSON aloud.** `Explain()` returns the server's envelope —
`{"success":true,"action":"RESPONSE","response":"..."}` — and passing the whole thing into the tool
result put a robot one step away from speaking it. Only the sentence goes through now.

## Settled: the picture

The photograph was about three stops under for three days, and the reason it took three days is worth
more than the fix.

**Both levers worked the whole time.** Measured on a held scene with the sensor's auto-exposure
genuinely disabled:

| exposure (rows) | 240 | 480 | 740 | 1200 | 2000 | 3000 |
|---|---|---|---|---|---|---|
| **mean** | 27 | 30 | 34 | 41 | 51 | 60 |

| gain (`0x50`) | 0x14 | 0x1C | 0x24 | 0x2C | 0x34 | 0x3C |
|---|---|---|---|---|---|---|
| **mean** | 34 | 39 | 43 | 46 | 49 | 52 |

🔴 **The output rises as roughly the cube root of the light** — that is the sensor ISP's gamma. Twelve
times the exposure is barely twice the picture. So *every* single-register experiment reported "no
visible change", and both levers were written off — gain explicitly, in a code comment, as measured
noise. Correct by the cube and the metering loop converges in three steps; correct by a ratio and it
creeps so slowly it looks like another dead lever.

**Neither lever is enough alone.** Exposure alone tops out at mean 60, gain alone at 52, and a lit
room wants about 100. It takes both — which is exactly why one-knob-at-a-time could never work.

**Two register facts worth not relearning:**

- The AEC enable is **bit 0 of `0x22`**, not bit 7 of `0xd2`. Clearing only `0xd2` left the sensor's
  own loop running, pulling exposure back to 480 underneath every write — which is why exposure looked
  like the dead lever and gain like the live one.
- `0x50` is a **six-bit** field. An early sweep ran it to `0xFF`; more than half of that was out of
  range, which produced both the "no ordering at all" result and four frames that came back dead.

**The haze was the gamma, not the panel.** A frame three stops under has its bottom end stretched hard
to be visible, which lifts the black floor and the noise with it — a grey veil over everything, in the
file as well as on the screen. Before: black floor 24, nothing above 124. After: black floor 6, median
71, p99 252. Fixing the exposure removed the veil from both at once, without touching the display.

That retired a long list of suspects, none of which was ever the bug: byte order, a red/blue transpose,
white balance, panel calibration, and the driver itself.

⚠️ **It lands at both rails.** Mean 103 takes exposure at the 12-bit maximum *and* gain at the top of
its field. Correct for a normally lit room, with no headroom for a darker one — and the long exposure
means a moving subject will smear. The loop backs off correctly in brighter light.

⚠️ **Do not re-test in isolation:** the AEC target (`0xd3`), horizontal blanking, or AWB gain writes.
Any single register on its own is the trap this all came from.

**Also disproved, so nobody repeats it:** the theory that QVGA was dark because its rows are half the
length of VGA's, and that VGA would collect twice the light. VGA was tried and made the picture
*darker* (mean 15 against 31) — the mode changes the PLL as well as the row length. Streaming is now
bracketed per photo (`StartStreaming`/`StopStreaming`) rather than running from boot, which was a real
problem and is fixed: at VGA the permanent 9.8 MB/s of DMA into PSRAM made wake-word detection spotty.

## Why this matters more than it looks

Vision is the one capability where "local" changes the nature of the thing rather than just the bill. A
camera that describes your room to a server you do not own is a different object from one that does it
on a card in the same room. The measurement says the second one costs about 4 GB.
