#!/usr/bin/env bash
#
# Build a variant of a reasoning model with the reasoning switched off.
#
#   ./no-think.sh qwen3:8b        creates qwen3:8b-nothink
#
# 🔴 WHY THIS EXISTS, AND WHY IT IS NOT A PROMPT.
#
#    Qwen3 and friends reason before answering, and it is the single largest
#    latency term in this stack: measured on an 8 GB card, qwen3:8b sits silent
#    for 4.3 seconds with reasoning on and 0.08 s with it off, for exactly the
#    same tool score. On a screen that is a spinner. Out loud it is a robot that
#    looks like it did not hear you.
#
#    Every documented way of turning it off fails here. Measured, same question,
#    same budget:
#
#      nothing (baseline)                      2.10s   916 chars of thought
#      "/no_think" in the system prompt        3.03s  1631 chars  <- MORE
#      "/no_think" in the user message         1.55s   701 chars
#      think:false to the OpenAI endpoint      3.07s   861 chars  <- ignored
#      think:false to Ollama's own /api/chat   0.33s     0 chars  <- works
#
#    Only the last one works, and the server speaks the OpenAI dialect, so it
#    cannot reach it. The reason is in the model's chat template: the no-think
#    branch is gated on $.Think, a variable only the native API sets.
#
#    A template is just text. This copies the model's own template with that
#    branch pinned, so the variant never reasons whoever is calling it and
#    whatever dialect they speak. No patch to the server, nothing to keep in
#    step with upstream.
#
# ⚠️ VERIFY TOOL CALLING AFTER BUILDING ONE. Editing a chat template is exactly
#    how tool calls quietly stop working. The stock qwen3:8b and the pinned
#    variant both score 21/21 here - that was checked, not assumed:
#
#      python3 ../tools/model-bench/bench.py \
#          --base-url http://127.0.0.1:11434/v1 --model qwen3:8b-nothink
#
set -euo pipefail

MODEL="${1:?usage: $0 <ollama-tag>   e.g. $0 qwen3:8b}"
OUT="${MODEL%%:*}:${MODEL##*:}-nothink"
[[ "$MODEL" == *:* ]] || OUT="$MODEL-nothink"

ollama_cmd() {
    if docker compose ps --services 2>/dev/null | grep -qx ollama; then
        docker compose exec -T ollama ollama "$@"
    else
        ollama "$@"
    fi
}

echo "==> reading $MODEL's template"
tmpl=$(ollama_cmd show "$MODEL" --template)

if ! grep -q 'IsThinkSet' <<<"$tmpl"; then
    echo "$MODEL does not reason - its template has no thinking branch." >&2
    echo "Nothing to switch off, and nothing to gain. Use it as it is." >&2
    exit 1
fi

echo "==> pinning the no-think branch"
pinned=$(sed -e 's/\$\.IsThinkSet/true/g' -e 's/\$\.Think/false/g' <<<"$tmpl")

work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
{
    echo "FROM $MODEL"
    echo 'TEMPLATE """'
    printf '%s\n' "$pinned"
    echo '"""'
} > "$work/Modelfile"

echo "==> building $OUT"
if docker compose ps --services 2>/dev/null | grep -qx ollama; then
    docker cp "$work/Modelfile" "$(docker compose ps -q ollama)":/tmp/Modelfile >/dev/null
    docker compose exec -T ollama ollama create "$OUT" -f /tmp/Modelfile
else
    ollama create "$OUT" -f "$work/Modelfile"
fi

echo
echo "built $OUT"
echo
echo "Now check it still calls tools - a hand-edited template is how that breaks:"
echo "  python3 ../tools/model-bench/bench.py \\"
echo "      --base-url http://127.0.0.1:11434/v1 --model $OUT --repeats 3"
echo
echo "Then point the robot at it:"
echo "  ./use-model.sh $OUT"
