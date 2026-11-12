#!/usr/bin/env bash
# Release packaging for macOS: universal build -> sign -> .pkg + .zip -> notarize -> staple -> verify.
# Usage: scripts/release-mac.sh [--unsigned] [--skip-build] [--no-validate] [--test-pkg]
#   --test-pkg (unsigned mode only): also build an UNSIGNED installer into dist/test/ to check the pkg structure; never distribute it
#   signed mode   (scripts/release.env defines all three variables): dist/Junkyard-<ver>-mac.pkg + dist/Junkyard-<ver>-mac.zip
#   unsigned mode (--unsigned, or any variable missing):               dist/Junkyard-<ver>-mac-UNSIGNED.zip (ad-hoc signed)
# Signing, notarytool and stapler need the sandbox disabled (keychain, network, xcrun cache).
# Claude never handles Apple passwords: notary credentials live in the keychain profile (see docs/RELEASING.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
UNSIGNED=0; SKIP_BUILD=0; TEST_PKG=0; BUILD_ARGS=()
for a in "$@"; do case $a in
  --unsigned) UNSIGNED=1;; --skip-build) SKIP_BUILD=1;; --test-pkg) TEST_PKG=1;; --no-validate) BUILD_ARGS+=(--no-validate);;
  *) echo "unknown option $a"; exit 2;; esac; done
[ -f "$ROOT/scripts/release.env" ] && source "$ROOT/scripts/release.env"
VER="$(sed -n 's/^set(JUNKYARD_VERSION \(.*\))$/\1/p' cmake/VersionNumber.cmake)"
REPO_URL="$(sed -n 's/^set(JUNKYARD_REPO_URL "\(.*\)")$/\1/p' cmake/Version.cmake)"
NOTARYTOOL=/Library/Developer/CommandLineTools/usr/bin/notarytool
STAPLER=/Library/Developer/CommandLineTools/usr/bin/stapler
SIGNED=1
if [ $UNSIGNED = 1 ] || [ -z "${JUNKYARD_SIGN_APP:-}" ] || [ -z "${JUNKYARD_SIGN_INSTALLER:-}" ] || [ -z "${JUNKYARD_NOTARY_PROFILE:-}" ]; then SIGNED=0; fi
for f in LICENSE THIRD_PARTY_NOTICES.md; do [ -f "$f" ] || { echo "!! missing $f"; exit 1; }; done

[ $SKIP_BUILD = 1 ] || "$ROOT/scripts/build-release-mac.sh" ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}
ART="$ROOT/build-release/Junkyard_artefacts/Release"
[ -d "$ART/VST3/Junkyard.vst3" ] && [ -d "$ART/AU/Junkyard.component" ] || { echo "!! no release build in build-release/"; exit 1; }

DIST="$ROOT/dist"; STAGE="$DIST/stage"
mkdir -p "$DIST"
rm -rf "$STAGE" "$DIST/test" "$DIST"/Junkyard-"$VER"-mac* "$DIST"/Junkyard-VST3.pkg "$DIST"/Junkyard-AU.pkg "$DIST"/*.plist "$DIST/resources" "$DIST/distribution.xml" "$DIST/SHA256SUMS.txt"
mkdir -p "$STAGE/vst3" "$STAGE/au"
ditto "$ART/VST3/Junkyard.vst3"      "$STAGE/vst3/Junkyard.vst3"
ditto "$ART/AU/Junkyard.component"   "$STAGE/au/Junkyard.component"
BUNDLES=("$STAGE/vst3/Junkyard.vst3" "$STAGE/au/Junkyard.component")

# Extra files that travel with every zip (AGPL source offer, license, notices, install notes).
make_docs() {  # $1 = target folder
  cp LICENSE "$1/LICENSE.txt"
  cp THIRD_PARTY_NOTICES.md "$1/THIRD_PARTY_NOTICES.txt"
  cp packaging/INSTALL-mac.txt "$1/INSTALL.txt"
  cat > "$1/SOURCE.txt" <<EOT
Junkyard $VER is free software under the GNU AGPL-3.0.
The complete corresponding source code of this release is at:
  $REPO_URL
  tag: v$VER
EOT
}

# ---------------------------------------------------------------- sign
if [ $SIGNED = 1 ]; then
  echo "== signing (Developer ID, hardened runtime, timestamp)"
  for b in "${BUNDLES[@]}"; do
    codesign --force --sign "$JUNKYARD_SIGN_APP" --options runtime --timestamp "$b"
    codesign --verify --strict --verbose=2 "$b"
    INFO="$(codesign -dv --verbose=4 "$b" 2>&1)"
    echo "$INFO" | grep -q "Authority=Developer ID Application" || { echo "!! $b: no Developer ID authority"; exit 1; }
    echo "$INFO" | grep -q "flags=0x10000(runtime)"            || { echo "!! $b: hardened runtime flag missing"; exit 1; }
    echo "$INFO" | grep -q "^Timestamp="                       || { echo "!! $b: no secure timestamp"; exit 1; }
  done
else
  echo "== UNSIGNED mode: ad-hoc signing only"
  echo "!! WARNING: this zip is NOT signed or notarized. Users must remove the quarantine flag (see INSTALL.txt)."
  for b in "${BUNDLES[@]}"; do
    codesign --force --sign - "$b"
    codesign --verify --strict --verbose=2 "$b"
  done
fi

# ---------------------------------------------------------------- pkg (relocation OFF)
build_pkg() {
  xattr -cr "$STAGE" 2>/dev/null || true   # extended attributes (com.apple.provenance) would end up in the pkg as ._ entries; the code signature does not cover them  # $1 = output pkg ; $2 = "signed" or "unsigned"
  echo "== component packages"
  for fmt in vst3:VST3:/Library/Audio/Plug-Ins/VST3 au:AU:/Library/Audio/Plug-Ins/Components; do
    id="${fmt%%:*}"; rest="${fmt#*:}"; name="${rest%%:*}"; loc="${rest#*:}"
    pkgbuild --analyze --root "$STAGE/$id" "$DIST/$id.plist" >/dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$DIST/$id.plist"
    pkgbuild --root "$STAGE/$id" --component-plist "$DIST/$id.plist" --install-location "$loc" \
             --identifier "com.junkyardaudio.junkyard.$id" --version "$VER" "$DIST/Junkyard-$name.pkg" >/dev/null
  done
  mkdir -p "$DIST/resources"
  cp packaging/mac/resources/welcome.txt "$DIST/resources/"
  cp LICENSE "$DIST/resources/LICENSE.txt"
  sed "s/@VERSION@/$VER/g" packaging/mac/distribution.xml > "$DIST/distribution.xml"
  mkdir -p "$(dirname "$1")"
  if [ "$2" = signed ]; then
    echo "== product archive (signed with Developer ID Installer)"
    productbuild --distribution "$DIST/distribution.xml" --resources "$DIST/resources" --package-path "$DIST" \
                 --sign "$JUNKYARD_SIGN_INSTALLER" --timestamp "$1"
  else
    echo "== product archive (UNSIGNED, structure test only)"
    productbuild --distribution "$DIST/distribution.xml" --resources "$DIST/resources" --package-path "$DIST" "$1"
  fi
}
# ---------------------------------------------------------------- unsigned: zip only
if [ $SIGNED = 0 ]; then
  Z="Junkyard-$VER-mac-UNSIGNED"
  rm -rf "$DIST/$Z"; mkdir -p "$DIST/$Z"
  ditto "$STAGE/vst3/Junkyard.vst3" "$DIST/$Z/Junkyard.vst3"
  ditto "$STAGE/au/Junkyard.component" "$DIST/$Z/Junkyard.component"
  make_docs "$DIST/$Z"
  (cd "$DIST" && ditto -c -k --norsrc --noextattr --noqtn --keepParent "$Z" "$Z.zip")
  rm -rf "$DIST/$Z"
  if [ $TEST_PKG = 1 ]; then build_pkg "$DIST/test/Junkyard-$VER-mac-TEST-UNSIGNED.pkg" unsigned; fi
  rm -rf "$STAGE" "$DIST"/Junkyard-VST3.pkg "$DIST"/Junkyard-AU.pkg "$DIST"/*.plist "$DIST/resources" "$DIST/distribution.xml"
  (cd "$DIST" && shasum -a 256 *.zip > SHA256SUMS.txt)
  echo "== done (UNSIGNED): $DIST/$Z.zip"; cat "$DIST/SHA256SUMS.txt"
  exit 0
fi

PKG="$DIST/Junkyard-$VER-mac.pkg"
build_pkg "$PKG" signed

notarize() {  # $1 = file ; prints the log and stops on anything but Accepted
  local out; out="$("$NOTARYTOOL" submit "$1" --keychain-profile "$JUNKYARD_NOTARY_PROFILE" --wait 2>&1)" || true
  echo "$out"
  if ! echo "$out" | grep -q "status: Accepted"; then
    local id; id="$(echo "$out" | sed -n 's/^ *id: \([0-9a-f-]*\).*/\1/p' | head -1)"
    [ -n "$id" ] && "$NOTARYTOOL" log "$id" --keychain-profile "$JUNKYARD_NOTARY_PROFILE" || true
    echo "!! notarization of $1 was not accepted (usual causes: missing --options runtime / --timestamp, unsigned nested binary)"; exit 1
  fi
}
echo "== notarize + staple pkg"
notarize "$PKG"
"$STAPLER" staple "$PKG"
"$STAPLER" validate "$PKG"

# ---------------------------------------------------------------- manual-install zip
echo "== manual-install zip"
ZF="Junkyard-$VER-mac"; ZIP="$DIST/$ZF.zip"
build_zip() {
  rm -rf "$DIST/$ZF" "$ZIP"; mkdir -p "$DIST/$ZF"
  ditto "$STAGE/vst3/Junkyard.vst3"    "$DIST/$ZF/Junkyard.vst3"
  ditto "$STAGE/au/Junkyard.component" "$DIST/$ZF/Junkyard.component"
  make_docs "$DIST/$ZF"
  (cd "$DIST" && ditto -c -k --norsrc --noextattr --noqtn --keepParent "$ZF" "$ZF.zip")
}
build_zip
notarize "$ZIP"
# Try to staple the tickets into the bundles. If stapler refuses the bundle type, Gatekeeper checks online: fine.
STAPLED=1
for b in "$STAGE/vst3/Junkyard.vst3" "$STAGE/au/Junkyard.component"; do
  "$STAPLER" staple "$b" || { echo "(stapler refused $(basename "$b"); keeping the unstapled zip, Gatekeeper will check online)"; STAPLED=0; }
done
if [ $STAPLED = 1 ]; then build_zip; echo "zip rebuilt with stapled bundles (not re-notarized: the tickets are valid)"; fi
rm -rf "$DIST/$ZF"

# ---------------------------------------------------------------- checksums + verification
(cd "$DIST" && shasum -a 256 Junkyard-"$VER"-mac.pkg Junkyard-"$VER"-mac.zip > SHA256SUMS.txt)
echo "== verify like a user who downloaded it"
Q="$(mktemp -d)/q.pkg"; cp "$PKG" "$Q"
xattr -w com.apple.quarantine "0081;$(printf %x "$(date +%s)");Safari;" "$Q"
spctl --assess --type install -vv "$Q" 2>&1 | tee "$DIST/spctl.log"
grep -q "source=Notarized Developer ID" "$DIST/spctl.log" || { echo "!! spctl did not say Notarized Developer ID"; exit 1; }
pkgutil --check-signature "$PKG"
echo "== done (signed): $PKG"; cat "$DIST/SHA256SUMS.txt"
