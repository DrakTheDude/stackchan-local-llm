# Could he see? What local vision costs

**Short answer: yes, and it is cheaper than expected — about 4 GB and a third of a second per photo.**
It is not wired up yet, and this page is the measurement plus what stands in the way.

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

## What stands in the way

Three things, in increasing order of work:

**1. The server already has a provider, and it speaks Chinese.** `core/providers/vllm/openai.py` takes
a `base_url`, a `model_name` and an `api_key`, so pointing it at a local Ollama is configuration rather
than code. But it does this to every question:

```python
def response(self, question, base64_image):
    question = question + "(请使用中文回复)"       # "please reply in Chinese"
```

Hardcoded, unconditional, and invisible until now because this code path has never run. It belongs in
the same asserted patch kit as every other Chinese string the model is shown.

**2. `GetCamera()` returns `nullptr`.** Deliberate, and the comment says why. Returning a real camera
means the frame goes to the server's vision endpoint — which is exactly what you want once that
endpoint is a model on your own machine, and exactly what you do not want while it is a default
pointing at a cloud.

**3. Nobody has pointed a robot at a room yet.** Everything above is measurement. The interesting
question — whether a 3B model usefully describes a dim desk through a 0.3 MP sensor — has not been
asked.

## Why this matters more than it looks

Vision is the one capability where "local" changes the nature of the thing rather than just the bill. A
camera that describes your room to a server you do not own is a different object from one that does it
on a card in the same room. The measurement says the second one costs about 4 GB.
