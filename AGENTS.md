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
| kphone   | iPhone 14 (iPhone14,7)               | EF8AC0BF-C213-5B5F-B92B-F86C7F7D489B |
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
- `xbmc/dialogs/GUIDialogRestore.h` / `.cpp` — GUI dialog with edit fields for host/username/password/sharepath + Save/Restore/Cancel (the old `xbmc/settings/RestoreManager.{h,cpp}` standalone path was **removed** — dead code, never wired into the build)
- `addons/skin.estuary/xml/DialogRestore.xml` — skin layout for the dialog
- `xbmc/guilib/WindowIDs.h` — added `WINDOW_DIALOG_RESTORE` (10161)
- `xbmc/input/WindowTranslator.cpp` — added `"dialogrestore"` mapping
- `xbmc/guilib/GUIWindowManager.cpp` — registers `CGUIDialogRestore`
- `xbmc/interfaces/builtins/SystemBuiltins.cpp` — added `restorefromnetwork` builtin; it opens `CGUIDialogRestore`

**tvOS restore / NSUserDefaults xml persistence (2026-08-06):**
- tvOS vectors `*.xml` under `special://home/userdata` into NSUserDefaults keys `/userdata/*` (`CTVOSFile`/`CTVOSNSUserDefaults`), while DBs/thumbnails etc. are real files in `Library/Caches/Kodi`. A physical `std::rename` swap of `userdata` doesn't move those keys.
- Restore now copies to `userdata_restore_tmp/`, then after the physical swap does (all tvOS-only, in `GUIDialogRestore.cpp`):
  1. `CTVOSNSUserDefaults::DeleteKeysWithPrefix("/userdata/")` — drop replaced settings
  2. `MoveKeysWithPrefix("/userdata_restore_tmp/", "/userdata/")` — guisettings.xml (copied via `XFILE::CFile` → keyed)
  3. `SyncTVOSXmlPersistence()` — recursively vectors any restored physical `.xml` into `/userdata/*` keys and deletes the physical copy (skips `Database/`, `Thumbnails/`); keeps xml durable across a Caches purge and avoids duplicate listings
- Validation probes: `Database/` via `CDirectory::Exists`, `guisettings.xml` via `CFile::Exists` (checks the key through `CTVOSFile`), with `bUseCache=false`.
- New helpers: `CTVOSNSUserDefaults::DeleteKeysWithPrefix` / `MoveKeysWithPrefix` (`TVOSNSUserDefaults.{h,mm}`).
- Verified: restore from `smb://192.168.1.39/storage/kodi/backup-08-03-26` replaced userdata fully — 35 `/userdata/*` xml keys, all 14 DBs physical, no `/userdata_restore_tmp/` leftovers, app relaunches clean.

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
- SMB URL embeds `username:password@host` in plaintext in logs

## App Store / TestFlight Build & Upload (2026-07-31)

Goal: get Kodi onto TestFlight. App Store server-side validation (error code 90171)
rejects any standalone Mach-O binary in the bundle other than the app executable
and supported bundles.

### What Apple rejected (now fixed)
- `script.module.pil` addon — shipped `.so` Python C extensions (`PIL/_imaging*.so`).
  **Removed entirely (2026-08-06)** along with `script.module.pycryptodome`
  (`Cryptodome/*.so`) — both were unused (no shipped addon imports PIL/Crypto;
  the TMDB scrapers use only stdlib `urllib`/`ssl`, which are statically linked
  into the binary via `libpython.a`). See "PIL/pycryptodome removal" below.
- `system/players/VideoPlayer/libdvdnav-aarch64.so` — DVD navigation; not needed
  for SMB playback; deleted.
- `Frameworks/lib` — empty dir; deleted.
- `Info.plist` had `UIFileSharingEnabled` as `<string>YES</string>`; Apple requires
  boolean `<true/>`. Fixed in `xbmc/platform/darwin/tvos/Info.plist.in:43`.

### ⚠️ Do NOT delete whole addon directories (crash found 2026-07-31)
The manifest lists **required** addons in
`AppData/AppHome/system/addon-manifest.xml`. `CAddonMgr::Init()` (in
`xbmc/addons/AddonManager.cpp`) hard-fails if any required addon is missing or
disabled:
```
critical: addon '<id>' not installed or not enabled.
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

### PIL/pycryptodome removal (2026-08-06)
To get movie/TV scraping working, Python was re-enabled (`ENABLE_PYTHON=ON`) on
both iOS and tvOS. `script.module.pil` and `script.module.pycryptodome` were
removed entirely (they were the only Python `.so` carriers and nothing used
them). Removal points (keep in sync):
- `tools/depends/target/Makefile` — `pythonmodule-pil`, `pythonmodule-pycryptodome` dropped from `DEPENDS`
- `system/addon-manifest.xml` — both `<addon>` entries removed
- `cmake/installdata/common/addons.txt` — both `addons/.../*` install lines removed
- stub addon dirs `addons/script.module.{pil,pycryptodome}/` deleted
- installed artifacts deleted from both `appletvos26.4` and `iphoneos26.4`
  prefixes: `share/Kodi/addons/script.module.pil`, `lib/python3.14/site-packages/Cryptodome`,
  `.../pycryptodomex-*.egg-info`
- Python native stdlib modules (`_ssl`, `_socket`, etc.) are **static** in
  `libpython.a` — `lib-dynload/` is empty, so re-enabling Python adds no new
  standalone Mach-O binaries. The store strip now also deletes `.so` under
  `Frameworks/lib` (catches anything native in site-packages).

### `_scproxy` stub (2026-08-06)
Movie/TV scraping initially failed with `ModuleNotFoundError: No module named
'_scproxy'`. `urllib/request.py` does `from _scproxy import _get_proxy_settings,
_get_proxies` when `sys.platform == 'darwin'` (true on iOS/tvOS), but the
macOS-only SystemConfiguration C extension is disabled for embedded builds
(`py_cv_module__scproxy=n/a` in `tools/depends/target/python3/Makefile` for
`darwin_embedded`). Fixed by overlaying a pure-Python `_scproxy.py` stub into
the packaged stdlib in `tools/darwin/Support/copyframeworks-darwin_embedded.command`
(runs every app build for both platforms; pure `.py` so App Store safe). Stub
returns `{'exclude_simple': False, 'exceptions': ()}` (the `exclude_simple` key
is indexed directly by `urllib/request.py`) and empty proxies.

Also: "Interfaces" settings item (`ActivateWindow(InterfaceSettings)`) was
restored — removed `<visible>false</visible>` from `addons/skin.estuary/xml/Settings.xml`.

### Known CMake gotcha
`xcodebuild archive` on the CMake-generated project produces an **empty**
`Products/Applications/` (products land in `UninstalledProducts/appletvos/`),
a silent `** ARCHIVE FAILED **`, exit 65. The `.xcarchive` must instead be
assembled manually from the built `Kodi.app` (see `build-testflight.sh`).

### Working pipeline (`build-testflight.sh`)
1. Regenerate Xcode project via `make -C tools/depends/target/cmakebuildsys`
   with `-DENABLE_PVR=ON -DENABLE_GAMES=ON -DENABLE_PYTHON=ON
   -DPLATFORM_BUNDLE_IDENTIFIER=com.jocala.kodi -DDEVELOPMENT_TEAM=9Q77WK7W3R`
2. Set version/build number in the two CMake-generated `Info.plist`s
   (build number = `YYYYMMDD.N` counter stored in `kodi-build/.buildnumber`)
3. `xcodebuild ... -configuration Release -destination generic/platform=tvOS`
   (delete stale `Kodi.app`/`kodi-topshelf.appex` **symlinks** in
   `kodi-build/build/Release-appletvos/` first, or `MkDir` fails)
4. Strip `.so` files from `AppData/AppHome` **and** `Frameworks/lib`
   (`find ... -name '*.so' -delete`); keeps the Python stdlib `.py` under
   `Frameworks/lib/python3.14` (needed at runtime) while removing native
   extension binaries; verify no `.so` left
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
Uses the **App Store Connect API key (JWT)** — no Apple ID app-specific password
needed. Key credentials (2026-08-07):
- Key ID: `D872KPNP65`
- Issuer ID: `69a6de6e-a9b5-47e3-e053-5b8c7c11a4d1`
- Key file: `~/.appstoreconnect/private_keys/AuthKey_D872KPNP65.p8` (chmod 600)
  — must live in one of altool's search locations (`~/private_keys`,
  `~/.private_keys`, `~/.appstoreconnect/private_keys`, or `$API_PRIVATE_KEYS_DIR`);
  `AuthKey_*.p8` is gitignored so it can never be committed.

`build-testflight.sh` passes `-authenticationKeyPath/-authenticationKeyID/
-authenticationKeyIssuerID` to `xcodebuild -exportArchive`, so the export uploads
with the API key automatically (defaults overridable via env
`ASC_API_KEY`/`ASC_API_ISSUER`/`ASC_API_KEY_PATH`).

Manual upload:
```bash
xcrun altool --upload-app -f "$BUILD_DIR/TestFlight/Kodi.ipa" -t tvos \
  --apiKey D872KPNP65 --apiIssuer 69a6de6e-a9b5-47e3-e053-5b8c7c11a4d1 \
  --p8-file-path "$HOME/.appstoreconnect/private_keys/AuthKey_D872KPNP65.p8"
# (deprecated alt) -u jeffelkins@gmail.com -p <APP_SPECIFIC_PASSWORD>
```
Test a key without uploading: `xcrun altool --generate-jwt --apiKey <KEY_ID> --apiIssuer <ISSUER_ID>`
(prints a signed JWT; `--list-providers` does NOT support API-key auth).

### Outcomes
- `/tmp/kodi-export2/Kodi.ipa` (manual run, 2026-07-31): **export + validation PASSED**
  after stripping. Nothing else was flagged.
- **2026-07-31: build `20260731.5` launched successfully on the Apple TV via
  TestFlight** — after fixing the strip step to delete only `.so` files (PIL
  addon kept). The prior `.4` crashed at launch (manifest check).
- **2026-08-06: Python re-enabled (`ENABLE_PYTHON=ON`) for movie/TV scraping on
  iOS + tvOS**; PIL and pycryptodome removed entirely (see "PIL/pycryptodome
  removal"). Dev installs keep Python's static stdlib; the store strip now also
  removes `.so` under `Frameworks/lib`.
- TestFlight gotcha: `xcodebuild -exportArchive` with `destination=upload`
  streams the `.ipa` straight to ASC (no local `.ipa` produced). New builds must
  be manually added to a TestFlight group (`Add to Group`); Automatic
  Distribution did not always attach them. If a freshly uploaded build is not
  visible in the TV's TestFlight app, force-quit TestFlight:
  `xcrun devicectl device process terminate --device <uuid> --pid <pid>` (PID via
  `xcrun devicectl device info processes`), or just relaunch it.
- Install-on-device for testing still uses `build-install.sh` (dev-signed build,
  `xcrun devicectl device install app`).

## iOS Build & TestFlight (2026-08-07)

Goal: get Kodi (as "Jocala Media Center") onto iPad/iPhone via TestFlight.

### iOS bundle ID / app record
- iOS uses a **distinct bundle ID** `com.jocala.kodi.ios` (tvOS keeps
  `com.jocala.kodi`). A bundle ID can belong to only one ASC app record, and iOS
  + tvOS must differ.
- ASC app record: **"Jocala Media Center iOS"** (id `6799227676`), team
  `9Q77WK7W3R`. Created in the ASC web UI — the App Store Connect **API cannot
  create apps** (`apps` allows only GET/UPDATE) and **cannot create internal beta
  groups** (`betaGroups` created via API are always external /
  `isInternalGroup: false`). Internal groups must be made in the web UI
  (TestFlight → Internal Testing).
- The App Store Connect API **cannot register a proper iOS App ID** either —
  `POST /v1/bundleIds` creates only a `UNIVERSAL`-platform record that the "New
  App" form rejects. Register the App ID as an explicit **iOS** App ID in the
  Developer portal (Certificates, Identifiers & Profiles → Identifiers).

### App Store rejections hit on iOS (and their fixes)
- **ITMS-90338 (non-public API)**: `hasExternalKeyboard()` used the private
  `UIKeyboardImpl` selector `isInHardwareKeyboardMode` for pre-iOS-14 devices.
  Removed the private-API fallback (`DarwinEmbedKeyboard.mm`) — iOS 14+ uses the
  public `GCKeyboard.coalescedKeyboard`; pre-14 returns false.
- **ITMS-90426 (SwiftSupport folder missing)**: caused by a bare
  `libshairplay.0.dylib` in `Frameworks/`. The tvOS build already converts
  dylibs into `.framework` bundles; iOS did not (commented-out TODO). Enabled
  the conversion for iOS (`copyframeworks-dylibs2frameworks.command`) and added
  `xbmc/platform/darwin/ios/FrameworkSeed_Info.plist`.
- **90022 (missing 120x120 icon)**: iOS ≥10 requires the app icon in an **asset
  catalog**; the legacy loose `AppIcon*.png` + `CFBundleIcons` plist approach is
  rejected. Added `xbmc/platform/darwin/ios/Assets.xcassets/AppIcon.appiconset`
  (generated from `~/Desktop/opencode/tv_1024.png`, opaque RGB), wired it in
  `cmake/scripts/darwin_embedded/Install.cmake`
  (`ASSETCATALOG_COMPILER_APPICON_NAME=AppIcon`), and removed the stale
  `CFBundleIcons` / `CFBundleIcons~ipad` blocks from the iOS `Info.plist.in`.
- **ITMS-90068 (MinimumOSVersion 12.0 too low)**: Apple requires ≥15.0 by Spring
  2027. `cmake/scripts/darwin_embedded/ArchSetup.cmake` now sets
  `IPHONEOS_DEPLOYMENT_TARGET 15.0` for iOS (warning-clean build).
- **ITMS-90788 (missing LSHandlerRank)**: added `LSHandlerRank=Alternate` +
  `CFBundleTypeRole=Viewer` to all four `CFBundleDocumentTypes` in the iOS
  `Info.plist.in`.

### iOS release pipeline
- `build-testflight-ios.sh` — mirror of the tvOS script: `kodi-build-ios` /
  `Release-iphoneos` / `com.jocala.kodi.ios`, own `.buildnumber` counter, patches
  only `kodi.dir/Info.plist`, strips `.so` + `config*`/`.o` under
  `AppData/AppHome`, assembles the `.xcarchive` manually, exports with
  `destination=upload` + API-key auth. Dev install uses `build-install-ios.sh`
  (now also `com.jocala.kodi.ios`).
- TestFlight group setup (via API): create the group, then
  `POST /v1/betaGroups/{id}/relationships/builds` for the build and
  `/relationships/betaTesters` for the tester. A beta tester must be created
  with a `betaGroups` relationship (`betaTesters` requires betaGroups or builds
  on CREATE); the app relationship can't be set on CREATE.
- Beta App Review: `POST /v1/betaAppReviewSubmissions`. Requires the **Beta App
  Description** (web-UI-only field) and contact info
  (`betaAppReviewDetails` PATCH — `contactPhone` needs a valid
  `+1...`-style number). Only **one build per version train** can be in review
  at a time (`ENTITY_UNPROCESSABLE.ANOTHER_BUILD_IN_REVIEW`); delete/remove the
  earlier one in the web UI or wait for it to resolve.
- Public TestFlight link: `PATCH /v1/betaGroups/{id}` with
  `publicLinkEnabled:true` (works only after the build passes Beta App Review).
  iOS external link: `https://testflight.apple.com/join/6DMeeMkV`.

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
| `xbmc/settings/` | Settings |
| `addons/skin.estuary/xml/` | Default skin XML layouts |
| `cmake/` | CMake modules, scripts, treedata |
| `tools/depends/` | Dependency builder scripts |

## Build for tvOS (Expected)

```bash
mkdir build-tvos && cd build-tvos
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/platform/darwin/tvos.toolchain.cmake \
      -DENABLE_PVR=OFF -DENABLE_GAMES=OFF \
      -DENABLE_PYTHON=ON \
      ..
make -j$(sysctl -n hw.logicalcpu)
```

(Exact toolchain and flags may vary — refer to `docs/` for platform guides.)

## adblink — Remote Restore/Backup for Apple TV Media Center (Exploratory)

**Status:** Exploratory only. Parked until the Kodi fork clears App Store review (or a 2nd upload round is required pre-acceptance). No work planned until then.

**Goal:** Build a function into `/Users/jeff/source/adblink/` to trigger the Apple TV Media Center app's **restore** (and optionally **backup**) functions over the network.

**Key finding — the gap:** adblink is **ADB-based and Android-only**. tvOS has no ADB, so adblink's device model (`adb -s <serial> shell`, `AdbDevice`, scoped storage, `/sdcard/xbmc_env.properties`) does not apply. A **network channel** is required.

**Current tvOS-side facts:**
- `restorefromnetwork` builtin (`xbmc/interfaces/builtins/SystemBuiltins.cpp:279`) only does `dlg->Open()`; the real work runs on a GUI button click (`CGUIDialogRestore::OnRestore()`, `xbmc/dialogs/GUIDialogRestore.cpp:396`) reading live edit-control values.
- Kodi has a JSON-RPC **TCP server** (port **9090**, `CTCPServer`) with `CTCPClient::GetPermissionFlags()` = `OPERATION_PERMISSION_ALL` (no auth — `xbmc/network/TCPServer.cpp:546`).
- It is **disabled on tvOS** — `NetworkServices.cpp:Start()` wraps JSON-RPC/EventServer/zeroconf/WSDiscovery in `#if !defined(TARGET_DARWIN_EMBEDDED)` (our watchdog/App-Store fix). Re-enabling for JSON-RPC only would require relaxing that guard.
- Restore/backup logic is GUI-coupled; needs a headless path reading `smb_restore.json`.

**Design (two options, B preferred):**
- **A:** Re-enable Kodi JSON-RPC TCP + add headless JSON-RPC methods. Con: full-permission LAN port, App Review exposure, watchdog-risk regression.
- **B (preferred):** Tiny token-gated HTTP endpoint in the tvOS app using **microhttpd** (already a depends target, `tools/depends/target/libmicrohttpd/`, but not in embedded `DEPENDS`) exposing e.g. `POST /restore`, `POST /backup`.

**Refactor principle (user-specified):**
- Extract shared core from `OnRestore()`/`OnBackup()` into headless methods (e.g. `bool DoRestore(SMBConfig)` / `bool DoBackup(SMBConfig)`) doing the existing copy/swap/upload logic.
- **GUI path remains unchanged** — `OnRestore`/`OnBackup` keep reading edit fields, call the same core, keep progress dialogs.
- **Headless is a pure addition** — network endpoint loads config from `smb_restore.json`, calls the same core, returns status over the network. Single source of truth.

**adblink side:** new manager (mirroring `BackupManager`) + UI (Kodi grid button/dialog), storing Apple TV IP + port + token + SMB config; use `QNetworkAccessManager` (already used by `kodidownloader`) or `QTcpSocket`. A separate Apple TV device record (adb table is Android-centric).

**Open questions for when work starts:** Option A vs B; restore only vs also backup; SMB config from device vs pushed by adblink; target release = post-approval `1.0.x` follow-up.

## Lockdown — Prevent Addon Modification (Planned)

**Status:** Planned. Single commit on `master`, no `tools/depends` rebuild, no store-metadata change. Additive guards only; does not disturb the upstream sync.

**Goal:** Closed system — only addons that ship inside `Kodi.app` and are approved by Apple at review time can ever execute. No user, file-sharing, or backup-restore path may introduce new code (`addon.xml` + `.py`/`.so`/`.dylib`) from `special://home`.

**Threat model:**
- `special://home/addons` directory (user-writable)
- SMB restore payload (crafted backup)
- iOS file sharing (`UIFileSharingEnabled` / `LSSupportsOpeningDocumentsInPlace`)
- Python `sys.path` shadowing (`special://home/addons` vs `special://xbmc/addons`)

**What is already locked (keep):**
- `AddonInstaller.cpp` — all install entry points (`InstallModal`, `InstallOrUpdate`, `Install`, `InstallFromZip`, `UnInstall`) return `false`
- `AddonManager.cpp` — `FindAddons("special://home/addons")` scan removed
- `AddonRepos.cpp` — `LoadAddonsFromDatabase` returns `false`
- `RepositoryUpdater.cpp` — `Start()` / `ScheduleUpdate()` no-ops
- `system/addon-manifest.xml` pins the required set; strip step deletes only `*.so`, never whole addon dirs (avoids `CAddonMgr::Init` crash)
- Skin hides Addons/PVR/Games (`Home.xml`, `Settings.xml`, `SkinSettings.xml`)

**Remaining vectors (to close):**
- **A. `special://home/addons` still exists and is writable.** Not scanned today, but a future upstream change re-adding the scan would make it live. A crafted backup could also plant files there if `CopyDirectory` ever copied outside `userdata`.
- **B. Restore copy filter is implicit.** `ResolveUserdataPath` returns only the `userdata` subfolder, so top-level `addons/` *should* be ignored, but there is no explicit `addons` skip in `CopyDirectory`/`UploadDirectory`.
- **C. File sharing surface (iOS only).** `UIFileSharingEnabled=true` is set for iOS. Need to verify `special://home` is not `Documents` (read-only check: `CSpecialProtocol::TranslatePath("special://home/")` vs `special://masterprofile/` vs `Documents`). If it is, lock to `false` for store builds.
- **D. Python import path.** Embedded `sys.path` historically included `special://home/addons`; must be locked to bundled `special://xbmc/addons` + `special://xbmcbin/addons` only.

**Plan — 5 steps (one commit):**

1. **Purge + guard `special://home/addons` at startup.**
   - In `AddonManager.cpp` (or early `CApplication::Create`): if `special://home/addons` exists, `RemoveRecursive` it and log `LOGINFO "Lockdown: purged stale home/addons"`. Do not re-create the directory. Optionally `chmod 0555` the parent so a later `mkdir` fails visibly.

2. **Explicit copy filter in restore.**
   - `GUIDialogRestore.cpp`: in `CopyDirectory` and `UploadDirectory`, skip any entry where `name == "addons"` (and defensively `*.so`/`*.dll`/`*.dylib`/`*.pyo` outside `addon_data`). Even if `ResolveUserdataPath` already excludes top-level `addons/`, this guarantees a malformed backup (`userdata/addons/...`) never restores code.

3. **Lock file sharing (iOS only).**
   - Read-only verification: compare `TranslatePath("special://home/")` and `special://masterprofile/` against `Documents`.
   - If `Documents` is inside `special://home`, set `UIFileSharingEnabled=false` in `xbmc/platform/darwin/ios/Info.plist.in` for store builds. If outside, keep `true` but document that `special://home` is not the shared `Documents`.

4. **Harden Python import.** `xbmc/interfaces/python/` — ensure `PYTHONPATH` is set to bundled addons only; never prepend `special://home/addons`.

5. **Re-verify skin.** `grep` that `SettingsCategory.xml` / `SettingsProfile.xml` `radiobutton id 8` remains `visible false` after upstream sync.

**Verification (must all fail to install):**
- Build `master` (`./build.sh tvos debug` + `ios debug`) → install to `appletv` + `jpad`.
- 1) UI `Install from zip` toast fails (already `false`).
- 2) Push crafted `special://home/addons/test.hello/addon.xml` via `devicectl` `copy to` → relaunch → `kodi.log` has no `Found addon test.hello`.
- 3) Craft backup on Mac containing `userdata/addons/evil/addon.xml` → restore via SMB → `restore_diag.txt` logs `skipped addons`, `special://home/addons` absent after launch.
- 4) Confirm `special://home/addons` does not exist after each launch.

**What this does NOT do (by design):**
- No signature/checksum of addons — unnecessary if the directory cannot exist; `addon-manifest.xml` already pins the set reviewed by Apple.
- No removal of existing approved addons in `special://xbmc/addons` / `special://xbmcbin/addons` — they are the only scanned paths.

**Open questions for execution:**
- File sharing on iOS: keep `UIFileSharingEnabled=true` for local Debug (handy via Files app) and `false` for store, or `false` everywhere?
- Purge vs ignore: delete `special://home/addons` at startup (most secure) or just ignore it? Recommend delete + log.
- Python path: explicit `PYTHONPATH` lock needed or is `FindAddons` removal sufficient for this threat model?
