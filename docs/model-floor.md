# Which model do you actually need?

> **Status: measured.** Fifteen models, on one RTX 4090, through Ollama at its defaults, with the
> shipped config's sampling settings. Every number here came out of
> [`tools/model-bench/bench.py`](../tools/model-bench/bench.py), which you can run against your own
> machine. The table below replaces a page of hypotheses, and it is worth saying that **most of those
> hypotheses were wrong**.

## The short answer

Measured on two cards: an RTX 4090 (24 GB) and an RTX 5060 Mobile (8 GB). They disagree, and both are
right — which is why this is a matrix rather than a recommendation.

| your card | chat model | why | with an eye too |
|---|---|---|---|
| **8–12 GB** | **`qwen3:8b`**, reasoning **off** | 100% on all 21 cases, fits in 5.6 GB, speaks in 0.08 s | ⚠️ **at 8 GB, not at the same time** — vision evicts it. Either `qwen3:8b` and no eye, or `qwen3:4b` + `qwen2.5vl:3b` at 6.1 GB. Measured, in [vision.md](vision.md) |
| **12–20 GB** | **`mistral-nemo:12b`** | 100%, speaks in 0.09 s, and does not reason at all so there is nothing to switch off | `+ qwen2.5vl:3b` → 16.5 GB total |
| **20 GB and up** | **`mistral-nemo:12b`** still | the extra card buys vision and headroom, not a better talker | `+ qwen2.5vl:3b`, comfortably |

```bash
./server/use-model.sh qwen3:8b          # or mistral-nemo:12b
```

📷 **The "with an eye too" column is measured, and the numbers behind it are in
[vision.md](vision.md#it-fits-alongside-the-chat-model--above-8-gb)** — VRAM per vision model, cost per
photo warm, which pairs stay resident together and which get evicted. It lives there rather than here
because it is the same benchmark asking a different question, but this is where people come looking
for it, so: that way.

> These are floors, not verdicts. Everything that shapes how the robot *talks* is fitted to
> `mistral-nemo:12b`, so changing the model hands you the tuning too — see
> [If you change the model, the tuning comes with it](#if-you-change-the-model-the-tuning-comes-with-it).

**The 8 GB row needs reasoning off, and there is a one-line way to do it.** With reasoning on,
`qwen3:8b` sits silent for **4.3 seconds** before speaking on that card, against 0.08 s without — for
an identical tool score. None of the documented switches work through the OpenAI-compatible endpoint
the server speaks, so build a variant with it pinned off:

```bash
./server/no-think.sh qwen3:8b     # creates qwen3:8b-nothink
./server/use-model.sh qwen3:8b-nothink
```

Verified rather than assumed: the pinned variant scores **21/21, speaks in 0.07 s and runs at 130
tok/s** — identical tool behaviour, none of the wait. Editing a chat template is exactly how tool
calling quietly breaks, so re-run the bench after building one.

**Do not put `mistral-nemo:12b` on 8 GB.** It is the best model here on a big card and it needs 6.1 GB
of weights plus its context; Ollama fits 78% of it and runs the rest on the CPU. The result is **17.6
tok/s against 101** on a 24 GB card — the same model, six times slower, with nothing in the logs
saying so.

### The two cards, same models

| | RTX 4090, 24 GB | RTX 5060 Mobile, 8 GB |
|---|---|---|
| `qwen3:8b` *no-think* | 100%, 132 tok/s, 10.0 GB | 100%, 27 tok/s, 5.6 GB |
| `mistral-nemo:12b` | 100%, 101 tok/s, 12.4 GB | 86%, **17.6 tok/s**, ⚠️ 78% fit |
| `granite4:tiny-h` | 83%, 122 tok/s, 4.7 GB | 90%, 84 tok/s, 4.4 GB |
| `granite4:micro` | 83%, 209 tok/s, 5.0 GB | 71%, 84 tok/s, 2.5 GB |
| `qwen3:4b` *no-think* | 78%, 212 tok/s, 7.5 GB | 81%, 50 tok/s, 3.2 GB |

📷 **Vision is benchmarked the same way, on the same two cards** — see
[vision.md](vision.md#at-8-gb-it-is-a-choice-and-it-was-measured-on-a-real-8-gb-card). The short
version: on 24 GB an eye is an addition, on 8 GB it is a choice, because loading it evicts any chat
model that does not leave room.

Three things only a second card could show:

**VRAM is not a property of the model.** `granite4:micro` took 5.0 GB on the 4090 and 2.5 GB on the
5060 — Ollama sizes its context to the card it finds. So a "needs X GB" figure measured on a big card
*overstates* what a small one needs, which is the friendlier direction to be wrong in but still wrong.

**Reasoning costs more on slower hardware, disproportionately.** `qwen3:8b` waits 2.24 s before
speaking on the 4090 and 4.26 s on the 5060. The models most suited to modest cards are exactly the
ones where reasoning hurts most.

**A mobile card is not a desktop card with fewer gigabytes.** The 5060 ran a 3B at 84 tok/s against the
4090's 209 — 40%, roughly the memory-bandwidth ratio. Everything scales down together.

### 🔴 How much of this table is noise

The 5060 was swept twice, a day apart — same card, same models, nothing changed between them. So
everything that moved is measurement noise, and now it has a number:

| column | run-to-run swing | read it as |
|---|---|---|
| **tokens/second** | **under 1%** | **real.** A difference here is a difference |
| tool score | ±2 cases in 21 (−12% to +11%) | **one run is one sample.** `granite4:tiny-h` scored 90%, then 100% |
| time to first spoken word | −18% to **+74%** | the *shape* is real — reasoning costs seconds, not reasoning does not — the figure is not |
| cold load | −67% to +100% | disk cache, nearly meaningless |

So **compare cards by tokens/second, and read tool percentages as a band rather than a number.** Two
models four points apart on this page are tied. The 100%-against-83% gaps are real; the
90%-against-86% ones are not — and this page previously invited both readings equally.

⚠️ It is also the floor any *experiment* against this bench has to clear. Something that moves
tokens/second by 5% is visible. Something that claims two more tool cases has measured nothing.

## The greeting is the test nobody writes

Before the table: the single most useful case in this bench came from a robot, not from the bench.

`mistral-nemo:12b` scored **100% on every case** here and was then unusable on hardware — it answered
the wake word by calling a `help` tool and reading fifty-three sentences of feature menu aloud, emoji
headers and all. Ten seconds before it said anything anybody wanted.

The firmware sends the wake phrase to the model **as though you had spoken it**, so every conversation
opens with it. It is the most frequent input in the entire system and it was not in the test suite,
because it is too obvious to think of. There is a `greeting` case now.

Two things made it fire, and only one was the model:

- The tool's own description said to call it when the user asks *"how to get started"* or *"wants the
  tour"* — which is what a greeting looks like. The fix was to **withhold the tool from the model**
  rather than instruct against it.
- The result was a markdown document. The robot's persona says *"plain English only, no markdown, no
  bullet points, no emoji"* — but the markdown arrived in the **tool result**, where no instruction
  reaches it. A tool whose output cannot be spoken is wrong for a voice robot even when calling it was
  right.

## What actually matters

Not prose quality. **Tool calling.**

A model that writes charming sentences and cannot call a tool turns this robot into a speaking clock —
it answers questions about your house from its own imagination rather than from the tools you gave it,
and it sounds just as confident doing it. That failure is worse than "I don't know", because you cannot
hear the difference.

So the bench scores six things that have observable right answers — which tool was called, with which
arguments — and one case that exists to catch the opposite failure: a model that calls a tool for
*"tell me a story about a lighthouse"* is not being thorough, it is unusable.

## Everything measured

<!-- BENCH TABLE START -->
| model | shape | tools | speaks after | tok/s | VRAM | fits |
|---|---|---|---|---|---|---|
| `qwen3:8b`<br>*thinking off* | dense, 8B | **100%** | 0.07 s | 125.0 | 10.0 GB | 12 GB |
| `mistral-nemo:12b` | dense, 12B | **100%** | 0.09 s | 101.4 | 12.4 GB | 16 GB |
| `qwen3:14b`<br>*thinking off* | dense, 14B | **100%** | 0.1 s | 80.1 | 14.5 GB | 24 GB |
| `qwen3:30b-a3b`<br>*thinking off* | MoE, 30B / 3B active | **100%** | 0.1 s | 206.6 | 21.7 GB | 24 GB |
| `granite4:small-h` | MoE, 32B / 9B active | **100%** | 0.32 s | 59.2 | 20.4 GB | 24 GB |
| `gpt-oss:20b` | MoE, 21B / 3.6B active | **100%** | 0.34 s | 154.3 | 12.9 GB | 16 GB |
| `qwen3:32b`<br>*thinking off* | dense, 32B | **100%** | 0.52 s | 9.3 | 22.9 GB<br>⚠️ 79% on GPU | more than 24 GB |
| `qwen3:30b-a3b` | MoE, 30B / 3B active | **100%** | 1.07 s | 210.0 | 21.7 GB | 24 GB |
| `qwen3:8b` | dense, 8B | **100%** | 2.24 s | 138.2 | 10.0 GB | 12 GB |
| `qwen3:32b` | dense, 32B | **100%** | 17.38 s | 9.5 | 22.9 GB<br>⚠️ 79% on GPU | more than 24 GB |
| `granite4:micro` | dense, 3B | **83%** | 0.05 s | 209.3 | 5.0 GB | 6 GB |
| `granite4:tiny-h` | MoE, 7B / 1B active | **83%** | 0.15 s | 122.4 | 4.7 GB | 6 GB |
| `qwen3:14b` | dense, 14B | **83%** | 1.76 s | 83.1 | 14.5 GB | 24 GB |
| `qwen3:4b` | dense, 4B | **78%** | 4.35 s | 212.0 | 7.5 GB | 12 GB |
| `qwen3:4b`<br>*thinking off* | dense, 4B | **67%** | 0.11 s | 205.8 | 7.5 GB | 12 GB |
| `mistral-small3.2:24b` | dense, 24B | **67%** | 0.15 s | 54.1 | 19.6 GB | 24 GB |
| `llama3.1:8b` | dense, 8B | **61%** | 0.06 s | 137.5 | 9.2 GB | 12 GB |
| `granite3.3:8b` | dense, 8B | **22%** | 0.08 s | 120.3 | 10.6 GB | 12 GB |
| `gemma3:12b` | dense, 12B | *refused* | — | 81.0 | 8.1 GB | 12 GB |
| `deepseek-v2:16b` | MoE, 16B / 2.4B active | *refused* | — | 237.6 | 20.4 GB | 24 GB |

- `gpt-oss:20b` does not honour `think: false` — it has its own reasoning control, so there is no *thinking off* row for it.
- `qwen3:32b` did **not fit**: only 79% of it was on the card and the rest ran on system RAM, which is the whole explanation for its single-digit tok/s. That is a fact about a 24 GB card, not about the model — llama.cpp runs the same weights at 37.9 tok/s with a tighter quantisation.
- `gemma3:12b` has **no tool support** in this runtime — every request came back `HTTP 400`. Its speed is measured without tools; it has no score because it was never allowed to try.
- `deepseek-v2:16b` has **no tool support** in this runtime — every request came back `HTTP 400`. Its speed is measured without tools; it has no score because it was never allowed to try.
<!-- BENCH TABLE END -->

### How much to trust a number

Three repeats per case, eighteen scored attempts per model, at the shipped temperature of 0.75 — so
these are samples, not constants. One model here was measured twice by accident: `qwen3:4b` with
reasoning off scored **78% and then 67%** on two independent runs.

Read the table accordingly. **100% and 22% mean what they say**; a score in the middle is worth about
±1 case per row, and the useful detail is *which* cases failed rather than the total. `qwen3:4b` fails
`direct` and `indirect` every time — it will not call a setter with a parameter — and that is a stable
fact about the model, while whether it scores 12 or 14 out of 18 is not.

`tools` is the share of 18 scored attempts that did the right thing. **`speaks after` is the one to
read for a robot you talk to**: not time to first token, but time to the first word the speaker would
actually utter. For a reasoning model those are wildly different numbers, and the gap is silence.

## Three things the measurements overturned

**Size is not the variable.** `mistral-small3.2:24b` scored **67% while using 19.6 GB** — worse than a
3B model at a quarter of the card. `granite3.3:8b` managed 22%. Meanwhile `granite4:micro` gets 83% in
5.0 GB. Tool *training* is what separates these, and it does not track parameter count at all.

**Small MoE did not beat small dense.** This was the interesting hypothesis and it is the one with a
clean experiment behind it: `granite4:tiny-h` (MoE, 1B active) against `granite4:micro` (dense 3B) —
same lab, same training, same day. They score **identically at 83%**, and the MoE is *slower* per token,
122 against 209. Every other MoE-versus-dense comparison also changes the lab and the training data, so
it can rank models but cannot answer the question. MoE earns its keep higher up — `gpt-oss:20b` reaches
100% in 12.9 GB, and `qwen3:30b-a3b` runs at 210 tok/s where the dense 32B manages 38 — but "small MoE
is the way" did not survive contact with the measurement.

**What speed actually tracks is *active* parameters**, which is the whole mechanism and worth stating
plainly, because it makes the table predictable:

| active | model | tok/s |
|---|---|---|
| 2.4B | `deepseek-v2:16b` (MoE, 16B total) | **237.6** |
| 3B | `qwen3:30b-a3b` (MoE, 30B total) | 210.0 |
| 3B | `granite4:micro` (dense) | 209.3 |
| 4B | `qwen3:4b` (dense) | 212.0 |
| 8B | `qwen3:8b` (dense) | 138.2 |
| 12B | `mistral-nemo:12b` (dense) | 101.4 |
| 24B | `mistral-small3.2:24b` (dense) | 54.1 |

A 30B model generating at the speed of a 4B, because only 3B of it is read per token. Dense has no such
lever — it slows steadily as it grows. So the honest summary of the trade is that **MoE converts VRAM
into quality at constant speed, where dense converts VRAM into quality at falling speed**: `qwen3:4b`
and `qwen3:30b-a3b` both run at ~210 tok/s, and the extra 14 GB bought a far better model rather than a
faster one.

The exception proves it is about active size rather than about being MoE: `granite4:small-h` has **9B
active and manages 59 tok/s** — slower than every dense 8B measured here.

**The old guesses on this page were backwards in the middle.** It said ~14B was "probably the smallest
that is dependable" and ~4B was "expected to fail". In fact `qwen3:8b` scores **100%** while
`qwen3:14b` manages only 83% as shipped, and `qwen3:4b` reaches 78% rather than failing outright. The
14B's every error was running out of its token budget while thinking — which is the next section.

**And the biggest model here could not be measured properly at all.** `qwen3:32b` needed 22.9 GB and
Ollama could fit only 79% of it on a 24 GB card, running the rest on system RAM at 9.5 tok/s. That is a
fact about the card, not the model — llama.cpp runs the same weights at 37.9 tok/s with a tighter
quantisation. A 32B dense model is not a 24 GB model in practice, whatever the file size suggests.

## Reasoning costs you the conversation

Models that think before answering pay for it in silence, and on these tasks they bought nothing:

| | thinking on | thinking off |
|---|---|---|
| `qwen3:8b` | 100%, speaks after **2.24 s** | 100%, speaks after **0.07 s** |
| `qwen3:14b` | **83%**, speaks after 1.76 s | **100%**, speaks after 0.09 s |
| `qwen3:4b` | 78%, speaks after **4.35 s** | 78%, speaks after 0.11 s |
| `qwen3:30b-a3b` | 100%, 1.07 s | 94%, 0.07 s |
| `qwen3:32b` | 100%, speaks after **17.38 s** | 100%, 0.50 s |

Up to seventeen seconds of a robot staring at you. On a screen you would see a spinner and know to
wait; out loud there is nothing at all, and he looks like he did not hear you.

And on `qwen3:14b` — the model this project ships by default — **reasoning made it worse**: 83% with
thinking on, 100% with it off. Every one of its failures was running out of the token budget
mid-thought and returning empty, which is silence rather than a wrong answer.

🔴 **And the switch everyone quotes does not work.** Measured on `qwen3:8b`, same question, same budget:

| how `/no_think` was sent | reply | characters thought |
|---|---|---|
| nothing (baseline) | 2.10 s | 916 |
| in the **system** prompt | 3.03 s | **1631** — it thought *more* |
| in the **user** message | 1.55 s | 701 |
| `think: false` to the **OpenAI-compatible** endpoint | 3.07 s | 861 — accepted and ignored |
| `think: false` to Ollama's **own** `/api/chat` | **0.33 s** | **0** |

Only the last one works. The third row matters to this project specifically: **the server's Ollama
provider prepends `/no_think` to the user's message for any model named `qwen3*`** and then calls the
OpenAI-compatible endpoint. So today, on the shipped default, the robot is thinking while the software
believes it is not — and the instruction is also visible to the model as part of what you said.

That is why the recommendation above prefers a model that does not reason at all — and why, when you
do want a Qwen, the switch has to be pinned into the model rather than asked for.

**The reason every prompt trick fails is in the chat template.** Its no-think branch is gated on
`$.Think`, a variable only Ollama's native API sets, so no wording in any message can reach it. But a
template is just text, and a derived model can pin that branch permanently — which is what
[`server/no-think.sh`](../server/no-think.sh) does. It needs no patch to the server and nothing kept in
step with upstream, and it works for any caller in any dialect.

## A model can also be refused rather than bad

`gemma3:12b` is in the table with **no score at all**, because every request came back `HTTP 400`:
Ollama will not send tools to a model whose template has no tool support. Scored naively that is 0%,
which reads as "tried and failed" and would be a slur on a capable model. It was never allowed to try.

⚠️ **The chat template matters as much as the size.** A model that "cannot call tools" is often a model
whose template was not applied — llama.cpp needs `--jinja`, Ollama depends on the tag's own template.
Rule that out before blaming the model.

## Run it on your own machine

```bash
python3 tools/model-bench/bench.py \
    --base-url http://127.0.0.1:11434/v1 \
    --ollama-host http://127.0.0.1:11434 \
    --model qwen3:8b --repeats 3
```

Standard library only, no dependencies. `--ollama-host` adds how much card the model took and whether
all of it fit; `--no-think` measures the same model with reasoning off, and **refuses to run** against
anything that cannot honour it rather than quietly reporting a thinking model as a fast one.

Then swap the robot over with one command:

```bash
./server/use-model.sh <tag>
```

## If you change the model, the tuning comes with it

The table above answers *will it work*. It does not answer *will it sound right*, and those are
different questions with different answers.

**Everything in this repo that shapes how the robot talks was fitted to `mistral-nemo:12b`** — the
persona, the few-shot examples, the wording of every tool description, and a fair slice of
[`server/patches/`](../server/patches/). None of it was designed in the abstract. It was written by
watching one model get things wrong and closing the gap, one behaviour at a time:

| what it did | what it took |
|---|---|
| spoke `TOOL_CALLS]` aloud at the start of every tool turn | a regex in the markdown cleaner — it is Mistral's own token, leaking as text |
| read the camera's notes back word for word | stopping the tool result from *being* a sentence, and giving it a worked example instead |
| invented `[Image](https://via.placeholder.com/…)` for a photo already on its screen | telling it there is no file and no link, plus two fixes in the TTS path |
| answered a question about a photograph with fabricated cluster health | still open — see below |

Swap in another model and **you inherit that job**, because the quirks are not shared. A model that
scores identically on the bench will have its own: a different stray token, a different tic, a
different way of being too formal or too chatty. None of that shows up in a tool-call score.

This is the fun part, and it is genuinely the part where the robot becomes yours. The three places
worth knowing:

- **The persona** — `data/.config.yaml`, the `prompt:` block. Voice, length, what it does with numbers.
- **Tool descriptions** — in the board file, next to each `AddTool`. These matter far more than they
  look: they are the only instructions the model gets at the moment it decides what to do.
- **The patch kit** — `server/patches/`. For behaviour no prompt can reach, because it happens in the
  server rather than in the model.

> 🔑 **The one lesson that transfers between models: a demonstration beats an instruction.** It came
> up five separate times building this — the wake word, the goodbye, the greeting, the recited tool
> result, the photo description. Every time, a rule written in the prompt lost to an example that
> contradicted it, and every time the fix was to change the example rather than to word the rule more
> firmly. If the model keeps doing something you have explicitly forbidden, stop rewording the
> prohibition and go find what is demonstrating the opposite.

**What we run, and why it is not the top of the table.** `mistral-nemo:12b`, for variety rather than
for obedience — it is more entertaining to live with, and a desk robot you enjoy talking to is doing
its job. It is a 12B, though, and the known cost is the last row above: asked what was in a
photograph, it once answered with confident, invented fleet health. Charming quirks and wrong numbers
are not the same thing, and if you point this robot at something where the numbers matter, that is the
behaviour to watch for and constrain.

---

## Hearing you: the ASR side

This page measured chat models for a long time before anybody measured the thing in front of them.
Speech recognition turned out to be the weakest link on the reference robot — *firmware* came back as
*fern butter*, on a voice the model had already been tuned against.

| | model | where | VRAM | why |
|---|---|---|---|---|
| **default** | `Systran/faster-whisper-small.en` | CPU, int8 | none | runs anywhere, leaves the GPU entirely to the model. The right default and the reason it was chosen |
| **upgrade** | `Systran/faster-whisper-medium.en` | GPU, float16 | ~1.5 GB | a real gain on accented speech and on quieter, further-away talking. Worth it the moment you have the headroom |

The upgrade is four environment variables — see the comment above the `whisper` service in
[`server/docker-compose.yml`](../server/docker-compose.yml).

🔴 **Pull the model before you point at it.** `speaches` does **not** download on demand: it serves
what is in its cache and returns **404** for anything else. Change the model name alone and you get a
container that starts cleanly, reports CUDA, logs no errors, and fails *every* transcription — the
only symptom being a robot that has stopped hearing you. Check what it actually has:

```bash
docker compose exec whisper curl -s localhost:8000/v1/models
```

⚠️ **An `.en` model is a structural choice, not a size one.** `large-v3-turbo` is better again and
barely larger — and it is multilingual, so with no language pinned it will sometimes decide you are
speaking Japanese. An English-only model cannot do that at all. If you go multilingual, pinning the
language stops being optional.

**Numbers from a second machine are the useful thing here too**, and this table has one row measured
on one card. If you run the upgrade on something smaller, the interesting figures are the VRAM it
actually takes alongside your chat model, and whether it still keeps up.

---

## Helping

Numbers from a different card are the most useful thing you can send. The bench writes a JSON file per
model; that file plus your GPU is a complete report. **Failures are worth more than successes** — an
honest "this one could not do it on 8 GB" saves everyone else the evening.

---

### How these were measured, and what nearly went wrong

Recorded because each of these produced a confident wrong number first, and the same traps are waiting
for anyone who repeats this:

- **`max_tokens` too small silently gags a reasoning model.** At 300 tokens, `qwen3:30b-a3b` spent the
  whole budget thinking and returned `finish_reason: length` with **empty content** — scored as
  "answered from its own head". It measured 56%; it is a 100% model. The bench now uses the shipped
  config's 1200 and reports truncation as truncation.
- **Single-turn scoring punishes a model for being careful.** Asked to set the volume, a good model may
  first read the current one. In the robot that is one turn of a loop; scored single-turn it looks like
  a miss. A read-only first call now earns a second turn with the reading fed back.
- **Time-to-first-token flatters reasoning models.** Their first token arrives in 0.1 s and is a
  thought. The figure quoted here is time to the first *spoken* word.
- **The first request of a run measures your disk.** 5.92 s to first token was 4.6 s of model loading.
  There is a warm-up call now, and cold load is its own field.
- **A refused request is not a score of zero.** See `gemma3:12b` above.
