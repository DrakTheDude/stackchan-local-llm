#!/usr/bin/env bash
# Measure every model Ollama has, and a second pass without reasoning for any
# model that actually reasons.
#
#   ./sweep.sh "RTX 5060 8GB"             ALWAYS pass the card - see below
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
# 🔴 NEVER DEFAULT THIS TO $(hostname). The label is written verbatim into every
#    result file as "hardware" AND used as the results directory name, and it is
#    the one field here that is not redacted - redact_host() covers the endpoint,
#    nothing covers this. These files are meant to be sent to a public issue, so
#    a hostname default publishes the contributor's machine name on the one path
#    nobody chooses: running the bare command shown at the top of this file.
LABEL="${1:-}"
if [ -z "$LABEL" ]; then
    LABEL="unlabelled"
    echo "No label given - results will be filed under 'unlabelled'." >&2
    echo "Re-run with the card, e.g. ./sweep.sh \"RTX 4090 24GB\", or the numbers" >&2
    echo "cannot be compared against anybody else's." >&2
fi

# 🔴 ONE DIRECTORY PER MACHINE. Results used to be named result-<model>.json
#    with the hardware recorded only inside, so a second machine overwrote the
#    first and the table silently mixed two cards. A 3B model "measuring"
#    2.5 GB where it had measured 5.0 GB an hour before is both a real
#    difference between cards AND what a clobbered file looks like - which is
#    the problem: nothing about it looks wrong.
RESULTS="results/$(echo "$LABEL" | tr 'A-Z ' 'a-z-' | tr -cd 'a-z0-9-')"
mkdir -p "$RESULTS"
echo "results -> $RESULTS"

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
        --out "$RESULTS/result-$slug.json"

    silence=$(python3 -c "
import json
try:
    print(json.load(open('$RESULTS/result-$slug.json')).get('silence_s') or 0)
except Exception:
    print(0)
")
    if python3 -c "import sys; sys.exit(0 if float('$silence') > 0.15 else 1)"; then
        echo "  -- it thinks for ${silence}s before speaking; measuring it without"
        python3 bench.py --base-url "$OLLAMA/v1" --ollama-host "$OLLAMA" \
            --model "$m" --label "$LABEL" --repeats 3 --timeout 300 --no-think \
            --out "$RESULTS/result-$slug-nothink.json"
    fi

    unload "$m"
    sleep 2
done

echo
echo "=== building the table ==="
python3 make-table.py
