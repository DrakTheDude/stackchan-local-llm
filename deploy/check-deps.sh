#!/bin/bash

################################################################################
# StackChan Dependency Checker
################################################################################
# Script: check-deps.sh
# Purpose: Check whether this machine can build and run StackChan, and print the
#          exact command to fix anything that is missing
# Supported: Alpine Linux (apk), Debian/Ubuntu (apt), and any Linux for the
#            checks that are not package-manager specific
# License: MIT
#
# Usage: ./deploy/check-deps.sh              # everything
#        ./deploy/check-deps.sh server       # just what the server needs
#        ./deploy/check-deps.sh firmware     # just what BUILDING firmware needs
#
# This script will:
#   1. Detect the operating system and pick the right install commands
#   2. Check each dependency and report what is missing
#   3. Print the recommended command for anything that needs installing
#   4. Log all output to check-deps.log
#   5. Exit with the number of hard failures, so CI can use it too
#
# 🔴 IT CHECKS. IT DOES NOT INSTALL.
#
#    Every dependency here is one that installing silently would be rude or
#    dangerous: Docker is a daemon and a group membership, the NVIDIA toolkit
#    must match a host driver, ESP-IDF is a 2GB toolchain most people do not
#    need, and the two WSL settings are not packages at all - they live on the
#    Windows side. A script that half-installs a GPU stack leaves behind a
#    machine nobody can reproduce, so this one prints the command and lets you
#    read it first.
#
# ⚠️ The Windows half of this project is checked by deploy/check-deps.ps1. The
#    two WSL settings below are configured over there, and the Hyper-V firewall
#    cannot be seen from in here at all.
#
# Log file: ./check-deps.log
################################################################################

set -uo pipefail          # NOT -e: a failing check is the output, not a crash

# ── Configuration ────────────────────────────────────────────────────────────

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly LOG_FILE="${SCRIPT_DIR}/check-deps.log"
readonly TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
readonly WANT="${1:-all}"

PASSED=0
WARNINGS=0
FAILURES=0

# Set by detect_os(), used to print an install command that actually works here.
PKG_MANAGER="unknown"

# ── Logging Functions ────────────────────────────────────────────────────────

# Initialize log file with header
init_log() {
  {
    echo "================================================================================"
    echo "StackChan Dependency Checker Log"
    echo "================================================================================"
    echo "Started: ${TIMESTAMP}"
    echo "OS: $(uname -s)"
    echo "User: $(whoami)"
    echo "Working Directory: ${REPO_DIR}"
    echo "================================================================================"
    echo ""
  } > "${LOG_FILE}"
}

log_info() {
  local msg="$1"
  echo "[INFO] ${msg}" | tee -a "${LOG_FILE}"
}

log_success() {
  local msg="$1"
  echo "✅ ${msg}" | tee -a "${LOG_FILE}"
  ((PASSED++)) || true
}

# Missing, but the project may still work - a different GPU, a model served
# elsewhere, firmware you are not building. Prints the fix and carries on.
log_warn() {
  local msg="$1"
  local fix="$2"
  {
    echo "⚠️  ${msg}"
    echo "    fix: ${fix}"
  } | tee -a "${LOG_FILE}"
  ((WARNINGS++)) || true
}

# Missing, and nothing will work until it is not.
log_error() {
  local msg="$1"
  local fix="${2:-}"
  {
    echo "❌ ERROR: ${msg}"
    [[ -n "${fix}" ]] && echo "    fix: ${fix}"
  } | tee -a "${LOG_FILE}" >&2
  ((FAILURES++)) || true
}

log_section() {
  local msg="$1"
  {
    echo ""
    echo "────────────────────────────────────────────────────────────────────────────────"
    echo ">>> ${msg}"
    echo "────────────────────────────────────────────────────────────────────────────────"
  } | tee -a "${LOG_FILE}"
}

# ── Helpers ──────────────────────────────────────────────────────────────────

have() { command -v "$1" &>/dev/null; }

# Should this section run? Lets a firmware-only contributor skip the GPU checks.
want() { [[ "${WANT}" == "all" || "${WANT}" == "$1" ]]; }

# The right install line for whatever this machine runs.
pkg_cmd() {
  local pkgs="$1"
  case "${PKG_MANAGER}" in
    apt) echo "sudo apt-get install -y ${pkgs}" ;;
    apk) echo "sudo apk add --no-cache ${pkgs}" ;;
    *)   echo "install with your package manager: ${pkgs}" ;;
  esac
}

# ── OS Detection ─────────────────────────────────────────────────────────────

detect_os() {
  if [[ -f /etc/os-release ]]; then
    . /etc/os-release
    OS_ID="$ID"
  else
    log_warn "Cannot determine OS (no /etc/os-release)" \
             "Checks still run; install commands below will be generic."
    return 0
  fi

  case "$OS_ID" in
    alpine)
      PKG_MANAGER="apk"
      log_info "Detected Alpine Linux - using apk package manager"
      ;;
    debian | ubuntu)
      PKG_MANAGER="apt"
      log_info "Detected Debian/Ubuntu - using apt package manager"
      ;;
    *)
      # Not fatal. Nothing here is installed by this script anyway, and the
      # non-package checks below are the valuable ones.
      log_info "OS is ${OS_ID} - install commands below will be generic"
      ;;
  esac
}

# ── The Machine ──────────────────────────────────────────────────────────────

check_machine() {
  log_section "The machine"

  if grep -qi microsoft /proc/version 2>/dev/null; then
    log_success "Running under WSL2"

    # 🔴 THE SETTING THAT STOPS THE ROBOT CONNECTING, CHECKED FIRST BECAUSE IT
    #    IS THE ONE THAT WASTES THE MOST TIME.
    #
    #    With WSL's default NAT, a robot on Wi-Fi cannot reach a server bound
    #    inside WSL without hand-written netsh portproxy rules. The service is
    #    up, the port is bound, and the packet dies a layer below where anyone
    #    is looking - so it presents as a broken client, not a network setting.
    local host_ip
    host_ip="$(ip route get 1.1.1.1 2>/dev/null | awk '{print $7; exit}')"
    if [[ "${host_ip:-}" == 172.* ]]; then
      log_warn "WSL looks like it is on NAT (address ${host_ip}) - the robot will not reach the server" \
               "On Windows: add 'networkingMode=mirrored' under [wsl2] in %USERPROFILE%\\.wslconfig, then run 'wsl --shutdown'"
    else
      log_success "WSL networking looks mirrored (address ${host_ip:-unknown})"
    fi

    log_info "Reminder: the Hyper-V firewall is SEPARATE from Windows Firewall and"
    log_info "          ON by default since WSL 2.0.9. Reachable from Windows is not"
    log_info "          reachable from the LAN. Run deploy/check-deps.ps1 to check it."
  else
    log_success "Running on native Linux"
  fi

  # Cross-boundary file access under WSL is slow and permission-weird, and it is
  # the actual source of most Windows/WSL pain on this project.
  case "${REPO_DIR}" in
    /mnt/*)
      log_warn "This checkout is on a Windows drive (${REPO_DIR})" \
               "Move it into the Linux filesystem, e.g. ~/stackchan-local-llm. Explorer can still reach it at \\\\wsl\$\\Ubuntu\\home\\..." ;;
    *)
      log_success "Checkout is in the Linux filesystem" ;;
  esac

  local free_gb
  free_gb="$(df -BG --output=avail "${REPO_DIR}" 2>/dev/null | tail -1 | tr -dc '0-9')"
  if [[ -n "${free_gb:-}" ]] && (( free_gb < 30 )); then
    log_warn "${free_gb}G free on this filesystem" \
             "Models and container images want more; 30G is a comfortable floor."
  else
    log_success "${free_gb:-?}G free"
  fi
}

# ── Core Tools ───────────────────────────────────────────────────────────────

check_core() {
  log_section "Core tools"

  if have python3; then
    log_success "python3 $(python3 -V 2>&1 | awk '{print $2}')"
  else
    log_error "python3 is missing - the patch kit and the model bench are Python" \
              "$(pkg_cmd 'python3 python3-pip')"
  fi

  have git  && log_success "git"  || log_error "git is missing"  "$(pkg_cmd git)"
  have curl && log_success "curl" || log_error "curl is missing" "$(pkg_cmd curl)"
}

# ── The Server ───────────────────────────────────────────────────────────────

check_docker() {
  log_section "Docker"

  if ! have docker; then
    log_error "Docker is not installed - the server runs in containers" \
              "https://docs.docker.com/engine/install/ (on Windows: Docker Desktop with WSL integration)"
    return 0
  fi

  if ! docker info &>/dev/null; then
    log_error "Docker is installed but not responding" \
              "Start Docker Desktop, or: sudo service docker start"
    return 0
  fi
  log_success "Docker responds"

  if docker compose version &>/dev/null; then
    log_success "Docker Compose v2 present"
  else
    log_error "Docker Compose v2 is missing" \
              "This project uses 'docker compose' (a plugin), not the older 'docker-compose'."
  fi
}

check_gpu() {
  log_section "The GPU"

  if ! have nvidia-smi; then
    log_info "No nvidia-smi. That is fine if you serve models elsewhere or on"
    log_info "non-NVIDIA hardware - see docs/your-llm.md for AMD, Apple and CPU."
    return 0
  fi

  local name vram
  name="$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1)"
  vram="$(nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits 2>/dev/null | head -1)"
  if [[ -n "${vram:-}" ]] && (( vram < 8000 )); then
    log_warn "${name}, ${vram}MB - under 8GB" \
             "There is a row for you in docs/model-floor.md, but it is tight."
  else
    log_success "${name}, ${vram:-?}MB"
  fi

  # 🔴 THE HOST SEEING THE GPU SAYS NOTHING ABOUT A CONTAINER SEEING IT, and the
  #    container is where the model actually runs. This check pulls a small
  #    image the first time; that is the cost of testing the real thing.
  if have docker && docker info &>/dev/null; then
    if docker run --rm --gpus all nvidia/cuda:12.4.0-base-ubuntu22.04 nvidia-smi &>/dev/null; then
      log_success "Containers can see the GPU"
    else
      log_warn "Containers cannot see the GPU" \
               "Install the NVIDIA Container Toolkit: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html"
    fi
  fi
}

check_ports() {
  log_section "Ports"

  if ! have ss; then
    log_info "No 'ss' available - skipping the port check. $(pkg_cmd iproute2)"
    return 0
  fi

  local p
  for p in 8000 8003; do
    if ss -ltn 2>/dev/null | grep -q ":${p} "; then
      log_info "Port ${p} is in use - the stack is probably already running."
    else
      log_success "Port ${p} free"
    fi
  done
}

# ── The Firmware Toolchain ───────────────────────────────────────────────────

check_firmware() {
  log_section "Firmware toolchain (only needed to BUILD firmware)"

  local idf="${IDF_PATH:-${HOME}/esp/esp-idf}"
  if [[ ! -f "${idf}/export.sh" ]]; then
    log_info "No ESP-IDF at ${idf}."
    log_info "Only needed if you build firmware - prebuilt binaries flash from a"
    log_info "browser with no toolchain at all. To build:"
    log_info "  git clone -b v6.0.2 --recursive https://github.com/espressif/esp-idf ~/esp/esp-idf"
    log_info "  ~/esp/esp-idf/install.sh esp32s3"
    return 0
  fi

  local ver
  ver="$(cat "${idf}/version.txt" 2>/dev/null || git -C "${idf}" describe --tags 2>/dev/null || echo unknown)"
  case "${ver}" in
    v6.*) log_success "ESP-IDF ${ver} at ${idf}" ;;
    *)    log_warn "ESP-IDF ${ver} at ${idf}" \
                   "This project is built against v6.0.2. Others may work; nobody has checked." ;;
  esac
}

# ── Main Execution ────────────────────────────────────────────────────────────

main() {
  init_log
  log_info "Checking whether this machine can build and run StackChan"
  log_info "Log file: ${LOG_FILE}"

  detect_os

  check_machine
  check_core
  if want server; then
    check_docker
    check_gpu
    check_ports
  fi
  if want firmware; then
    check_firmware
  fi

  log_section "Summary"
  log_info "${PASSED} passed, ${WARNINGS} warnings, ${FAILURES} failures"

  if (( FAILURES > 0 )); then
    {
      echo ""
      echo "❌ Not ready. The failures above are things that will not work, not"
      echo "   preferences. Each one printed the command that fixes it."
      echo ""
    } | tee -a "${LOG_FILE}"
  elif (( WARNINGS > 0 )); then
    {
      echo ""
      echo "⚠️  Probably fine - read the warnings. Most of them are the traps that"
      echo "   cost this project days, not style points."
      echo ""
      echo "Next steps:"
      echo "  1. cd ${REPO_DIR}"
      echo "  2. cp server/config.example.yaml ~/xiaozhi-data/.config.yaml   # and edit it"
      echo "  3. docker compose -f server/docker-compose.yml up -d"
      echo ""
    } | tee -a "${LOG_FILE}"
  else
    {
      echo ""
      echo "✅ Ready."
      echo ""
      echo "Next steps:"
      echo "  1. cd ${REPO_DIR}"
      echo "  2. cp server/config.example.yaml ~/xiaozhi-data/.config.yaml   # and edit it"
      echo "  3. docker compose -f server/docker-compose.yml up -d"
      echo ""
      echo "Then flash the robot from a Chromium browser - see docs/quickstart.md."
      echo ""
    } | tee -a "${LOG_FILE}"
  fi

  exit "${FAILURES}"
}

main "$@"
