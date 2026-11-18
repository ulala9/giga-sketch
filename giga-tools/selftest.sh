#!/bin/bash
# Giga Sketch: run a UI self-test (libs/ui/GigaSelfTest.cpp) in a throw-away
# config, so it never touches your real settings or your running copy.
#   giga-tools/selftest.sh panels      # one test
#   giga-tools/selftest.sh all         # every test
# Prints only the GIGA_SELFTEST lines; exit code = number of failures.
NAME="${1:-all}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRATCH="$(mktemp -d)"
# The test ends with _Exit (no cleanup), so remove the swap files it leaves.
SWAPDIR=/var/tmp
ls "$SWAPDIR"/KRITA_SWAP_FILE_* 2>/dev/null | sort > "$SCRATCH.swap-before"
cleanup() {
    ls "$SWAPDIR"/KRITA_SWAP_FILE_* 2>/dev/null | sort | comm -13 "$SCRATCH.swap-before" - | xargs -r rm -f
    rm -rf "$SCRATCH" "$SCRATCH.swap-before"
}
trap cleanup EXIT
mkdir -p "$SCRATCH/home" "$SCRATCH/cfg"
HOME="$SCRATCH/home" XDG_CONFIG_HOME="$SCRATCH/cfg" GIGA_SELFTEST="$NAME" \
    GIGA_BLENDER_SOCKET="$SCRATCH/blender.sock" \
    timeout 180 "$ROOT/install/bin/krita" > "$SCRATCH/log" 2>&1
CODE=$?
grep '^GIGA_SELFTEST' "$SCRATCH/log" || { echo "no self-test output (exit $CODE); last log lines:"; tail -20 "$SCRATCH/log"; }
exit $CODE
