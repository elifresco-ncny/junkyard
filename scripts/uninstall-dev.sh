#!/usr/bin/env bash
# Removes the dev copies of Junkyard from ~/Library (run before testing the system-wide .pkg, otherwise Live shows two).
# scripts/install-dev.sh puts them back. Needs the sandbox disabled (writes to ~/Library/Audio/Plug-Ins).
set -euo pipefail
rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/Junkyard.vst3" "$HOME/Library/Audio/Plug-Ins/Components/Junkyard.component"
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Removed the dev copies. Restart Live and rescan."
