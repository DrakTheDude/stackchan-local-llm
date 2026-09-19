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

# Where Ollama is, from THIS shell rather than from inside a container.
#
# 🔑 The config's base_url is written for the server process. If Ollama is the
#    bundled container it says `http://ollama:11434`, a name that only resolves
#    on the compose network; if it is native it says `host.docker.internal`,
#    which only means anything inside a container. Both are 127.0.0.1 from here,
#    and anything else - a box down the hall - is already a real address.
ollama_url() {
    local url
    url=$(awk '/^  Ollama:/{inblk=1; next} /^  [A-Za-z]/{inblk=0} inblk && /base_url:/{print $2; exit}' "$CONFIG")
    url="${url%/v1}"
    case "$url" in
        *//ollama:*|*//stackchan-ollama:*|*//host.docker.internal:*|"")
            echo "http://127.0.0.1:11434" ;;
        *) echo "$url" ;;
    esac
}

current() {
    # The model_name inside the Ollama entry specifically. There are several
    # model_name keys in the file and picking the wrong one is a silent mistake.
    awk '/^  Ollama:/{inblk=1; next} /^  [A-Za-z]/{inblk=0} inblk && /model_name:/{print $2; exit}' "$CONFIG"
}

OLLAMA=$(ollama_url)

if [[ $# -eq 0 ]]; then
    echo "configured: $(current)"
    echo "ollama at : $OLLAMA"
    echo
    echo "pulled:"
    curl -sf "$OLLAMA/api/tags" 2>/dev/null |
        python3 -c "
import json, sys
for m in json.load(sys.stdin).get('models', []):
    print(f\"  {m['name']:24} {m.get('size',0)/1e9:5.1f} GB\")
" || echo "  (no Ollama reachable at $OLLAMA - start it, or fix base_url in $CONFIG)"
    echo
    echo "usage: $0 <ollama-tag>      e.g. $0 qwen3:8b"
    exit 0
fi

MODEL="$1"

# Pull FIRST. If the tag is wrong this fails here, with the config untouched,
# instead of leaving the robot pointed at something that does not exist.
echo "==> pulling $MODEL"
# Start the bundled container if this install has one; harmless otherwise, and
# a native Ollama is expected to be running already.
docker compose up -d ollama >/dev/null 2>&1 || true

if ! curl -sf "$OLLAMA/api/tags" >/dev/null 2>&1; then
    echo "No Ollama at $OLLAMA." >&2
    echo "  - bundled (NVIDIA): COMPOSE_PROFILES=gpu-nvidia in .env" >&2
    echo "  - native (Mac/AMD): start Ollama, and set base_url in $CONFIG to" >&2
    echo "                      http://host.docker.internal:11434/v1" >&2
    echo "  See docs/your-llm.md." >&2
    exit 1
fi

# Streams the same progress the CLI shows, without needing the CLI.
curl -s "$OLLAMA/api/pull" -d "{\"model\": \"$MODEL\"}" |
    python3 -c "
import json, sys
last = ''
for line in sys.stdin:
    try:
        d = json.loads(line)
    except ValueError:
        continue
    if d.get('error'):
        print(f\"  ERROR: {d['error']}\"); sys.exit(1)
    s = d.get('status', '')
    if s != last and 'pulling' not in s:
        print(f'  {s}'); last = s
" || exit 1

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
