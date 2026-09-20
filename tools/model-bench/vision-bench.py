#!/usr/bin/env python3
"""Measure vision models on the pictures the robot actually takes.

    ./vision-bench.py --frames ~/xiaozhi-data/frames --label "RTX 4090 24GB"
    OLLAMA=http://box:11434 ./vision-bench.py --frames ./frames

The text bench next door answers "can this model call a tool, and how long
before he speaks". This one answers the other half: what does it cost to look at
a photograph, and does the model say something true about it.

🔴 REAL FRAMES, NOT A TEST CARD. The first vision numbers in docs/vision.md came
   from a synthetic image - a red square and a yellow circle - and said so,
   because the camera did not work yet. A GC0308 frame is a different problem:
   noisy, low contrast, tone-mapped on the device before it is sent. A model
   that names shapes on a clean canvas may still have nothing useful to say
   about a dim room, and that is the question worth answering now that there are
   real frames to ask it with.

🔴 EVERY REQUEST GETS A DIFFERENT PICTURE. Three identical requests measure
   Ollama's prompt cache rather than the model: that mistake reported 0.1 s per
   photo where the honest figure was 0.35 s. Camera frames are never identical,
   so neither is this benchmark.

🔴 THE DESCRIPTIONS ARE OF SOMEBODY'S ROOM. They are printed for you to judge
   and written to a separate file, which is gitignored - the published result
   carries timings and VRAM only. A benchmark whose output is a description of
   where its author lives is not a benchmark you can share.
"""
import argparse
import base64
import json
import os
import re
import statistics
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_PROMPT = "What do you see? One sentence."


def redact_host(url):
    """Same rule as bench.py: keep the shape, drop whose machine it was."""
    return re.sub(r"//[^/:]+", "//<your-llm-host>", url)


def post(url, payload, timeout):
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def residency(ollama, model):
    """What the model is using on the card, and whether all of it fit.

    The split matters more than the size: Ollama runs a model that does not fit
    by keeping the rest in system RAM, without warning, and it simply gets
    slower. On an 8 GB card that is the difference this whole page is about.
    """
    try:
        with urllib.request.urlopen(ollama.rstrip("/") + "/api/ps", timeout=10) as r:
            payload = json.load(r)
    except (urllib.error.URLError, TimeoutError, ValueError, OSError):
        return {}
    for m in payload.get("models", []):
        if model not in (m.get("name"), m.get("model")):
            continue
        total = m.get("size") or 0
        gpu = m.get("size_vram") or 0
        return {
            "resident_gb": round(total / 1e9, 1),
            "on_gpu_gb": round(gpu / 1e9, 1),
            "fully_on_gpu": bool(total and gpu >= total * 0.99),
        }
    return {}


def unload(ollama, model):
    """Give the card back, so the next model is measured on an empty one."""
    try:
        post(ollama.rstrip("/") + "/api/generate",
             {"model": model, "keep_alive": 0}, 30)
    except Exception:  # noqa: BLE001
        pass


def describe(ollama, model, image_b64, prompt, timeout):
    """One photo in, one sentence out. Returns (seconds, text)."""
    started = time.monotonic()
    payload = post(ollama.rstrip("/") + "/api/generate", {
        "model": model,
        "prompt": prompt,
        "images": [image_b64],
        "stream": False,
    }, timeout)
    return time.monotonic() - started, (payload.get("response") or "").strip()


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--frames", required=True,
                   help="directory of real camera frames (jpg/png)")
    p.add_argument("--models", default="",
                   help="comma-separated; default is every vision model the host has")
    p.add_argument("--ollama", default=os.environ.get("OLLAMA", "http://127.0.0.1:11434"))
    p.add_argument("--label", default="", help="hardware note, e.g. 'RTX 4090 24GB'")
    p.add_argument("--prompt", default=DEFAULT_PROMPT)
    p.add_argument("--timeout", type=int, default=180)
    p.add_argument("--warm", type=int, default=1, help="frames used to warm up, not measured")
    a = p.parse_args()

    frames = sorted([f for f in Path(a.frames).iterdir()
                     if f.suffix.lower() in (".jpg", ".jpeg", ".png")])
    if len(frames) < a.warm + 2:
        print(f"need at least {a.warm + 2} frames in {a.frames}, found {len(frames)}",
              file=sys.stderr)
        return 1
    encoded = [base64.b64encode(f.read_bytes()).decode() for f in frames]

    models = [m.strip() for m in a.models.split(",") if m.strip()]
    if not models:
        try:
            with urllib.request.urlopen(a.ollama.rstrip("/") + "/api/tags", timeout=10) as r:
                tags = json.load(r)
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            print(f"no Ollama at {a.ollama}: {e}", file=sys.stderr)
            return 1
        # 🔑 "Which models can see" is asked of the model's own family rather
        #    than of a list of names kept here - a list of names in the thing
        #    that watches the names is a list that goes stale. Ollama reports a
        #    family per model; the vision ones say so.
        for m in tags.get("models", []):
            fam = ((m.get("details") or {}).get("family") or "").lower()
            families = [f.lower() for f in ((m.get("details") or {}).get("families") or [])]
            if "clip" in families or "vl" in fam or "vision" in fam or "mllama" in fam:
                models.append(m["name"])
        if not models:
            print("no vision-capable models found - name them with --models",
                  file=sys.stderr)
            return 1

    label = a.label or "unlabelled"
    results_dir = Path(__file__).parent / "results" / re.sub(
        r"[^a-z0-9-]", "", label.lower().replace(" ", "-"))
    results_dir.mkdir(parents=True, exist_ok=True)
    # 🔴 Descriptions go here, NOT into the published result. See the header.
    seen_path = results_dir / "vision-descriptions.local.txt"
    seen = open(seen_path, "a", encoding="utf-8")

    print(f"{len(frames)} real frames, {len(models)} model(s), on {redact_host(a.ollama)}")
    print(f"descriptions -> {seen_path} (gitignored)\n")

    for model in models:
        unload(a.ollama, model)
        print(f"--- {model}")
        try:
            cold, first = describe(a.ollama, model, encoded[0], a.prompt, a.timeout)
        except Exception as e:  # noqa: BLE001
            print(f"    FAILED: {str(e)[:80]}\n")
            continue
        res = residency(a.ollama, model)

        times, texts = [], []
        # Warm frames are consumed first and not counted; everything after is
        # measured, and each is a DIFFERENT picture.
        for idx in range(a.warm, len(encoded)):
            try:
                secs, text = describe(a.ollama, model, encoded[idx], a.prompt, a.timeout)
            except Exception as e:  # noqa: BLE001
                print(f"    frame {frames[idx].name}: FAILED {str(e)[:60]}")
                continue
            times.append(secs)
            texts.append((frames[idx].name, text))

        if not times:
            print("    no frame succeeded\n")
            continue

        median = statistics.median(times)
        print(f"    cold first look {cold:.2f}s | per photo warm: median {median:.2f}s "
              f"(min {min(times):.2f} max {max(times):.2f}, n={len(times)})")
        if res:
            fit = "all on GPU" if res.get("fully_on_gpu") else "SPILLED TO CPU"
            print(f"    resident {res.get('resident_gb')} GB, {res.get('on_gpu_gb')} GB on card - {fit}")
        print(f"    first two: {texts[0][1][:90]!r}")
        if len(texts) > 1:
            print(f"               {texts[1][1][:90]!r}")
        print()

        seen.write(f"\n===== {model} @ {label} =====\n")
        for name, text in texts:
            seen.write(f"{name}: {text}\n")
        seen.flush()

        out = {
            "model": model,
            "hardware": label,
            "base_url": redact_host(a.ollama),
            "prompt": a.prompt,
            "frames_measured": len(times),
            "real_camera_frames": True,
            "cold_first_look_s": round(cold, 2),
            "per_photo_warm_s": round(median, 2),
            "per_photo_min_s": round(min(times), 2),
            "per_photo_max_s": round(max(times), 2),
            **res,
        }
        slug = model.replace(":", "-").replace("/", "-")
        (results_dir / f"vision-{slug}.json").write_text(
            json.dumps(out, indent=2) + "\n", encoding="utf-8")
        unload(a.ollama, model)

    seen.close()
    print(f"results -> {results_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
