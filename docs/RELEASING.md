# Releasing Junkyard

Checklist for every release. Mac signing happens only on the owner's Mac (the certificate lives in its keychain); CI never signs.

## One-time setup (owner)
1. Apple Developer Program membership active; create **Developer ID Application** and **Developer ID Installer** certificates (one CSR file per certificate: Apple rejects a CSR that was already used) (developer.apple.com > Certificates, IDs & Profiles; only the Account Holder can). Check `security find-identity -v -p codesigning` lists `Developer ID Application: <Name> (<TEAMID>)` (the Installer identity shows with `security find-identity -v`). If it says "not trusted", install Apple's "Developer ID - G2" intermediate from apple.com/certificateauthority.
2. Create an app-specific password (account.apple.com > Sign-In and Security) named `notarytool`.
3. In your own Terminal (it prompts for the password; never give it to Claude):
   `xcrun notarytool store-credentials junkyard-notary --apple-id "<Apple ID>" --team-id "<TEAMID>"`
4. `cp scripts/release.env.example scripts/release.env` and fill in the two identity names and the profile name (git-ignored, not secret).

## Every release
1. Update `cmake/VersionNumber.cmake`, the version in the README download links, and add a short user-facing `## v<ver>` section to `docs/CHANGELOG.md` (CI uses it as the release notes).
2. `scripts/dev.sh` (tests + pluginval) then `scripts/release-mac.sh` (signed; run with the sandbox disabled: keychain, network, notarytool). Check `dist/`: `Junkyard-<ver>-mac.pkg`, `Junkyard-<ver>-mac.zip`, `SHA256SUMS.txt`. The script ends with `spctl` and `pkgutil --check-signature`; it must say `Notarized Developer ID`.
3. Commit, `git tag v<ver>`, push the commit and the tag (owner OK).
4. CI builds Windows and creates a **draft** GitHub Release with the Windows zip. Upload the signed Mac pkg, zip and `SHA256SUMS.txt` to it (`gh release upload v<ver> dist/Junkyard-<ver>-mac.pkg ...`). The owner reviews the draft and publishes it. Never auto-publish.
5. Post-release: download from the release page on a Mac (Safari, so it is quarantined) and a PC, install, load in the DAW.

Testing the `.pkg` on your own Mac: run `scripts/uninstall-dev.sh` first (otherwise the DAW lists two Junkyards), and `scripts/install-dev.sh` afterwards to return to dev builds.

## Beta builds before the certificate exists
`scripts/release-mac.sh --unsigned` makes an ad-hoc-signed `Junkyard-<ver>-mac-UNSIGNED.zip`. Recipients must clear the quarantine flag themselves (`xattr -dr com.apple.quarantine <bundle>`); for testers only, never published.

## Certificates and renewal
Developer ID certificates last 5 years; the Developer Program membership is yearly. Releases that are already notarized keep working if the membership lapses; new builds cannot be signed until it is renewed.

## Changing the repo URL
The About box and `SOURCE.txt` read `JUNKYARD_REPO_URL` in `cmake/Version.cmake`. The README download buttons link to the versioned assets (`releases/download/v<ver>/Junkyard-<ver>-mac.pkg` and `-windows-x64.zip`): update the version there for every release.
