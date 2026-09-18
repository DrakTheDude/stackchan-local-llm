#!/usr/bin/env bash
#
# Build the StackChan firmware. Run this from anywhere; it finds the repo itself.
#
# ⚠️ THE ONLY REASON THIS SCRIPT EXISTS is the -DSDKCONFIG_DEFAULTS list below.
#    A plain `idf.py build` silently builds a DIFFERENT BOARD - upstream's
#    default target - and bakes in upstream's cloud OTA URL. It compiles, it
#    flashes, and it boots into something that is not this project. The only
#    outward sign is the binary being a few hundred KB smaller.
#
#    So: always build through this script, and never delete `sdkconfig` and
#    rebuild by hand expecting the same result.
#
# The files, in order, later ones overriding earlier:
#
#   sdkconfig.defaults             upstream's baseline
#   sdkconfig.defaults.stackchan   this board: target, PSRAM, camera, wake word,
#                                  and a deliberately unreachable OTA URL
#   sdkconfig.defaults.local       YOUR server address. Optional and gitignored;
#                                  see docs/quickstart.md. Without it the build
#                                  points at a .invalid hostname that cannot
#                                  resolve - a fail-safe, not a working default.
#
set -euo pipefail

FW="$(cd "$(dirname "${BASH_SOURCE[0]}")/../firmware" && pwd)"
cd "$FW"

# ESP-IDF v6.0.2. Override with IDF_PATH if yours lives elsewhere.
: "${IDF_PATH:=$HOME/esp/esp-idf}"
if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "ESP-IDF not found at $IDF_PATH - set IDF_PATH to your checkout" >&2
    exit 1
fi
# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null

DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.stackchan'
if [[ -f sdkconfig.defaults.local ]]; then
    DEFAULTS="$DEFAULTS;sdkconfig.defaults.local"
else
    echo "note: no sdkconfig.defaults.local - the OTA URL will be the unreachable"
    echo "      placeholder. Fine for a compile check, not for a robot."
fi

# ⚠️ TWO THINGS DO NOT REACH A PLAIN BUILD, and both fail in ways that read as
#    something else entirely:
#
#    1. A NEW Kconfig OPTION. Adding one and rebuilding fails with "CONFIG_FOO
#       was not declared in this scope", which looks like a missing include.
#    2. A NEW SOURCE FILE. Board sources are picked up by a CMake glob, and a
#       glob is evaluated at configure time - so a new .cc compiles into nothing
#       and the build fails at the LINKER, with undefined references to functions
#       you are looking straight at.
#
#    Both are one `reconfigure` away, so just notice and do it.
NEED_RECONFIG=""
[[ main/Kconfig.projbuild -nt sdkconfig ]] && NEED_RECONFIG="Kconfig.projbuild changed"
if [[ -f build/CMakeCache.txt ]]; then
    # Any board source newer than the cache means the glob may be stale.
    if find main/boards -name '*.cc' -newer build/CMakeCache.txt -print -quit | grep -q .; then
        NEED_RECONFIG="${NEED_RECONFIG:+$NEED_RECONFIG; }board sources changed"
    fi
fi
if [[ -n "$NEED_RECONFIG" ]]; then
    echo "note: reconfiguring first ($NEED_RECONFIG)"
    idf.py -DSDKCONFIG_DEFAULTS="$DEFAULTS" reconfigure >/dev/null
fi

idf.py -DSDKCONFIG_DEFAULTS="$DEFAULTS" "${@:-build}"

# The size check is the cheap way to catch the wrong-board build described above.
if [[ -f build/xiaozhi.bin ]]; then
    echo
    echo "built: $(du -h build/xiaozhi.bin | cut -f1)  build/xiaozhi.bin"
    grep -o 'CONFIG_OTA_URL="[^"]*"' sdkconfig || true
fi
