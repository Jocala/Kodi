#!/bin/bash
set -e

WIPE=0
case "$1" in
  --wipe) WIPE=1 ;;
  "") ;;
  *) echo "Usage: $0 [--wipe]" >&2; exit 1 ;;
esac

KODI_SRC="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$KODI_SRC/kodi-build"
APP_PATH="$BUILD_DIR/build/Debug-appletvos/Kodi.app"
DEVICE="appletv"
BUNDLE_ID="com.jocala.kodi"
DEV_TEAM="9Q77WK7W3R"

echo "=== 1. Regenerate Xcode project ==="
make -C "$KODI_SRC/tools/depends/target/cmakebuildsys" \
  BUILD_DIR="$BUILD_DIR" \
  CMAKE_EXTRA_ARGUMENTS="-DENABLE_PVR=ON -DENABLE_GAMES=ON -DENABLE_PYTHON=OFF -DPLATFORM_BUNDLE_IDENTIFIER=$BUNDLE_ID -DDEVELOPMENT_TEAM=$DEV_TEAM"

echo ""
echo "=== 2. Build for tvOS (Xcode handles signing) ==="
xcodebuild -project "$BUILD_DIR/kodi.xcodeproj" \
  -config Debug \
  -destination "generic/platform=tvOS"

if [ "$WIPE" -eq 1 ]; then
  echo ""
  echo "=== 3. Uninstall old app from device (wipe) ==="
  xcrun devicectl device uninstall app --device "$DEVICE" "$BUNDLE_ID" 2>/dev/null || true
fi

echo ""
echo "=== Install to device (data preserved) ==="
xcrun devicectl device install app --device "$DEVICE" "$APP_PATH"

echo ""
echo "=== Launch ==="
xcrun devicectl device process launch --device "$DEVICE" --terminate-existing "$BUNDLE_ID"

echo ""
echo "=== Done ==="
