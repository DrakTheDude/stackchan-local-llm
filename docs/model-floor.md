# Which model do you actually need?

> **Status: one model verified, the floor not yet measured.** This page says what is known and what is
> guessed, and is marked so you can tell them apart. Measuring it properly is on
> [the roadmap](roadmap.md).

## What is verified

**Qwen3-32B at 4-bit, on a 24 GB GPU.** Tool calling reliable, 33–34 tokens/second, and the basis of
every subjective judgement elsewhere in these docs. That is the configuration the reference robot has
been running daily.

That is the whole of the verified column. Everything below is reasoning, not measurement.

## What actually matters

Not prose quality. **Tool calling.**

A model that writes charming sentences and cannot call a tool turns this robot into a speaking clock —
it will answer questions about your house from its own imagination rather than from the tools you gave
it, and it will sound just as confident doing it. That failure is worse than a model that says "I don't
know", because you cannot hear the difference.

So when you test a smaller model, do not ask it to chat. Ask it something that requires a tool, and
check it actually called one. The server logs every tool call:

```bash
docker compose logs xiaozhi | grep -i "执行工具"    # "executing tool"
```

Three things to watch for, in the order they break:

1. **It stops calling tools** and answers from memory instead.
2. **It calls the wrong one**, or invents arguments.
3. **It calls one and then ignores the result**, narrating something else.

## What to expect, unmeasured

Community experience and the shape of the problem both suggest smaller models lose tool calling before
they lose fluency — which is the worst possible order for this use. Treat the following as hypotheses
to test, not guidance to follow:

| size | expectation |
|---|---|
| ~30B | likely fine — closest to the verified configuration |
| ~14B | the interesting one. Probably the smallest that is dependable, and it fits a 12 GB card |
| ~8B | may work with simple tools and few of them; expect the failures above under pressure |
| ~4B | expected to fail at tools. Worth running once so the failure is documented rather than assumed |

⚠️ **The chat template matters as much as the size.** A model that "cannot call tools" is often a model
whose template was not applied — llama.cpp needs `--jinja`, and Ollama depends on the tag's own
template. Rule that out before blaming the model.

## Helping

If you run a smaller model, the numbers worth reporting are: the model and quantisation, whether tool
calls happened at all, how it failed when it failed, and tokens/second. Failures are more useful than
successes here — an honest "8B could not do it" saves everyone else the evening.
