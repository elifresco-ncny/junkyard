# Building Junkyard

Requirements: CMake 3.24+, a C++20 compiler. JUCE 8 and Catch2 are downloaded automatically.

- **macOS** (Command Line Tools, CMake, Ninja): `scripts/dev.sh` runs the development loop (arm64 build, tests, pluginval, install into `~/Library/Audio/Plug-Ins`). `scripts/build-release-mac.sh` makes a universal Release build; `scripts/release-mac.sh` packages it (see [RELEASING.md](RELEASING.md)).
- **Windows** (Visual Studio 2022 Build Tools, CMake): `scripts/build-windows.ps1`.

Dev builds go into `~/Library/Audio/Plug-Ins` (ad-hoc signed). Before testing the system-wide `.pkg`, run `scripts/uninstall-dev.sh`, otherwise your DAW lists two Junkyards; `scripts/install-dev.sh` puts the dev copies back. Most DAWs do not reload plug-ins while running: restart the DAW after each build (dev builds show the build number bottom-right).

User presets: `~/Library/Audio/Presets/Junkyard Audio/Junkyard/*.jyd` (Mac), `Documents\Junkyard Audio\Junkyard\Presets\*.jyd` (Windows).

Design notes: [DECISIONS.md](DECISIONS.md). Tests: [TESTING.md](TESTING.md).
