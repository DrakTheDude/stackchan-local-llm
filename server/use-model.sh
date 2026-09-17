#!/usr/bin/env bash
#
# Swap the model the robot thinks with.
#
#   ./use-model.sh qwen3:8b          pull it, point the config at it, restart
#   ./use-model.sh                   show what is configured and what is pulled
#
# Everything here is one command because it is three steps that MUST stay in
# step: Ollama needs the model present, the config needs the tag spelled exactly
# right, and the server only re-reads its config on restart. Doing two of the
# three leaves a robot that answers with an error nobody can hear the reason for.
#
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

CONFIG=data/.config.yaml
[[ -f $CONFIG ]] || { echo "no $CONFIG yet - cp config.example.yaml $CONFIG first" >&2; exit 1; }

current() {
    # The model_name inside the Ollama entry specifically. There are several
    # model_name keys in the file and picking the wrong one is a silent mistake.
    awk '/^  Ollama:/{inblk=1; next} /^  [A-Za-z]/{inblk=0} inblk && /model_name:/{print $2; exit}' "$CONFIG"
}

if [[ $# -eq 0 ]]; then
    echo "configured: $(current)"
    echo
    echo "pulled:"
    docker compose exec -T ollama ollama list 2>/dev/null || echo "  (ollama not running)"
    echo
    echo "usage: $0 <ollama-tag>      e.g. $0 qwen3:8b"
    exit 0
fi

MODEL="$1"

# Pull FIRST. If the tag is wrong this fails here, with the config untouched,
# instead of leaving the robot pointed at something that does not exist.
echo "==> pulling $MODEL"
docker compose up -d ollama
docker compose exec -T ollama ollama pull "$MODEL"

echo "==> pointing the config at $MODEL"
awk -v m="$MODEL" '
    /^  Ollama:/ { inblk=1 }
    inblk && /model_name:/ && !done { sub(/model_name:.*/, "model_name: " m); done=1 }
    /^  [A-Za-z]/ && !/^  Ollama:/ { inblk=0 }
    { print }
' "$CONFIG" > "$CONFIG.new"
mv "$CONFIG.new" "$CONFIG"

# Keep .env in step so the compose file and the config never disagree.
if [[ -f .env ]] && grep -q '^LLM_MODEL=' .env; then
    sed -i "s|^LLM_MODEL=.*|LLM_MODEL=$MODEL|" .env
fi

echo "==> restarting the pipeline"
docker compose restart xiaozhi

echo
echo "now using: $(current)"
echo "say the wake word and ask him something that needs a tool - that is the"
echo "thing smaller models lose first. docs/model-floor.md has the measurements."
