#!/usr/bin/env bash
# Universal (arm64 + x86_64) Release build + tests on both architectures + pluginval on both slices.
# Output: build-release/ (never touches build/). Usage: scripts/build-release-mac.sh [--no-validate]
# Env: JUNKYARD_DEPS_DIR=<dir>  FetchContent base dir (CI cache). Otherwise reuses build/_deps sources when present.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; BR="$ROOT/build-release"
VALIDATE=1; for a in "$@"; do [ "$a" = "--no-validate" ] && VALIDATE=0; done
DEPS=()
if [ -n "${JUNKYARD_DEPS_DIR:-}" ]; then
  DEPS+=("-DFETCHCONTENT_BASE_DIR=$JUNKYARD_DEPS_DIR")
else
  # Reuse the dev build's downloaded sources when present (no network needed, sandbox friendly).
  [ -d "$ROOT/build/_deps/juce-src" ]   && DEPS+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$ROOT/build/_deps/juce-src")
  [ -d "$ROOT/build/_deps/catch2-src" ] && DEPS+=("-DFETCHCONTENT_SOURCE_DIR_CATCH2=$ROOT/build/_deps/catch2-src")
fi
VER="$(sed -n 's/^set(JUNKYARD_VERSION \(.*\))$/\1/p' "$ROOT/cmake/VersionNumber.cmake")"

echo "== configure (universal Release, v$VER)"
cmake -S "$ROOT" -B "$BR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUNKYARD_DEV_BUILD=OFF \
      "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" ${DEPS[@]+"${DEPS[@]}"} >/dev/null
echo "== build"
cmake --build "$BR" 2>&1 | tail -n 5

ART="$BR/Junkyard_artefacts/Release"
VST3_BIN="$ART/VST3/Junkyard.vst3/Contents/MacOS/Junkyard"
AU_BIN="$ART/AU/Junkyard.component/Contents/MacOS/Junkyard"
echo "== slices"
for f in "$VST3_BIN" "$AU_BIN"; do
  A="$(lipo -archs "$f")"; echo "$(basename "$(dirname "$(dirname "$(dirname "$f")")")"): $A"
  case "$A" in *x86_64*arm64*|*arm64*x86_64*) ;; *) echo "!! missing a slice in $f"; exit 1;; esac
done

HAVE_ROSETTA=0
if arch -x86_64 /usr/bin/true 2>/dev/null; then HAVE_ROSETTA=1
else echo "!! WARNING: Rosetta is not available: Intel tests and Intel pluginval are SKIPPED"; fi

echo "== tests (arm64)"
ctest --test-dir "$BR" --output-on-failure -j4 2>&1 | tee "$BR/tests-arm64.log" | tail -n 4
SUM_ARM="$(grep -E 'tests passed' "$BR/tests-arm64.log" | tail -1)"
SUM_X86="skipped (no Rosetta)"
if [ $HAVE_ROSETTA = 1 ]; then
  echo "== tests (x86_64 under Rosetta)"
  (cd "$BR" && arch -x86_64 ./junkyard_tests) > "$BR/tests-x86_64.log" 2>&1 && X86_RC=0 || X86_RC=$?
  tail -n 4 "$BR/tests-x86_64.log"
  SUM_X86="$(tail -n 3 "$BR/tests-x86_64.log" | tr '\n' ' ')"
  [ $X86_RC = 0 ] || { echo "!! Intel tests FAILED (see $BR/tests-x86_64.log). Sort per the tolerance policy in docs/DECISIONS.md."; exit 1; }
fi

SUM_PV_ARM="skipped"; SUM_PV_X86="skipped"
if [ $VALIDATE = 1 ]; then
  PV="$ROOT/tools/bin/pluginval.app/Contents/MacOS/pluginval"
  if [ ! -x "$PV" ]; then
    mkdir -p "$ROOT/tools/bin"
    curl -fsSL -o "$ROOT/tools/bin/pluginval.zip" https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip
    (cd "$ROOT/tools/bin" && unzip -qo pluginval.zip && rm pluginval.zip)
  fi
  OPTS=(--strictness-level 10 --validate-in-process --skip-gui-tests --timeout-ms 300000)
  echo "== pluginval strictness 10 (arm64)"
  "$PV" "${OPTS[@]}" "$ART/VST3/Junkyard.vst3" > "$BR/pluginval-arm64.log" 2>&1 \
    || { tail -n 40 "$BR/pluginval-arm64.log"; exit 1; }
  SUM_PV_ARM="$(tail -n 1 "$BR/pluginval-arm64.log")"
  if [ $HAVE_ROSETTA = 1 ]; then
    echo "== pluginval strictness 10 (x86_64 under Rosetta)"
    arch -x86_64 "$PV" "${OPTS[@]}" "$ART/VST3/Junkyard.vst3" > "$BR/pluginval-x86_64.log" 2>&1 \
      || { tail -n 40 "$BR/pluginval-x86_64.log"; exit 1; }
    SUM_PV_X86="$(tail -n 1 "$BR/pluginval-x86_64.log")"
  fi
fi

echo
echo "== summary: Junkyard $VER universal Release"
echo "   slices:       VST3 $(lipo -archs "$VST3_BIN"), AU $(lipo -archs "$AU_BIN")"
echo "   tests arm64:  $SUM_ARM"
echo "   tests x86_64: $SUM_X86"
echo "   pluginval arm64:  $SUM_PV_ARM"
echo "   pluginval x86_64: $SUM_PV_X86"
