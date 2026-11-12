#!/usr/bin/env bash
# pluginval (+ auval when the AU is installed). Usage: validate.sh [quick|full|max]
set -euo pipefail
source "$(dirname "$0")/common.sh"
MODE="${1:-quick}"
case $MODE in quick) LEVEL=5;; full) LEVEL=8;; max) LEVEL=10;; *) LEVEL=5;; esac
BIN="$ROOT/tools/bin"
PV="$BIN/pluginval.app/Contents/MacOS/pluginval"
if [ ! -x "$PV" ]; then
  mkdir -p "$BIN"
  curl -fsSL -o "$BIN/pluginval.zip" https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip
  (cd "$BIN" && unzip -qo pluginval.zip && rm pluginval.zip)
fi
OPTS=(--strictness-level "$LEVEL" --validate-in-process --skip-gui-tests --timeout-ms 120000)
# GUI tests are skipped in headless/sandboxed runs unless JUNKYARD_GUI_TESTS=1
[ "${JUNKYARD_GUI_TESTS:-0}" = 1 ] && OPTS=(--strictness-level "$LEVEL" --validate-in-process --timeout-ms 180000)
echo "pluginval level $LEVEL: VST3"
"$PV" "${OPTS[@]}" "$VST3_SRC" > "$BUILD/pluginval-vst3.log" 2>&1 || { tail -n 40 "$BUILD/pluginval-vst3.log"; exit 1; }
tail -n 2 "$BUILD/pluginval-vst3.log"
# AU can only be scanned once installed (macOS must know about it): validate.sh <mode> au
if [ "${2:-}" = au ]; then
  AU_INST="$HOME/Library/Audio/Plug-Ins/Components/Junkyard.component"
  echo "pluginval level $LEVEL: AU (installed)"
  "$PV" "${OPTS[@]}" "$AU_INST" > "$BUILD/pluginval-au.log" 2>&1 || { tail -n 40 "$BUILD/pluginval-au.log"; exit 1; }
  tail -n 2 "$BUILD/pluginval-au.log"
fi
