#!/usr/bin/env bash
# Measure every model Ollama has, and a second pass without reasoning for any
# model that actually reasons.
#
#   ./sweep.sh                            everything, against localhost
#   ./sweep.sh "RTX 5060 8GB"             label the results with the hardware
#   OLLAMA=http://box:11434 ./sweep.sh    somewhere else
#
# 🔑 ONE MODEL RESIDENT AT A TIME. Ollama keeps the last model loaded while the
#    next arrives, and the second then measures as partly-on-CPU through no
#    fault of its own. Each is unloaded before the next is touched, so every
#    figure is what that model needs on an empty card - which is the number a
#    reader compares against their own.
#
# 🔑 THE SECOND PASS IS DECIDED BY EVIDENCE, not by a list of model names. If
#    the first run measured a gap between the first token and the first spoken
#    word, the model reasons and the comparison is worth having.
set -uo pipefail
cd "$(dirname "$0")"

OLLAMA="${OLLAMA:-http://127.0.0.1:11434}"
LABEL="${1:-$(hostname)}"

if ! curl -sf "$OLLAMA/api/tags" >/dev/null; then
    echo "No Ollama at $OLLAMA - start it, or set OLLAMA=..." >&2
    exit 1
fi

unload() {   # give the card back; ollama frees a model on keep_alive 0
    curl -s "$OLLAMA/api/generate" \
        -d "{\"model\": \"$1\", \"keep_alive\": 0}" >/dev/null 2>&1
}

mapfile -t MODELS < <(curl -s "$OLLAMA/api/tags" |
    python3 -c "import json,sys; [print(m['name']) for m in json.load(sys.stdin)['models']]" |
    sort)
echo "${#MODELS[@]} models on $OLLAMA, labelled '$LABEL'"

for m in "${MODELS[@]}"; do
    slug=$(echo "$m" | tr ':/' '--')
    echo
    echo "==================== $m"
    python3 bench.py --base-url "$OLLAMA/v1" --ollama-host "$OLLAMA" \
        --model "$m" --label "$LABEL" --repeats 3 --timeout 300 \
        --out "result-$slug.json"

    silence=$(python3 -c "
import json
try:
    print(json.load(open('result-$slug.json')).get('silence_s') or 0)
except Exception:
    print(0)
")
    if python3 -c "import sys; sys.exit(0 if float('$silence') > 0.15 else 1)"; then
        echo "  -- it thinks for ${silence}s before speaking; measuring it without"
        python3 bench.py --base-url "$OLLAMA/v1" --ollama-host "$OLLAMA" \
            --model "$m" --label "$LABEL" --repeats 3 --timeout 300 --no-think \
            --out "result-$slug-nothink.json"
    fi

    unload "$m"
    sleep 2
done

echo
echo "=== building the table ==="
python3 make-table.py
