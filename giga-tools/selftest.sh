#!/bin/bash
# Giga Sketch: run a UI self-test (libs/ui/GigaSelfTest.cpp) in a throw-away
# config, so it never touches your real settings or your running copy.
#   giga-tools/selftest.sh panels      # one test
#   giga-tools/selftest.sh all         # every test
# Prints only the GIGA_SELFTEST lines; exit code = number of failures.
NAME="${1:-all}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRATCH="$(mktemp -d)"
trap 'rm -rf "$SCRATCH"' EXIT
mkdir -p "$SCRATCH/home" "$SCRATCH/cfg"
HOME="$SCRATCH/home" XDG_CONFIG_HOME="$SCRATCH/cfg" GIGA_SELFTEST="$NAME" \
    GIGA_BLENDER_SOCKET="$SCRATCH/blender.sock" \
    timeout 180 "$ROOT/install/bin/krita" > "$SCRATCH/log" 2>&1
CODE=$?
grep '^GIGA_SELFTEST' "$SCRATCH/log" || { echo "no self-test output (exit $CODE); last log lines:"; tail -20 "$SCRATCH/log"; }
exit $CODE
