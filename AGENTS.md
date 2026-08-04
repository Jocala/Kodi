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

**Home screen logo (upper-left):**
- The Kodi "K" glyph + "KODI" wordmark in the upper-left of the home screen is a **single image**, `media/vendor_logo.png` (465×128, mark + wordmark baked together — there is no separate text label).
- Rendered by `addons/skin.estuary/xml/Home.xml:1173-1180` — an `<image>` control (`left:4, top:0`, 192×56, `<texture>special://xbmc/media/vendor_logo.png</texture>`) inside a group at `left:90, top:30`.
- To hide/swap: edit that one image control (`<visible>false</visible>` or change `<texture>`). Same logo also appears on `LoginScreen.xml:19`.
- May need replacing with the TV icon for branding consistency.

**Entitlements:**
- `Kodi.entitlements.in` and `TopShelf.entitlements.in` — removed `com.apple.security.application-groups`

### Known Concerns
- Hardcoded default SMB credentials in `GUIDialogRestore.cpp` (host: `192.168.1.39`, user: `jeff`, password: `xky91234`)
- SMB URL embeds `username:password@host` in plaintext in logs

## App Store / TestFlight Build & Upload (2026-07-31)

Goal: get Kodi onto TestFlight. App Store server-side validation (error code 90171)
rejects any standalone Mach-O binary in the bundle other than the app executable
and supported bundles.

### What Apple rejected (now fixed)
- `script.module.pil` addon — shipped `.so` Python C extensions (`PIL/_imaging*.so`).
  Useless with `ENABLE_PYTHON=OFF`; deleted entirely from the app bundle.
- `system/players/VideoPlayer/libdvdnav-aarch64.so` — DVD navigation; not needed
  for SMB playback; deleted.
- `Frameworks/lib` — empty dir; deleted.
- `Info.plist` had `UIFileSharingEnabled` as `<string>YES</string>`; Apple requires
  boolean `<true/>`. Fixed in `xbmc/platform/darwin/tvos/Info.plist.in:43`.

### ⚠️ Do NOT delete whole addon directories (crash found 2026-07-31)
`script.module.pil` is listed as a **required** addon in
`AppData/AppHome/system/addon-manifest.xml`. `CAddonMgr::Init()` (in
`xbmc/addons/AddonManager.cpp`) hard-fails if it's missing/enabled:
```
critical: addon 'script.module.pil' not installed or not enabled.
critical: CServiceManager::InitStageTwo: Unable to start CAddonMgr
error:   ERROR: Unable to create application. Exiting
```
Kodi then calls `exit(0)` and SIGSEGVs in static teardown
(`CApplication::~CApplication` → `CAddonMgr::~CAddonMgr` →
`CDirectoryCache::FileExists`), so the app "installs but won't open".
The `build-testflight.sh` strip step therefore deletes **only `.so` files**
(`find .../AppData/AppHome -name '*.so' -delete`), keeping `addon.xml` + `.py`
sources so Kodi starts. `libdvdnav-aarch64.so` is NOT in the manifest, so
deleting it is safe (only used for DVD playback).

### Known CMake gotcha
`xcodebuild archive` on the CMake-generated project produces an **empty**
`Products/Applications/` (products land in `UninstalledProducts/appletvos/`),
a silent `** ARCHIVE FAILED **`, exit 65. The `.xcarchive` must instead be
assembled manually from the built `Kodi.app` (see `build-testflight.sh`).

### Working pipeline (`build-testflight.sh`)
1. Regenerate Xcode project via `make -C tools/depends/target/cmakebuildsys`
   with `-DENABLE_PVR=ON -DENABLE_GAMES=ON -DENABLE_PYTHON=OFF
   -DPLATFORM_BUNDLE_IDENTIFIER=com.jocala.kodi -DDEVELOPMENT_TEAM=9Q77WK7W3R`
2. Set version/build number in the two CMake-generated `Info.plist`s
   (build number = `YYYYMMDD.N` counter stored in `kodi-build/.buildnumber`)
3. `xcodebuild ... -configuration Release -destination generic/platform=tvOS`
   (delete stale `Kodi.app`/`kodi-topshelf.appex` **symlinks** in
   `kodi-build/build/Release-appletvos/` first, or `MkDir` fails)
4. Strip `.so` files from `AppData/AppHome` (`find ... -name '*.so' -delete`;
   keeps addon `addon.xml`+`.py` so the manifest check passes), remove empty
   `Frameworks/lib`, verify no `.so` left
5. Assemble `.xcarchive` by hand (copy `Kodi.app` to `Products/Applications/`,
   write archive `Info.plist` with `ApplicationProperties`)
6. `xcodebuild -exportArchive ... -exportOptionsPlist ExportOptions.plist
   -allowProvisioningUpdates` with `method=app-store-connect`,
   `teamID=9Q77WK7W3R`, `signingStyle=automatic` → produces `Kodi.ipa`

### Signing / profiles
- Dev build signs with **development** identity — correct for `devicectl` install,
  wrong for App Store.
- Store export auto-creates/uses App Store distribution profiles on disk:
  `~/Library/Developer/Xcode/UserData/Provisioning Profiles/` has
  `tvOS Team Store Provisioning Profile: com.jocala.kodi` (+ `.topshelf`).
  Requires `-allowProvisioningUpdates` if they don't yet exist.
- Store profile entitlements: `get-task-allow=false`, `beta-reports-active=true`,
  `application-groups` group.com.jocala.kodi.
- ASC app record: tvOS "Jocala Media", adamId `6796066358`, provider `69a6de6e...`.

### Uploading to App Store Connect
```bash
xcrun altool --upload-app -f "$BUILD_DIR/TestFlight/Kodi.ipa" -t tvos \
  -u jeffelkins@gmail.com -p <APP_SPECIFIC_PASSWORD>
# or --apiKey <KEY_ID> --apiIssuer <ISSUER_ID> --apiKeyPath <PATH_TO_P8>
```
No app-specific password is stored in keychain; user must supply one.

### Outcomes
- `/tmp/kodi-export2/Kodi.ipa` (manual run, 2026-07-31): **export + validation PASSED**
  after stripping. Nothing else was flagged.
- **2026-07-31: build `20260731.5` launched successfully on the Apple TV via
  TestFlight** — after fixing the strip step to delete only `.so` files (PIL
  addon kept). The prior `.4` crashed at launch (manifest check).
- TestFlight gotcha: `xcodebuild -exportArchive` with `destination=upload`
  streams the `.ipa` straight to ASC (no local `.ipa` produced). New builds must
  be manually added to a TestFlight group (`Add to Group`); Automatic
  Distribution did not always attach them. If a freshly uploaded build is not
  visible in the TV's TestFlight app, force-quit TestFlight:
  `xcrun devicectl device process terminate --device <uuid> --pid <pid>` (PID via
  `xcrun devicectl device info processes`), or just relaunch it.
- Install-on-device for testing still uses `build-install.sh` (dev-signed build,
  `xcrun devicectl device install app`).

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
