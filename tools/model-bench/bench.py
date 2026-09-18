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
]


def call(base_url, model, prompt, timeout, temperature):
    body = json.dumps(
        {
            "model": model,
            "messages": [
                {"role": "system", "content": SYSTEM},
                {"role": "user", "content": prompt},
            ],
            "tools": TOOLS,
            "temperature": temperature,
            "max_tokens": 300,
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


def evaluate(payload, expected_tool, arg_check):
    """Returns (ok, what_happened)."""
    try:
        msg = payload["choices"][0]["message"]
    except (KeyError, IndexError):
        return False, "no message in response"

    calls = msg.get("tool_calls") or []
    if expected_tool is None:
        if calls:
            return False, f"called {calls[0]['function']['name']} when it should have answered"
        return True, "answered without calling a tool"

    if not calls:
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
    p.add_argument("--label", default=None, help="hardware note, e.g. 'RTX 4090'")
    p.add_argument("--out", default=None)
    a = p.parse_args()

    results, latencies, rates = [], [], []
    print(f"\n{a.model} @ {a.base_url}  (temp {a.temperature}, {a.repeats}x each)\n")

    for cid, prompt, expected, check in CASES:
        passes, notes = 0, []
        for _ in range(a.repeats):
            try:
                payload, elapsed = call(a.base_url, a.model, prompt, a.timeout, a.temperature)
            except (urllib.error.URLError, TimeoutError) as e:
                notes.append(f"request failed: {e}")
                continue
            ok, note = evaluate(payload, expected, check)
            passes += ok
            if not ok:
                notes.append(note)
            latencies.append(elapsed)
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
        "tool_score": f"{total}/{of}",
        "tool_pct": round(100 * total / of) if of else 0,
        "median_latency_s": round(statistics.median(latencies), 2) if latencies else None,
        "median_tok_s": round(statistics.median(rates), 1) if rates else None,
        "cases": results,
    }

    print(f"\n  tools {total}/{of} ({summary['tool_pct']}%)"
          f"   median {summary['median_latency_s']}s"
          f"   {summary['median_tok_s']} tok/s\n")

    out = a.out or f"result-{a.model.replace(':', '-').replace('/', '-')}.json"
    with open(out, "w") as f:
        json.dump(summary, f, indent=2)
    print(f"  wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
