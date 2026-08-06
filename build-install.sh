#!/bin/bash
set -e

WIPE=0
PRESERVE_CREDS=0
case "$1" in
  --wipe) WIPE=1 ;;
  --wipe-preserve) WIPE=1; PRESERVE_CREDS=1 ;;
  "") ;;
  *) echo "Usage: $0 [--wipe|--wipe-preserve]" >&2; exit 1 ;;
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
  CMAKE_EXTRA_ARGUMENTS="-DENABLE_PVR=ON -DENABLE_GAMES=ON -DENABLE_PYTHON=ON -DPLATFORM_BUNDLE_IDENTIFIER=$BUNDLE_ID -DDEVELOPMENT_TEAM=$DEV_TEAM"

echo ""
echo "=== 2. Build for tvOS (Xcode handles signing) ==="
xcodebuild -project "$BUILD_DIR/kodi.xcodeproj" \
  -config Debug \
  -destination "generic/platform=tvOS"

if [ "$WIPE" -eq 1 ]; then
  if [ "$PRESERVE_CREDS" -eq 1 ]; then
    echo "=== 3a. Snapshot SMB creds (sources.xml + passwords.xml) ==="
    python3 <<'PYEOF2'
import plistlib, gzip, pathlib, sys
try:
    src = pathlib.Path("/tmp/plist-snapshot.plist")
    # copy current plist off device
    import subprocess
    r = subprocess.run(
        ["xcrun","devicectl","device","copy","from","--device","appletv",
         "--domain-type","appDataContainer","--domain-identifier","com.jocala.kodi",
         "--source","Library/Preferences/com.jocala.kodi.plist","--destination",str(src)],
        capture_output=True)
    if src.exists() and src.stat().st_size>0:
        d = plistlib.loads(src.read_bytes())
        snap = {k: d[k] for k in ["/userdata/sources.xml","/userdata/passwords.xml"] if k in d}
        pathlib.Path("/tmp/smb-preserve-snapshot.plist").write_bytes(plistlib.dumps(snap, fmt=plistlib.FMT_BINARY))
        print(f"  snap: {list(snap.keys())}")
    else:
        print("  no plist yet (fresh device), skipping snapshot")
except Exception as e:
    print(f"  snapshot failed: {e}", file=sys.stderr)
PYEOF2
  fi
  echo ""
  echo "=== 3. Uninstall old app from device (wipe) ==="
  xcrun devicectl device uninstall app --device "$DEVICE" "$BUNDLE_ID" 2>/dev/null || true
fi

echo ""
echo "=== Install to device (data preserved) ==="
xcrun devicectl device install app --device "$DEVICE" "$APP_PATH"

if [ "$WIPE" -eq 1 ] && [ "$PRESERVE_CREDS" -eq 1 ] && [ -s /tmp/smb-preserve-snapshot.plist ]; then
  echo ""
  echo "=== 3b. Restore SMB creds ==="
  # A freshly installed app has no Preferences plist until it has run once,
  # so launch it first, then pull the plist it just created.
  xcrun devicectl device process launch --device "$DEVICE" "$BUNDLE_ID"
  for _ in $(seq 1 20); do
    if xcrun devicectl device copy from --device "$DEVICE" --domain-type appDataContainer \
      --domain-identifier "$BUNDLE_ID" --source "Library/Preferences/com.jocala.kodi.plist" \
      --destination "/tmp/plist-after.plist" 2>/dev/null; then
      break
    fi
    sleep 2
  done
  if [ -s /tmp/plist-after.plist ]; then
    python3 <<'PYEOF3'
import plistlib, pathlib
snap = plistlib.loads(pathlib.Path("/tmp/smb-preserve-snapshot.plist").read_bytes())
d = plistlib.loads(pathlib.Path("/tmp/plist-after.plist").read_bytes())
d.update(snap)
pathlib.Path("/tmp/plist-after.plist").write_bytes(plistlib.dumps(d, fmt=plistlib.FMT_BINARY))
print(f"  merged {list(snap.keys())} into plist")
PYEOF3
    xcrun devicectl device copy to --device "$DEVICE" --domain-type appDataContainer \
      --domain-identifier "$BUNDLE_ID" --source /tmp/plist-after.plist \
      --destination "Library/Preferences/com.jocala.kodi.plist"
    echo "  restored SMB creds"
  else
    echo "  no plist yet, skipping cred restore"
  fi
fi

echo ""
echo "=== Launch ==="
xcrun devicectl device process launch --device "$DEVICE" --terminate-existing "$BUNDLE_ID"

echo ""
echo "=== Done ==="
