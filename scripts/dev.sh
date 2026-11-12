#!/usr/bin/env bash
# Full dev loop: configure, bump build number, build, test, validate, install.
# Usage: scripts/dev.sh [--no-install] [--no-validate]
set -euo pipefail
source "$(dirname "$0")/common.sh"
INSTALL=1; VALIDATE=1
for a in "$@"; do case $a in --no-install) INSTALL=0;; --no-validate) VALIDATE=0;; esac; done

mkdir -p "$BUILD"
N=$(( $(cat "$BUILD/.build_number" 2>/dev/null || echo 0) + 1 ))
echo "$N" > "$BUILD/.build_number"

echo "== configure (build #$N)"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DJUNKYARD_DEV_BUILD=ON \
      -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null

echo "== build"
cmake --build "$BUILD" 2>&1 | tail -n 25

echo "== tests"
ctest --test-dir "$BUILD" --output-on-failure -j4 2>&1 | tail -n 15

if [ $VALIDATE = 1 ]; then
  echo "== validate"
  "$ROOT/scripts/validate.sh" quick
fi

if [ $INSTALL = 1 ]; then
  echo "== install"
  "$ROOT/scripts/install-dev.sh" || {
    echo "!! Install failed (sandbox?). Re-run: scripts/install-dev.sh  (with the sandbox disabled)"; exit 2; }
fi
echo "== done: build #$N"
