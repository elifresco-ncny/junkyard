#!/usr/bin/env bash
# Copy the dev build into the user's plugin folders, ad-hoc sign, refresh AU cache, run auval.
# Needs write access to ~/Library/Audio/Plug-Ins (disable the sandbox for this script).
set -euo pipefail
source "$(dirname "$0")/common.sh"
VST3_DST="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DST="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DST" "$AU_DST"
rsync -a --delete "$VST3_SRC/" "$VST3_DST/Junkyard.vst3/"
rsync -a --delete "$AU_SRC/" "$AU_DST/Junkyard.component/"
codesign --force --deep -s - "$VST3_DST/Junkyard.vst3"
codesign --force --deep -s - "$AU_DST/Junkyard.component"
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 1
if auval -v aufx Jky1 Jnky > "$BUILD/auval.log" 2>&1; then
  echo "auval: PASS"; tail -n 3 "$BUILD/auval.log"
else
  echo "auval: FAIL (see build/auval.log)"; tail -n 20 "$BUILD/auval.log"; exit 1
fi
"$ROOT/scripts/validate.sh" quick au || echo "(AU pluginval failed - see build/pluginval-au.log)"
N=$(cat "$BUILD/.build_number" 2>/dev/null || echo "?")
echo "Installed build #$N:"; echo "  $VST3_DST/Junkyard.vst3"; echo "  $AU_DST/Junkyard.component"
if pgrep -x Live >/dev/null 2>&1; then echo "Live is running: quit and reopen Live to load build #$N."; fi
