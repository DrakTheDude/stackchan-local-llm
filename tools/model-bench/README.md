# model-bench

**Measures whether a model can actually run this robot**, which is a question about
tool calling rather than about prose.

```bash
# one model
python3 bench.py --base-url http://127.0.0.1:11434/v1 \
                 --ollama-host http://127.0.0.1:11434 --model qwen3:8b

# everything Ollama has, then regenerate the table in docs/model-floor.md
./sweep.sh "RTX 4090 24GB"
```

Standard library only. It works against anything OpenAI-compatible — Ollama,
llama.cpp, vLLM — and `--ollama-host` adds the two things only Ollama will tell
you: how much card the model took, and **whether all of it fitted**.

## What it reports

| | |
|---|---|
| `tools` | share of scored attempts that called the right tool with sane arguments |
| `speaks after` | time to the first **spoken** word — see below |
| `tok/s` | median, warm |
| `VRAM` | and a loud warning if part of the model ran on the CPU |

Six cases, three repeats each. Five of them require a tool call; the sixth
requires *not* calling one, because a model that reaches for a tool when asked
for a bedtime story is as unusable as one that never reaches at all.

## Why "speaks after" and not TTFT

A reasoning model emits its first token almost immediately — and that token is
*thought*. The robot stays silent through all of it, sometimes for seventeen
seconds. Time-to-first-token would rank that model as responsive. The figure
reported here is the first token the speaker would actually utter, and the gap
between the two is recorded as `silence_s`.

## Turning reasoning off

`--no-think` goes through Ollama's own `/api/chat`. That is not a preference —
it is the only place the switch was found to work:

| how it was sent | reply | chars thought |
|---|---|---|
| nothing (baseline) | 2.10 s | 916 |
| `/no_think` in the system prompt | 3.03 s | **1631** — *more* |
| `/no_think` in the user message | 1.55 s | 701 |
| `think: false`, OpenAI-compatible endpoint | 3.07 s | 861 — ignored |
| `think: false`, Ollama `/api/chat` | **0.33 s** | **0** |

A model that ignores the flag anyway — `gpt-oss` has its own reasoning control —
is **recorded as having ignored it**, and `make-table.py` leaves it out of the
table rather than crediting it with speed it never had.

## Reading the results

One JSON per model, committed, because they are the evidence behind
[docs/model-floor.md](../../docs/model-floor.md). Regenerate that page's table
from them with:

```bash
python3 make-table.py
```

## If you run this on your own machine

Results from a different card are the most useful thing you can contribute — the
whole table is currently one GPU. Send the JSON files and the label; failures are
worth more than successes.
