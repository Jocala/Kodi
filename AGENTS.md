# Kodi — tvOS Build Context

## Project

- **Kodi** (formerly XBMC) — open-source cross-platform media center
- Upstream: `https://github.com/xbmc/xbmc.git` (branch `master`)
- Current version: **22.0 BETA1** (code 21.90.801)
- License: GPL-2.0-or-later
- Organization: XBMC Foundation / Team Kodi

## Developer

- **Name**: Jeff Elkins
- **Apple Team**: jeff elkins (Team ID: `9Q77WK7W3R`)
- **Apple Account**: jeff elkins (B459C7RRUN)
- **Email**: jeffelkins@gmail.com
- **Org bundle prefix**: `com.jocala.*`

## Dev Environment

- **macOS**: 26.5.2 (build 25F84)
- **Xcode**: 26.4.1 (build 17E202)
- **Swift**: Apple Swift 6.3.1 (swiftlang-6.3.1.1.2, clang-2100.0.123.102)
- **Target arch**: arm64-apple-macosx26.0
- **Android SDK**: Gradle 8.7, JDK 21.0.1 (Temurin)

## Connected Devices

| Name     | Model                                | Identifier |
|----------|--------------------------------------|------------|
| appletv  | Apple TV 4K (3rd gen, AppleTV14,1)   | 942EFDDE-3584-5D12-9672-17CA071EB5E5 |
| jpad     | iPad (A16, iPad15,7)                 | 83AD472F-8BE9-58BD-9936-4EDF5B660609 |
| jphone   | iPhone 15 Pro Max (iPhone16,2)       | 2A26B51F-5502-5D00-AECF-76896E24EDF1 |
| mpad     | iPad mini 6th gen (iPad14,1)         | 8BACA08C-16D6-50EB-82F1-FC15946B004F |
| opad     | iPad Air 3rd gen (iPad11,3)          | 9B5909D2-1931-5CD8-B6E8-E29C1F39F10D |

## Certificates & Signing

| Type | Subject | Status |
|------|---------|--------|
| Apple Development | jeff elkins (B459C7RRUN) | ✅ Valid |
| Developer ID Application | jeff elkins (9Q77WK7W3R) | ✅ Valid |
| Apple Distribution | jeff elkins (9Q77WK7W3R) | ✅ Valid |

All required signing certificates are present and valid.

Provisioning profiles exist for `com.jocala.*` (iOS + tvOS).

## Recent Work (Initial Commit `b6ce94b6f0`)

### Goal
Produce a minimal Kodi build for **Apple TV (tvOS)** with network-based userdata restore from SMB.

### Changes

**Build system:**
- Made PVR, Games, and RetroPlayer optional — moved `cmake/treedata/common/{games,pvr,retroplayer}.txt` → `cmake/treedata/optional/common/`
- Added `ENABLE_PVR` and `ENABLE_GAMES` CMake options (default `OFF`)
- Added `GUIDialogRestore` to `xbmc/dialogs/CMakeLists.txt`

**Addon system disabled:**
- `AddonInstaller.cpp` — `InstallModal`, `InstallOrUpdate`, `Install`, `InstallFromZip`, `UnInstall` all return `false`
- `AddonManager.cpp` — removed `FindAddons("special://home/addons")` scan path
- `AddonRepos.cpp` — `LoadAddonsFromDatabase` returns `false`
- `RepositoryUpdater.cpp` — `Start()` and `ScheduleUpdate()` are no-ops (no repo polling)

**SMB Restore feature (new):**
- `xbmc/settings/RestoreManager.h` / `.cpp` — standalone restore logic (prompt for SMB config, copy userdata, fix guisettings.xml, quit)
- `xbmc/dialogs/GUIDialogRestore.h` / `.cpp` — GUI dialog with edit fields for host/username/password/sharepath + Save/Restore/Cancel
- `addons/skin.estuary/xml/DialogRestore.xml` — skin layout for the dialog
- `xbmc/guilib/WindowIDs.h` — added `WINDOW_DIALOG_RESTORE` (10161)
- `xbmc/input/WindowTranslator.cpp` — added `"dialogrestore"` mapping
- `xbmc/guilib/GUIWindowManager.cpp` — registers `CGUIDialogRestore`
- `xbmc/interfaces/builtins/SystemBuiltins.cpp` — added `restorefromnetwork` builtin command

**UI simplified (skin.estuary):**
- `Home.xml` — TV, Radio, Games, Addons, Pictures, Weather sidebar items all hidden (`<visible>false</visible>`)
- `Settings.xml` — added "Restore" item; hidden Addons, PVR Settings, Game Settings
- `SkinSettings.xml` — hidden TV/Radio/Games/Addons visibility toggles

**Entitlements:**
- `Kodi.entitlements.in` and `TopShelf.entitlements.in` — removed `com.apple.security.application-groups`

### Known Concerns
- Hardcoded default SMB credentials in `GUIDialogRestore.cpp` (host: `192.168.1.39`, user: `jeff`, password: `xky91234`)
- SMB URL embeds `username:password@host` in plaintext in logs

## Build System

- **CMake** >= 3.18 (3.30.6 on Windows), project `kodi` with languages C, C++, ASM
- Key CMake options: `ENABLE_PVR` (OFF), `ENABLE_GAMES` (OFF), `ENABLE_PYTHON` (ON), `ENABLE_TESTING` (ON)
- Internal depends managed via `tools/depends/` (FFmpeg, libcurl, libcec, etc.)
- CI: GitHub Actions (SonarQube, documentation, stale issues, Weblate sync)
- Linting: `.clang-format`, `.clang-tidy`, `.editorconfig`

## Key Source Directories

| Path | Contents |
|------|----------|
| `xbmc/` | Core C++ application source |
| `xbmc/addons/` | Addon management (installer, manager, repos, updater) |
| `xbmc/dialogs/` | GUI dialog classes |
| `xbmc/guilib/` | Core GUI library (windows, controls, window manager) |
| `xbmc/input/` | Input handling, window translator |
| `xbmc/interfaces/builtins/` | Builtin command operations |
| `xbmc/platform/darwin/tvos/` | tvOS-specific platform code, entitlements |
| `xbmc/settings/` | Settings, RestoreManager |
| `addons/skin.estuary/xml/` | Default skin XML layouts |
| `cmake/` | CMake modules, scripts, treedata |
| `tools/depends/` | Dependency builder scripts |

## Build for tvOS (Expected)

```bash
mkdir build-tvos && cd build-tvos
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/platform/darwin/tvos.toolchain.cmake \
      -DENABLE_PVR=OFF -DENABLE_GAMES=OFF \
      -DENABLE_PYTHON=OFF \
      ..
make -j$(sysctl -n hw.logicalcpu)
```

(Exact toolchain and flags may vary — refer to `docs/` for platform guides.)
