#!/bin/bash
# build.sh — build Kodi for tvOS or iOS, Debug or Release.
#
# Usage:
#   ./build.sh <tvos|ios> [debug|release] [--log[=FILE]]   (default configuration: debug)
#
# Examples:
#   ./build.sh tvos debug
#   ./build.sh ios release
#   ./build.sh tvos --log              (log to /tmp/kodi-build-<ts>.log)
#   ./build.sh ios release --log=my.log
#
# Notes:
#   - Switches the depends platform via tools/depends/configure (Makefile.include
#     is regenerated, so building one platform after the other is safe; the
#     depends prefixes for both platforms stay intact under /Users/Shared/xbmc-depends).
#   - SDK version is pinned to 26.4 to match the existing depends prefixes.
#   - Does NOT strip, archive, export, upload, or install. For a test build only.
#   - Requires the repo to live at its current path; the depends Makefile.include
#     records abs_top_srcdir at configure time.
set -euo pipefail

KODI_SRC="$(cd "$(dirname "$0")" && pwd)"
DEV_TEAM="9Q77WK7W3R"
SDK_VERSION="26.4"

usage() {
  echo "Usage: $0 <tvos|ios> [debug|release]" >&2
  exit 1
}

PLATFORM=""
CONFIG="debug"
LOG_FILE=""

for arg in "$@"; do
  case "$arg" in
    --log)
      LOG_FILE="/tmp/kodi-build-$(date +%Y%m%d-%H%M%S).log"
      ;;
    --log=*)
      LOG_FILE="${arg#--log=}"
      ;;
    tvos|ios)
      [ -z "$PLATFORM" ] && PLATFORM="$arg" || usage
      ;;
    debug|Debug|release|Release)
      [ -z "$CONFIG" ] || CONFIG="$arg"
      ;;
    *)
      usage
      ;;
  esac
done

[ -n "$PLATFORM" ] || usage

if [ -n "$LOG_FILE" ]; then
  mkdir -p "$(dirname "$LOG_FILE")"
  exec > >(tee "$LOG_FILE") 2>&1
  echo "Logging to: $LOG_FILE"
fi

case "$PLATFORM" in
  tvos)
    DEPENDS_PLATFORM="tvos"
    BUILD_DIR="$KODI_SRC/kodi-build"
    BUNDLE_ID="com.jocala.kodi.tvos"
    DESTINATION="generic/platform=tvOS"
    SDK_NAME="appletvos"
    ;;
  ios)
    DEPENDS_PLATFORM="ios"
    BUILD_DIR="$KODI_SRC/kodi-build-ios"
    BUNDLE_ID="com.jocala.kodi.ios"
    DESTINATION="generic/platform=iOS"
    SDK_NAME="iphoneos"
    ;;
  *)
    usage
    ;;
esac

case "$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')" in
  debug)   CONFIG="Debug" ;;
  release) CONFIG="Release" ;;
  *) usage ;;
esac

PRODUCT_DIR="${CONFIG}-${SDK_NAME}"   # e.g. Debug-appletvos, Release-iphoneos

echo "=== [0/3] Configure depends for $PLATFORM (sdk $SDK_VERSION) ==="
( cd "$KODI_SRC/tools/depends" && ./configure --with-platform="$DEPENDS_PLATFORM" --with-sdk="$SDK_VERSION" >/dev/null )

echo "=== [1/3] Regenerate Xcode project ($BUILD_DIR) ==="
make -C "$KODI_SRC/tools/depends/target/cmakebuildsys" \
  BUILD_DIR="$BUILD_DIR" \
  CMAKE_EXTRA_ARGUMENTS="-DENABLE_PVR=ON -DENABLE_GAMES=ON -DENABLE_PYTHON=ON -DPLATFORM_BUNDLE_IDENTIFIER=$BUNDLE_ID -DDEVELOPMENT_TEAM=$DEV_TEAM"

echo "=== [2/3] Build $PLATFORM ($CONFIG) ==="
# Remove stale symlinks that block MkDir during the build.
find "$BUILD_DIR/build/$PRODUCT_DIR" -maxdepth 2 \( -name "Kodi.app" -o -name "kodi-topshelf.appex" \) -type l -delete 2>/dev/null || true

if [ "$PLATFORM" = "ios" ]; then
  xcodebuild -project "$BUILD_DIR/kodi.xcodeproj" \
    -configuration "$CONFIG" \
    -destination "$DESTINATION" \
    CODE_SIGN_IDENTITY="Apple Development" \
    CODE_SIGN_STYLE=Automatic
else
  xcodebuild -project "$BUILD_DIR/kodi.xcodeproj" \
    -scheme kodi \
    -configuration "$CONFIG" \
    -destination "$DESTINATION"
fi

APP_PATH="$BUILD_DIR/build/$PRODUCT_DIR/Kodi.app"
if [ ! -d "$APP_PATH" ]; then
  echo "ERROR: build succeeded but $APP_PATH not found" >&2
  exit 1
fi

echo ""
echo "=== [3/3] Done ==="
echo "App: $APP_PATH"
du -sh "$APP_PATH"
