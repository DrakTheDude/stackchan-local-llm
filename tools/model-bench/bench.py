#!/usr/bin/env python3
"""Measure whether a model can actually run this robot.

🔴 THIS DOES NOT SCORE PROSE. It scores TOOL CALLING, because that is what a
   smaller model loses first and it loses it silently: a model that stops calling
   tools does not say so, it answers from its own head in the same confident
   voice. From across the room you cannot tell "the volume is 71" from a guess.

   So every case has a right answer that is observable in the API response -
   which tool was called, with what arguments - rather than a judgement about
   wording.

⚠️ AND ONE CASE EXISTS TO CATCH THE OPPOSITE FAILURE. A model that calls a tool
   for "tell me a story" is not being helpful, it is being unusable: the robot
   stops mid-sentence to fiddle with its own brightness. Over-calling is scored
   as harshly as under-calling.

Run against anything OpenAI-compatible:

    python3 bench.py --base-url http://127.0.0.1:8080/v1 --model qwen3-32b
    python3 bench.py --base-url http://127.0.0.1:11434/v1 --model qwen3:8b

It reports four things per model:

    tools                 share of scored attempts that did the right thing
    time to first SPOKEN  word, which is not time to first token - a reasoning
                          model emits its first token at once and says nothing
    tok/s                 median, warm
    VRAM                  and whether all of it fitted, via --ollama-host

⚠️ FIVE WAYS THIS MEASUREMENT LIES, each found by publishing a wrong number
   first. They are worth knowing before trusting any tool of this kind:

   1. Too small a max_tokens gags a reasoning model. It spends the budget
      thinking, returns EMPTY with finish_reason=length, and scores as though it
      ignored its tools. One model read 56% that way and is a 100% model.
   2. Single-turn scoring punishes prudence. A model that reads the volume
      before setting it is mid-loop, not wrong; here a read-only first call
      earns a second turn.
   3. Time-to-first-token flatters reasoners, for the reason above.
   4. The first request of a run measures your disk. Everything here is warm,
      and the cold load is its own field.
   5. A refused request is not a score of zero. Ollama returns HTTP 400 for a
      model whose template has no tool support; that model was never allowed to
      try, and printing 0% would be a slur rather than a result.

Writes JSON next to itself and prints a table. No dependencies beyond the
standard library, so it runs on a laptop without setting anything up.
"""

import argparse
import json
import statistics
import sys
import time
import urllib.error
import urllib.request

# The robot's real tools, trimmed to the shape the server presents. Descriptions
# matter - they are most of what the model has to go on - so they are kept close
# to the originals rather than tidied.
TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "self_get_device_status",
            "description": (
                "Provides the real-time information of the device, including the current "
                "status of the audio speaker, screen, battery, network, etc. Use this tool "
                "for: 1. Answering questions about current condition (e.g. what is the "
                "current volume of the audio speaker?) 2. As the first step to control the "
                "device (e.g. turn up / down the volume of the audio speaker, etc.)"
            ),
            "parameters": {"type": "object", "properties": {}},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "self_audio_speaker_set_volume",
            "description": (
                "Set the volume of the audio speaker. If the current volume is unknown, you "
                "must call `self_get_device_status` tool first and then call this tool."
            ),
            "parameters": {
                "type": "object",
                "properties": {"volume": {"type": "integer", "minimum": 0, "maximum": 100}},
                "required": ["volume"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "self_screen_set_brightness",
            "description": "Set the brightness of the screen.",
            "parameters": {
                "type": "object",
                "properties": {"brightness": {"type": "integer", "minimum": 0, "maximum": 100}},
                "required": ["brightness"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "self_robot_set_head_angles",
            "description": (
                "Point the robot's head. pan is left/right in degrees, negative is left, "
                "range -70 to 70. tilt is up/down in degrees, range -35 to 35."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "pan": {"type": "number"},
                    "tilt": {"type": "number"},
                },
                "required": ["pan", "tilt"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "self_camera_show_photo",
            "description": (
                "Take a photo with the robot's camera and show it on the robot's own screen. "
                "Use this when someone asks you to take a picture or show them what you can see."
            ),
            "parameters": {"type": "object", "properties": {}},
        },
    },
]

SYSTEM = (
    "You are Stack Chan, a small robot on someone's desk. You are spoken to out loud and "
    "your replies are read aloud, so keep them to one or two short sentences. "
    "You have tools for controlling yourself. Use them rather than guessing: an answer you "
    "made up sounds exactly like one you looked up."
)

# (id, prompt, expected tool or None, argument check or None)
CASES = [
    (
        "direct",
        "Turn your volume up to eighty.",
        "self_audio_speaker_set_volume",
        lambda a: isinstance(a.get("volume"), (int, float)) and 70 <= a["volume"] <= 90,
    ),
    (
        "indirect",
        "It's really dark in here and I can barely see your screen.",
        "self_screen_set_brightness",
        lambda a: isinstance(a.get("brightness"), (int, float)),
    ),
    (
        "two-args",
        "Look over to your left for a second.",
        "self_robot_set_head_angles",
        # Negative pan is left, per the tool description. Getting the SIGN wrong
        # is the interesting failure - it means the description was not read.
        lambda a: isinstance(a.get("pan"), (int, float)) and a["pan"] < 0,
    ),
    (
        "no-args",
        "Take a picture of me.",
        "self_camera_show_photo",
        None,
    ),
    (
        "must-check",
        "How loud are you set to right now?",
        # The whole point: the model cannot know this. Answering without calling
        # is the exact failure that makes a robot confidently wrong.
        "self_get_device_status",
        None,
    ),
    (
        "must-not-call",
        "Tell me a two sentence story about a lighthouse.",
        None,
        None,
    ),
    (
        # 🔴 THE MOST FREQUENT PROMPT IN THE WHOLE SYSTEM, and it was missing.
        #
        #    The firmware sends the wake phrase to the model as if the user had
        #    spoken it (CONFIG_SEND_WAKE_WORD_DATA), so EVERY conversation opens
        #    with this exact string. A model that answers it by calling a tool
        #    has made the robot unusable before anyone has asked for anything.
        #
        #    Found on hardware, not here: a model that scored 100% on every case
        #    above answered the greeting by calling a `help` tool and reading 53
        #    sentences of menu out loud. Ten seconds of that before a word
        #    anybody wanted. The lighthouse case did not catch it, because
        #    "write me something" and "hello" are different invitations.
        "greeting",
        "Hi,Stack Chan",
        None,
        None,
    ),
]


# 🔑 A MODEL THAT LOOKS BEFORE IT ACTS IS NOT FAILING.
#
#    Asked to set the volume to eighty, a good model may first call
#    self_get_device_status to see where it is. In the real robot that is one
#    turn of a loop: the reading comes back and it calls the setter. Scored
#    single-turn, that careful model lands BELOW one that guesses - which is
#    precisely backwards, and it is what this table would have published.
#
#    So a read-only first call earns one more turn, with a plausible reading fed
#    back, and the SECOND answer is scored. Anything else would reward guessing.
READ_ONLY_TOOLS = {"self_get_device_status"}

# What the robot would actually have replied. Values are deliberately ordinary -
# a status that already satisfied the request would let a model pass by doing
# nothing.
READ_ONLY_RESULT = json.dumps({
    "volume": 40, "brightness": 50, "muted": False,
    "battery_pct": 82, "wifi": "connected",
})


# 🔴 THE ONLY WAY TO ACTUALLY STOP A QWEN3 THINKING, MEASURED.
#
#    `/no_think` is widely repeated as the switch. On this runtime it does
#    nothing: in the system prompt the model thought 1631 characters against a
#    916-character baseline - it thought MORE. Passing think:false to the
#    OpenAI-compatible endpoint is accepted and silently ignored, which is the
#    worst of the three because it looks like it worked.
#
#    Ollama's own /api/chat honours it, so that is the path taken, and the reply
#    is translated into the OpenAI shape rather than teaching the scorer a
#    second dialect.
#
# ⚠️ Any runtime without that endpoint therefore cannot answer this question,
#    and --no-think says so rather than quietly measuring a thinking model.
def call_native_no_think(ollama_host, model, messages, timeout, temperature, max_tokens):
    body = json.dumps({
        "model": model,
        "messages": messages,
        "tools": TOOLS,
        "think": False,
        "stream": False,
        "options": {"temperature": temperature, "num_predict": max_tokens},
    }).encode()
    req = urllib.request.Request(ollama_host.rstrip("/") + "/api/chat", data=body,
                                 headers={"Content-Type": "application/json"})
    start = time.monotonic()
    with urllib.request.urlopen(req, timeout=timeout) as r:
        native = json.load(r)
    elapsed = time.monotonic() - start

    msg = native.get("message") or {}
    # Native arguments arrive as an object; the scorer accepts either, and
    # leaving them as they came avoids a re-encode that could change them.
    calls = []
    for i, c in enumerate(msg.get("tool_calls") or []):
        fn = c.get("function") or {}
        calls.append({"id": c.get("id", f"call_{i}"),
                      "type": "function",
                      "function": {"name": fn.get("name"),
                                   "arguments": fn.get("arguments", {})}})
    thought = len(msg.get("thinking") or "")
    if thought:
        # Loud AND recorded. Printing it was not enough: the run still wrote a
        # file labelled "thinking: off", which is the kind of tidy lie a table
        # inherits without anyone noticing. gpt-oss:20b does not honour this
        # flag - it has its own reasoning-effort control - and the result must
        # say so rather than claim a mode it never entered.
        print(f"    !! think:false was ignored - {thought} chars of thought")

    payload = {
        "choices": [{
            "message": {"role": "assistant",
                        "content": msg.get("content") or "",
                        "tool_calls": calls or None},
            "finish_reason": "length" if native.get("done_reason") == "length" else "stop",
        }],
        "usage": {"completion_tokens": native.get("eval_count")},
    }
    payload["_thought_chars"] = thought
    return payload, elapsed


def call(base_url, model, messages, timeout, temperature, max_tokens, with_tools=True):
    # with_tools=False exists for one case: a model whose template has no tool
    # support, which the runtime rejects outright. Its SPEED is still a fact
    # worth having, and this is the only way to ask for it.
    body = json.dumps(
        {
            "model": model,
            "messages": messages,
            **({"tools": TOOLS} if with_tools else {}),
            "temperature": temperature,
            # 🔴 THIS IS THE SHIPPED VALUE, AND IT HAS TO BE.
            #
            #    At 300 a reasoning model spends the whole budget thinking and
            #    returns finish_reason=length with EMPTY content and no tool
            #    call - scored, before this was found, as "answered from its own
            #    head". qwen3:30b-a3b measured 56% that way and is not a 56%
            #    model; it never got to speak. The shipped config says 1200, so
            #    that is what gets measured.
            "max_tokens": max_tokens,
        }
    ).encode()
    req = urllib.request.Request(
        base_url.rstrip("/") + "/chat/completions",
        data=body,
        headers={"Content-Type": "application/json", "Authorization": "Bearer local"},
    )
    start = time.monotonic()
    with urllib.request.urlopen(req, timeout=timeout) as r:
        payload = json.load(r)
    elapsed = time.monotonic() - start
    return payload, elapsed


def residency(ollama_host, model):
    """How much card this model is using, and whether ALL of it fit.

    🔑 THE SPLIT IS THE INTERESTING NUMBER, not the size. Ollama will happily
       run a model that does not fit by keeping the rest in system RAM, and it
       does not warn you - it just gets several times slower. A reader comparing
       tok/s between two models has no way to see that one of them was half on
       the CPU unless it is written down, so it is written down.

    Returns {} if the host is not an Ollama one; every other runtime is welcome
    here and simply has no such endpoint.
    """
    try:
        req = urllib.request.Request(ollama_host.rstrip("/") + "/api/ps")
        with urllib.request.urlopen(req, timeout=10) as r:
            payload = json.load(r)
    except (urllib.error.URLError, TimeoutError, ValueError):
        return {}

    for m in payload.get("models", []):
        if m.get("name") != model and m.get("model") != model:
            continue
        total = m.get("size") or 0
        on_gpu = m.get("size_vram") or 0
        if not total:
            return {}
        return {
            "resident_gb": round(total / 1e9, 1),
            "vram_gb": round(on_gpu / 1e9, 1),
            "on_gpu_pct": round(100 * on_gpu / total),
        }
    return {}


# Measured on prose rather than on a tool call, because a tool call has no
# spoken part at all - the robot says nothing until the tool returns. This is
# the "he answers a question" case, which is what people mean by responsive.
TTFT_PROMPT = "Tell me a two sentence story about a lighthouse."


def time_to_speech(base_url, model, timeout, temperature, max_tokens, no_think):
    """Returns (first_token_s, first_spoken_word_s, total_s).

    Streams the reply and watches for two moments: anything at all arriving, and
    the first character that is not part of a reasoning block. Reasoning reaches
    us two ways depending on the runtime - a separate `reasoning` delta, or a
    <think> ... </think> span inside the content - so both are handled.
    """
    messages = [{"role": "system", "content": SYSTEM},
                {"role": "user", "content": TTFT_PROMPT}]
    if no_think:
        # Ollama's native stream: newline-delimited JSON, thinking in its own
        # field, and think:false actually obeyed.
        body = json.dumps({"model": model, "messages": messages, "think": False,
                           "stream": True,
                           "options": {"temperature": temperature,
                                       "num_predict": max_tokens}}).encode()
        req = urllib.request.Request(base_url.rstrip("/") + "/api/chat", data=body,
                                     headers={"Content-Type": "application/json"})
    else:
        body = json.dumps({
            "model": model, "messages": messages,
            "temperature": temperature, "max_tokens": max_tokens, "stream": True,
        }).encode()
        req = urllib.request.Request(
            base_url.rstrip("/") + "/chat/completions", data=body,
            headers={"Content-Type": "application/json", "Authorization": "Bearer local"})

    start = time.monotonic()
    first_any = first_speech = None
    in_think = False
    seen = ""
    with urllib.request.urlopen(req, timeout=timeout) as r:
        for raw in r:
            line = raw.decode("utf-8", "replace").strip()
            if no_think:
                # Native stream: one JSON object per line, no "data:" prefix.
                if not line:
                    continue
                try:
                    chunk = json.loads(line)
                except json.JSONDecodeError:
                    continue
                m = chunk.get("message") or {}
                if first_any is None and (m.get("content") or m.get("thinking")):
                    first_any = time.monotonic() - start
                if (m.get("content") or "").strip() and first_speech is None:
                    first_speech = time.monotonic() - start
                if chunk.get("done"):
                    break
                continue
            if not line.startswith("data:"):
                continue
            data = line[5:].strip()
            if data == "[DONE]":
                break
            try:
                chunk = json.loads(data)
            except json.JSONDecodeError:
                continue
            delta = (chunk.get("choices") or [{}])[0].get("delta") or {}
            if not delta:
                continue
            if first_any is None:
                first_any = time.monotonic() - start

            # Reasoning delivered in its own field is never spoken.
            if delta.get("reasoning") or delta.get("reasoning_content"):
                continue
            piece = delta.get("content") or ""
            if not piece:
                continue

            # Reasoning delivered inline, fenced in <think> tags.
            seen += piece
            if "<think>" in seen and "</think>" not in seen:
                in_think = True
            if in_think:
                if "</think>" in seen:
                    in_think = False
                    tail = seen.split("</think>", 1)[1]
                    if tail.strip() and first_speech is None:
                        first_speech = time.monotonic() - start
                continue
            if piece.strip() and first_speech is None:
                first_speech = time.monotonic() - start

    return first_any, first_speech, time.monotonic() - start


def evaluate(payload, expected_tool, arg_check):
    """Returns (ok, what_happened)."""
    try:
        msg = payload["choices"][0]["message"]
    except (KeyError, IndexError):
        return False, "no message in response"

    calls = msg.get("tool_calls") or []
    # ⚠️ SAY WHAT HAPPENED, NOT WHAT YOU ASSUME IT MEANS. A truncated reply and a
    #    model ignoring its tools are the same empty tool_calls list, and calling
    #    both "answered from its own head" sent this straight down the wrong
    #    road: the model had not answered at all, it ran out of budget mid-
    #    thought. The two need opposite fixes, so they get different words.
    truncated = payload.get("choices", [{}])[0].get("finish_reason") == "length"

    if expected_tool is None:
        if calls:
            return False, f"called {calls[0]['function']['name']} when it should have answered"
        if truncated:
            # It passed by saying nothing, which is not the same as passing.
            return False, "ran out of max_tokens - cannot tell whether it would have called"
        return True, "answered without calling a tool"

    if not calls:
        if truncated:
            return False, ("ran out of max_tokens before it said anything - "
                           "raise --max-tokens, this is not a tool-calling failure")
        return False, "answered from its own head instead of calling a tool"

    name = calls[0]["function"]["name"]
    if name != expected_tool:
        return False, f"called {name}"

    if arg_check is None:
        return True, "called correctly"

    raw = calls[0]["function"].get("arguments") or "{}"
    try:
        args = json.loads(raw) if isinstance(raw, str) else raw
    except json.JSONDecodeError:
        return False, "arguments were not valid JSON"
    if not arg_check(args):
        return False, f"wrong arguments: {args}"
    return True, "called correctly"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--base-url", required=True, help="OpenAI-compatible base, ending in /v1")
    p.add_argument("--model", required=True)
    p.add_argument("--repeats", type=int, default=3, help="per case; models are stochastic")
    p.add_argument("--temperature", type=float, default=0.75,
                   help="production default - test what you ship, not what flatters")
    p.add_argument("--timeout", type=int, default=180)
    # 🔑 REASONING IS THE BIGGEST LATENCY TERM MEASURED HERE, so it gets a
    #    switch - and the switch goes through Ollama's own /api/chat, because
    #    that is the only place it was found to work. See call_native_no_think
    #    for the four ways that do not.
    #
    # ⚠️ The mode recorded in the result is the one MEASURED, not the one asked
    #    for: a model that ignores the flag is written down as having ignored
    #    it. Mixing a real no-think run with a pretend one in a single table is
    #    how a model gets credited with speed it does not have.
    p.add_argument("--no-think", action="store_true",
                   help="turn reasoning off via Ollama's /api/chat (needs --ollama-host)")
    p.add_argument("--max-tokens", type=int, default=1200,
                   help="the shipped config's value. Lower it and reasoning models "
                        "score as though they cannot call tools")
    p.add_argument("--label", default=None, help="hardware note, e.g. 'RTX 4090'")
    p.add_argument("--ollama-host", default=None,
                   help="e.g. http://127.0.0.1:11434 - asks /api/ps how much card "
                        "the model took and how much of it actually fit")
    p.add_argument("--out", default=None,
                   help="defaults to result-<model>.json beside this script, with "
                        "-nothink appended when reasoning was turned off")
    a = p.parse_args()

    if a.no_think and not a.ollama_host:
        print("--no-think needs --ollama-host: no other endpoint here honours it",
              file=sys.stderr)
        return 2

    def do_call(messages):
        if a.no_think:
            return call_native_no_think(a.ollama_host, a.model, messages, a.timeout,
                                        a.temperature, a.max_tokens)
        return call(a.base_url, a.model, messages, a.timeout, a.temperature, a.max_tokens)

    results, latencies, rates, res = [], [], [], {}
    ignored_thinking = 0
    tools_refused = 0

    # ⚠️ WARM IT FIRST, AND THROW THE RESULT AWAY.
    #
    #    Without this the TTFT figure is dominated by the model being read off
    #    disk and into VRAM - 5.9s for an 8B, against 0.8s once resident. That
    #    is a fact about the cache, not about the model, and it would have gone
    #    into the table as though it were the latter.
    #
    #    Cold-start cost is worth knowing, but it belongs in its own column and
    #    only matters once per model load, not once per sentence.
    print("  warming...", end="", flush=True)
    load_start = time.monotonic()
    try:
        do_call([{"role": "user", "content": "hi"}])
        cold_load_s = round(time.monotonic() - load_start, 1)
        print(f" resident after {cold_load_s}s")
    except (urllib.error.URLError, TimeoutError) as e:
        cold_load_s = None
        print(f" failed: {e}")

    try:
        ttft, ttfw, _ = time_to_speech(
            a.ollama_host if a.no_think else a.base_url,
            a.model, a.timeout, a.temperature, a.max_tokens, a.no_think)
    except (urllib.error.URLError, TimeoutError, ValueError) as e:
        print(f"  (no TTFT: {e})")
        ttft = ttfw = None
    print(f"\n{a.model} @ {a.base_url}  (temp {a.temperature}, {a.repeats}x each)\n")

    for cid, prompt, expected, check in CASES:
        passes, notes = 0, []
        for _ in range(a.repeats):
            # The SAME system prompt in both modes. An earlier version appended
            # " /no_think" here on top of the native switch, which did nothing
            # to the thinking and everything to the comparison: the two runs
            # were no longer differing in one variable.
            messages = [{"role": "system", "content": SYSTEM},
                        {"role": "user", "content": prompt}]
            try:
                payload, elapsed = do_call(messages)
            except urllib.error.HTTPError as e:
                # 🔴 400 MEANS THE MODEL WAS NEVER ALLOWED TO TRY.
                #
                #    Ollama rejects a request carrying `tools` for a model whose
                #    template has no tool support - gemma3 being the case here.
                #    Scored as 0/18 that reads as "tried and failed" and would
                #    have gone into a published table as a verdict on the model.
                #    It is a verdict on the runtime's template.
                if e.code == 400:
                    tools_refused += 1
                    notes.append("the runtime refused the request: this model has no "
                                 "tool support here (HTTP 400)")
                else:
                    notes.append(f"request failed: {e}")
                continue
            except (urllib.error.URLError, TimeoutError) as e:
                notes.append(f"request failed: {e}")
                continue
            # One extra turn if it went to look something up first.
            checked_first = False
            calls = (payload.get("choices") or [{}])[0].get("message", {}).get("tool_calls") or []
            if (expected and calls and calls[0]["function"]["name"] in READ_ONLY_TOOLS
                    and calls[0]["function"]["name"] != expected):
                checked_first = True
                messages.append(payload["choices"][0]["message"])
                messages.append({
                    "role": "tool",
                    "tool_call_id": calls[0].get("id", "0"),
                    "content": READ_ONLY_RESULT,
                })
                try:
                    payload2, elapsed2 = do_call(messages)
                    payload, elapsed = payload2, elapsed + elapsed2
                except (urllib.error.URLError, TimeoutError) as e:
                    notes.append(f"second turn failed: {e}")

            ignored_thinking += payload.get("_thought_chars", 0)
            ok, note = evaluate(payload, expected, check)
            if ok and checked_first:
                note += " (checked the device first, which is fair enough)"
            passes += ok
            if not ok:
                notes.append(note)
            latencies.append(elapsed)
            # Asked once, after the first call, because that is the first moment
            # the model is certain to be loaded.
            if a.ollama_host and not res:
                res = residency(a.ollama_host, a.model)
            usage = payload.get("usage") or {}
            out_tokens = usage.get("completion_tokens")
            if out_tokens and elapsed > 0:
                rates.append(out_tokens / elapsed)

        mark = "ok  " if passes == a.repeats else ("FAIL" if passes == 0 else "flaky")
        print(f"  {mark} {cid:<14} {passes}/{a.repeats}"
              + (f"   {notes[0]}" if notes else ""))
        results.append({"case": cid, "passes": passes, "of": a.repeats, "notes": notes[:3]})

    total = sum(r["passes"] for r in results)
    of = sum(r["of"] for r in results)
    summary = {
        "model": a.model,
        "hardware": a.label,
        "base_url": a.base_url,
        "temperature": a.temperature,
        "thinking": (
            ("off (verified)" if not ignored_thinking
             else f"REQUESTED OFF BUT IGNORED - {ignored_thinking} chars of thought")
            if a.no_think else "on (model default)"),
        "cold_load_s": cold_load_s,
        "ttft_s": round(ttft, 2) if ttft else None,
        # The one to quote. See the note on time_to_speech.
        "time_to_first_spoken_word_s": round(ttfw, 2) if ttfw else None,
        "silence_s": round(ttfw - ttft, 2) if (ttft and ttfw) else None,
        "max_tokens": a.max_tokens,
        "tool_score": f"{total}/{of}",
        # None, not 0. A model that was refused has no score, and a table that
        # prints 0% for it is stating something untrue in a tidy column.
        "tool_pct": (None if tools_refused == of
                     else (round(100 * total / of) if of else 0)),
        "tools_supported": tools_refused < of,
        "median_latency_s": round(statistics.median(latencies), 2) if latencies else None,
        "median_tok_s": round(statistics.median(rates), 1) if rates else None,
        **res,
        "cases": results,
    }

    fit = ""
    if res:
        fit = f"   {res['vram_gb']} GB on the card"
        if res["on_gpu_pct"] < 100:
            # Loud, because it silently explains a bad tok/s and would otherwise
            # be read as the model simply being slow.
            fit += f"  \u26a0 ONLY {res['on_gpu_pct']}% FIT - the rest ran on the CPU"
    if ttfw is not None:
        gap = f"  (first token at {ttft:.2f}s, then {ttfw - ttft:.2f}s of thinking)" \
            if ttft is not None and ttfw - ttft > 0.15 else ""
        print(f"  speaks after {ttfw:.2f}s{gap}")
    if not summary["tools_supported"]:
        print("\n  NO TOOL SUPPORT in this runtime - every request was refused (HTTP 400).")
        print("  Not a score. The model was never given the chance to call anything.")
        # Speed is still a fact about the model, and for a MoE it is the fact
        # the whole active-parameter question turns on. Measured without tools,
        # which is the only way this model can be measured at all.
        try:
            # Warm it first: this model never loaded, because every scored
            # call was refused before it could.
            call(a.base_url, a.model, [{"role": "user", "content": "hi"}],
                 a.timeout, a.temperature, 16, with_tools=False)
            # ⚠️ THREE SAMPLES, MEDIAN. One is a measurement of your disk: the
            #    first call after a warm-up can still be paying for the weights
            #    arriving, and it read 10.9 tok/s for a model that does 84.
            samples = []
            for _ in range(3):
                t0 = time.monotonic()
                payload, _ = call(a.base_url, a.model,
                                  [{"role": "user", "content": TTFT_PROMPT}],
                                  a.timeout, a.temperature, a.max_tokens,
                                  with_tools=False)
                toks = (payload.get("usage") or {}).get("completion_tokens")
                el = time.monotonic() - t0
                if toks and el > 0:
                    samples.append(toks / el)
            if samples:
                summary["median_tok_s"] = round(statistics.median(samples), 1)
                summary["tok_s_measured_without_tools"] = True
                spread = f" (of {', '.join(f'{x:.0f}' for x in sorted(samples))})"
                print(f"  Speed, measured without tools: "
                      f"{summary['median_tok_s']} tok/s{spread}")
            res2 = residency(a.ollama_host, a.model) if a.ollama_host else {}
            if res2:
                summary.update(res2)
                print(f"  {res2['vram_gb']} GB on the card")
        except (urllib.error.URLError, TimeoutError) as e:
            print(f"  (could not measure speed either: {e})")
        print()
    print(f"\n  tools {total}/{of} ({summary['tool_pct']}%)"
          f"   median {summary['median_latency_s']}s"
          f"   {summary['median_tok_s']} tok/s{fit}\n")

    suffix = "-nothink" if a.no_think else ""
    out = a.out or f"result-{a.model.replace(':', '-').replace('/', '-')}{suffix}.json"
    with open(out, "w") as f:
        json.dump(summary, f, indent=2)
    print(f"  wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
