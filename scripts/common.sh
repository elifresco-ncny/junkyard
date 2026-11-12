#!/usr/bin/env bash
# Shared helpers. Source me.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
ART="$BUILD/Junkyard_artefacts/RelWithDebInfo"
VST3_SRC="$ART/VST3/Junkyard.vst3"
AU_SRC="$ART/AU/Junkyard.component"
