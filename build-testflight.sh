#!/bin/bash
set -e

KODI_SRC="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$KODI_SRC/kodi-build"
APP_PATH="$BUILD_DIR/build/Release-appletvos/Kodi.app"
DEV_TEAM="9Q77WK7W3R"
BUNDLE_ID="com.jocala.kodi.tvos"
MARKETING_VERSION="${MARKETING_VERSION:-22.0}"

# Unique build number: date + incrementing counter (stored in gitignored kodi-build)
BUILDNUM_FILE="$BUILD_DIR/.buildnumber"
TODAY=$(date +%Y%m%d)
COUNT=1
if [ -f "$BUILDNUM_FILE" ]; then
  read -r LAST_DAY LAST_COUNT < "$BUILDNUM_FILE"
  if [ "$LAST_DAY" = "$TODAY" ]; then
    COUNT=$((LAST_COUNT + 1))
  fi
fi
BUILD_NUMBER="${BUILD_NUMBER:-$TODAY.$COUNT}"
echo "$TODAY $COUNT" > "$BUILDNUM_FILE"

ARCHIVE_PATH="$BUILD_DIR/TestFlight/Kodi.xcarchive"
IPA_PATH="$BUILD_DIR/TestFlight"
EXPORT_OPTS="$BUILD_DIR/TestFlight/ExportOptions.plist"

# App Store Connect API key (JWT) credentials. Key file must be named
# 'AuthKey_<KEY_ID>.p8' in one of altool's search locations, e.g.
# ~/.appstoreconnect/private_keys/. Override via env if you change keys.
ASC_API_KEY="${ASC_API_KEY:-D872KPNP65}"
ASC_API_ISSUER="${ASC_API_ISSUER:-69a6de6e-a9b5-47e3-e053-5b8c7c11a4d1}"
ASC_API_KEY_PATH="${ASC_API_KEY_PATH:-$HOME/.appstoreconnect/private_keys/AuthKey_${ASC_API_KEY}.p8}"

echo "=== 1. Regenerate Xcode project ==="
make -C "$KODI_SRC/tools/depends/target/cmakebuildsys" \
  BUILD_DIR="$BUILD_DIR" \
  CMAKE_EXTRA_ARGUMENTS="-DENABLE_PVR=ON -DENABLE_GAMES=ON -DENABLE_PYTHON=ON -DPLATFORM_BUNDLE_IDENTIFIER=$BUNDLE_ID -DDEVELOPMENT_TEAM=$DEV_TEAM"

echo ""
echo "=== 2. Set version (marketing: $MARKETING_VERSION, build: $BUILD_NUMBER) ==="
for plist in "$BUILD_DIR/CMakeFiles/kodi.dir/Info.plist" \
             "$BUILD_DIR/CMakeFiles/kodi-topshelf.dir/Info.plist"; do
  echo "  patching $plist"
  /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $MARKETING_VERSION" "$plist"
  /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $BUILD_NUMBER" "$plist"
done

echo ""
echo "=== 3. Build for tvOS (Release) ==="
# NOTE: 'xcodebuild archive' on this CMake project yields an empty Products/
# dir, so we build the app and assemble the archive manually.
# Remove stale symlinks that block MkDir during the build.
find "$BUILD_DIR/build/Release-appletvos" -maxdepth 2 -name "Kodi.app" -type l -delete 2>/dev/null || true
find "$BUILD_DIR/build/Release-appletvos" -maxdepth 2 -name "kodi-topshelf.appex" -type l -delete 2>/dev/null || true
xcodebuild -project "$BUILD_DIR/kodi.xcodeproj" \
  -scheme kodi \
  -configuration Release \
  -destination "generic/platform=tvOS"

echo ""
echo "=== 4. Strip App Store-objectionable files ==="
# App Store validation (code 90171) rejects standalone Mach-O .so binaries in
# the bundle. We delete ONLY the .so files — NOT the addon directories. Kodi's
# addon-manifest.xml requires system addons to be present and enabled at
# startup; deleting a whole addon directory makes CAddonMgr::Init fail and Kodi
# exits ("Unable to create application. Exiting"). Keeping addon.xml + .py
# sources satisfies Kodi; the .so files are what Apple rejects.
# libdvdnav-aarch64.so is not in the manifest and is only used for DVD
# playback, so removing it is safe for an SMB media player.
echo "  deleting .so binaries under AppData/AppHome"
find "$APP_PATH/AppData/AppHome" -name "*.so" -delete
# python build tree + stray Mach-O objects would trip App Store validation
find "$APP_PATH/AppData/AppHome/lib" -name "config*" -type d -prune -exec rm -rf {} +
find "$APP_PATH/AppData/AppHome/lib" -name "*.o" -delete
# UIFileSharingEnabled is fixed to boolean true in the source Info.plist.in.

echo ""
echo "=== 5. Assemble .xcarchive manually ==="
rm -rf "$ARCHIVE_PATH"
mkdir -p "$ARCHIVE_PATH/Products/Applications"
cp -R "$APP_PATH" "$ARCHIVE_PATH/Products/Applications/Kodi.app"
cat > "$ARCHIVE_PATH/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>ArchiveVersion</key>
	<integer>2</integer>
	<key>Name</key>
	<string>kodi</string>
	<key>SchemeName</key>
	<string>kodi</string>
	<key>ApplicationProperties</key>
	<dict>
		<key>ApplicationPath</key>
		<string>Applications/Kodi.app</string>
		<key>CFBundleIdentifier</key>
		<string>$BUNDLE_ID</string>
		<key>CFBundleShortVersionString</key>
		<string>$MARKETING_VERSION</string>
		<key>CFBundleVersion</key>
		<string>$BUILD_NUMBER</string>
	</dict>
</dict>
</plist>
EOF

echo ""
echo "=== 6. Verify no .so remains in app ==="
SO_FILES=$(find "$APP_PATH" -name "*.so" 2>/dev/null)
if [ -n "$SO_FILES" ]; then
  echo "ERROR: .so files still present:" >&2
  echo "$SO_FILES" >&2
  exit 1
fi
echo "OK: no .so files"

echo ""
echo "=== 7. Export .ipa ==="
mkdir -p "$IPA_PATH"
cat > "$EXPORT_OPTS" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>method</key>
	<string>app-store-connect</string>
	<key>teamID</key>
	<string>$DEV_TEAM</string>
	<key>destination</key>
	<string>upload</string>
	<key>signingStyle</key>
	<string>automatic</string>
</dict>
</plist>
EOF
rm -f "$IPA_PATH/Kodi.ipa"
xcodebuild -exportArchive \
  -archivePath "$ARCHIVE_PATH" \
  -exportOptionsPlist "$EXPORT_OPTS" \
  -exportPath "$IPA_PATH" \
  -authenticationKeyPath "$ASC_API_KEY_PATH" \
  -authenticationKeyID "$ASC_API_KEY" \
  -authenticationKeyIssuerID "$ASC_API_ISSUER" \
  -allowProvisioningUpdates

echo ""
echo "=== Done ==="
echo "Archive: $ARCHIVE_PATH"
echo "IPA:     $IPA_PATH/Kodi.ipa"
echo "Upload with:"
echo "  xcrun altool --upload-app -f '$IPA_PATH/Kodi.ipa' -t tvos"
echo "    --apiKey $ASC_API_KEY --apiIssuer $ASC_API_ISSUER --p8-file-path '$ASC_API_KEY_PATH'"
echo "  (or: -u jeffelkins@gmail.com -p <APP_SPECIFIC_PASSWORD>)"
