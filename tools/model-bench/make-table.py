#!/usr/bin/env python3
"""Build the results table and splice it into docs/model-floor.md.

Run it after a bench sweep:  python3 tools/model-bench/make-table.py

Generated rather than typed, so the doc cannot drift from the JSON files, and a
re-run updates the page.
"""
import json, pathlib, re, sys

R = pathlib.Path(__file__).resolve().parents[2]
OUT = R / "tools/model-bench"
DOC = R / "docs/model-floor.md"

SHAPE = {
    "granite4:micro": ("dense", "3B"),
    "granite4:tiny-h": ("MoE", "7B / 1B active"),
    "granite4:small-h": ("MoE", "32B / 9B active"),
    "qwen3:4b": ("dense", "4B"),
    "qwen3:8b": ("dense", "8B"),
    "qwen3:14b": ("dense", "14B"),
    "qwen3:32b": ("dense", "32B"),
    "qwen3:30b-a3b": ("MoE", "30B / 3B active"),
    "gpt-oss:20b": ("MoE", "21B / 3.6B active"),
    "deepseek-v2:16b": ("MoE", "16B / 2.4B active"),
    "mistral-nemo:12b": ("dense", "12B"),
    "mistral-small3.2:24b": ("dense", "24B"),
    "granite3.3:8b": ("dense", "8B"),
    "llama3.1:8b": ("dense", "8B"),
    "gemma3:12b": ("dense", "12B"),
}
CARDS = [6, 8, 12, 16, 24]


def card_for(v, fits):
    if v is None:
        return "?"
    if not fits:
        return "more than 24 GB"
    for c in CARDS:
        if v <= c * 0.9:
            return f"{c} GB"
    return "24 GB"


# Which machine's table to build. Defaults to the 4090 results that the
# published page is about; pass a directory name to build another one.
#
# ⚠️ NEVER GLOB ACROSS MACHINES. Two cards in one table, sorted together and
#    presented under one heading, is a lie that reads perfectly.
import sys as _sys

machine = _sys.argv[1] if len(_sys.argv) > 1 else "rtx-4090-24gb-ollama-defaults"
src = OUT / "results" / machine
if not src.is_dir():
    legacy = sorted(OUT.glob("result-*.json"))
    if legacy:
        src = OUT          # pre-split layout
    else:
        print(f"no results in {src}", file=_sys.stderr)
        _sys.exit(1)
print(f"building from {src}")

rows, notes = [], []
for f in sorted(src.glob("result-*.json")):
    d = json.load(open(f))
    if "llama.cpp" in str(d.get("hardware") or ""):
        continue
    if "IGNORED" in str(d.get("thinking") or ""):
        notes.append(f"- `{d['model']}` does not honour `think: false` — it has its own "
                     f"reasoning control, so there is no *thinking off* row for it.")
        continue
    rows.append(d)

rows.sort(key=lambda d: (0 if d.get("tool_pct") is None else -d["tool_pct"],
                         d.get("time_to_first_spoken_word_s") or 99))

lines = ["| model | shape | tools | speaks after | tok/s | VRAM | fits |",
         "|---|---|---|---|---|---|---|"]
for d in rows:
    m = d["model"]
    kind, size = SHAPE.get(m, ("?", "?"))
    label = f"`{m}`"
    if "off (verified)" in str(d.get("thinking")):
        label += "<br>*thinking off*"
    fits = d.get("on_gpu_pct") in (None, 100)
    if d.get("tools_supported") is False:
        tools = "*refused*"
        notes.append(f"- `{m}` has **no tool support** in this runtime — every request came back "
                     f"`HTTP 400`. Its speed is measured without tools; it has no score because it "
                     f"was never allowed to try.")
    else:
        tools = f"**{d['tool_pct']}%**"
    v = d.get("vram_gb")
    vram = f"{v} GB" if v is not None else "?"
    if not fits:
        vram += f"<br>⚠️ {d['on_gpu_pct']}% on GPU"
        notes.append(f"- `{m}` did **not fit**: only {d['on_gpu_pct']}% of it was on the card "
                     f"and the rest ran on system RAM, which is the whole explanation for its "
                     f"single-digit tok/s. That is a fact about a 24 GB card, not about the "
                     f"model — llama.cpp runs the same weights at 37.9 tok/s with a tighter "
                     f"quantisation.")
    # ⚠️ A refused model's warm-up was refused too, so its first-word time
    #    includes the weights arriving from disk. Printing it beside figures
    #    taken warm would invite exactly the comparison it cannot support.
    speaks = ("—" if d.get("tools_supported") is False
              else f"{d.get('time_to_first_spoken_word_s') or '—'} s")
    lines.append(f"| {label} | {kind}, {size} | {tools} | "
                 f"{speaks} | "
                 f"{d.get('median_tok_s') or '—'} | {vram} | {card_for(v, fits)} |")

table = "\n".join(lines)
if notes:
    table += "\n\n" + "\n".join(dict.fromkeys(notes))

doc = DOC.read_text(encoding="utf-8")
new = re.sub(r"<!-- BENCH TABLE START -->.*?<!-- BENCH TABLE END -->",
             f"<!-- BENCH TABLE START -->\n{table}\n<!-- BENCH TABLE END -->",
             doc, flags=re.S)
# "Unchanged" is a normal outcome - a re-run with the same results. Only a
# MISSING marker is an error, and the distinction matters: the first version of
# this check reported "markers not found in the doc" for a no-op splice, which
# is the wrong cause, stated confidently, in a tool whose whole job is to stop
# the page drifting from the measurements.
if "<!-- BENCH TABLE START -->" not in doc:
    print("docs/model-floor.md has no <!-- BENCH TABLE START --> marker", file=sys.stderr)
    sys.exit(1)
DOC.write_text(new, encoding="utf-8")
print(table)
print(f"\nspliced {len(rows)} rows into {DOC.relative_to(R)}")
