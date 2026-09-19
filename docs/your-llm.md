# Standing up your own model

The robot's server needs **one thing**: an OpenAI-compatible endpoint that can call tools. Where that
runs is entirely up to you — a container on this machine, an app on your Mac, a box down the hall.
This page is the per-platform mechanics, and the one command that tells you whether it is actually
using your GPU.

> ⚠️ **The failure to expect is silence about the CPU.** Every runtime here will happily run a model
> on your processor if the GPU path is not working, at a fifth of the speed, with nothing in any log
> saying so. It looks like "this model is slow" rather than "my setup is wrong". The check at the
> bottom of this page exists for exactly that.

## Pick your path

| you have | run Ollama | how the server reaches it |
|---|---|---|
| **NVIDIA** on Linux or Windows/WSL2 | in the container we ship | `COMPOSE_PROFILES=gpu-nvidia` — nothing to do |
| **Apple Silicon** | natively, from ollama.com | `COMPOSE_PROFILES=` and `base_url: http://host.docker.internal:11434/v1` |
| **AMD** | natively | same as Apple Silicon |
| **anything else** | wherever you like | point `base_url` at it |

🔴 **If you are not on NVIDIA, empty `COMPOSE_PROFILES`.** The bundled container asks Docker for an
`nvidia` device, and that is a hard failure rather than a slow fallback — the stack will not start at
all. Neither a Mac nor Windows-with-AMD can pass a GPU into a container anyway, which is why the
answer on both is to run Ollama on the host.

## Apple Silicon

Docker on a Mac cannot see the GPU. Ollama's native app can, through Metal, and unified memory means
the "VRAM" figures in [the model floor](model-floor.md) read differently: a 32 GB Mac can hold models
that need a 24 GB card, at lower throughput.

```bash
brew install ollama          # or the .dmg from ollama.com
ollama serve                 # the app does this for you if you installed the .dmg
ollama pull mistral-nemo:12b
```

Then, in `server/.env`:

```bash
COMPOSE_PROFILES=
```

and in `server/data/.config.yaml`, under the `Ollama:` entry:

```yaml
base_url: http://host.docker.internal:11434/v1
```

`host.docker.internal` is how a container reaches the machine it is running on. It resolves on Docker
Desktop by default; our compose asks for it explicitly so it works on Linux too.

## AMD

Same shape as the Mac: run Ollama natively, empty the profile, point `base_url` at
`host.docker.internal`. Ollama ships ROCm support, and which cards it covers moves faster than any doc
can track — **so do not trust a table, run the check below.** A card the build does not recognise is
not an error, it is a quiet fall back to the CPU.

If you land on the CPU and want to stay on AMD, the Vulkan backend in llama.cpp is the usual next
step; it speaks the same OpenAI API, so nothing else in this project changes.

## Everything else

LM Studio, vLLM, llama.cpp, a machine down the hall. The requirements are:

- an OpenAI-compatible `/v1/chat/completions`
- **working tool calls** — this is the one that catches people, and it is more often the chat template
  than the model. llama.cpp needs `--jinja`; Ollama depends on the tag's own template
- reachable from the `xiaozhi` container: not `localhost`, which means the container itself

## The check that matters

Whatever you did above, this tells you whether it worked. It needs no GPU tooling and no vendor SDK —
just Python and the endpoint:

```bash
python3 tools/model-bench/bench.py \
    --base-url http://127.0.0.1:11434/v1 \
    --ollama-host http://127.0.0.1:11434 \
    --model mistral-nemo:12b --repeats 1
```

Three numbers, and what they mean:

| | good | bad |
|---|---|---|
| **tok/s** | tens to hundreds | single digits usually means CPU |
| **`% on GPU`** | silent, i.e. all of it | `⚠️ ONLY 78% FIT` means the rest is in system RAM |
| **tools** | 90–100% | under 60% and the robot will invent answers |

Measured reference points, so you have something to compare against: a 3B runs at **209 tok/s** on an
RTX 4090 and **84 tok/s** on an RTX 5060 Mobile. If you see 8 tok/s for a small model, you are on the
CPU regardless of what any installer told you.

⚠️ **The partial-fit warning is the subtle one.** A model that does not quite fit is not refused — it
is split, with the remainder run on the processor, and it simply goes six times slower. On the
reference machines `mistral-nemo:12b` does 101 tok/s on a 24 GB card and **17.6 tok/s** on an 8 GB one
for this exact reason. Nothing else reports it.

## Then tell the robot

```bash
./server/use-model.sh mistral-nemo:12b
```

That pulls the model, points the config at it and restarts the pipeline — three steps that have to
stay in step. It works against a native Ollama as well as the bundled one.

Which model to pick is [the model floor](model-floor.md), which is measured on two cards rather than
guessed.

## If your model reasons before answering

Qwen3 and similar think first, and on modest hardware that is the biggest delay you will feel — 4.3
seconds of silence on an 8 GB card before the first word, for the same answer. If you picked one of
those, build a variant with it switched off:

```bash
./server/no-think.sh qwen3:8b
./server/use-model.sh qwen3:8b-nothink
```

The script explains why a prompt cannot do this, with the measurements. Check tool calling afterwards —
it edits a chat template, and that is exactly how tool calls quietly stop working.
