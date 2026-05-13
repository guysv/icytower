#!/bin/bash
# Build IcyTower.app with bundled Homebrew libs (DylibBundler) plus gfx/sfx in Resources.
# Run from repo root after `make icytower` (with USE_ALLEGRO_PKG_CONFIG=1 on macOS CI).
#
# Usage:
#   packaging/macos/make-bundle.sh [--dmg] [--out-dir DIR]
# Env:
#   RELEASE_VERSION — CFBundleShortVersionString (default: git describe or "0-dev")

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

DO_DMG=false
OUT_DIR=""
while [[ "${1:-}" ]]; do
	case "$1" in
		--dmg) DO_DMG=true ;;
		--out-dir)
			shift
			OUT_DIR="${1:?--out-dir needs a directory}"
			;;
		-h | --help)
			echo "Usage: packaging/macos/make-bundle.sh [--dmg] [--out-dir DIR]"
			exit 0
			;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
	shift || true
done

VERSION="${RELEASE_VERSION:-}"
if [[ -z "$VERSION" ]] && git -C "$ROOT" describe --tags --always >/dev/null 2>&1; then
	VERSION="$(git -C "$ROOT" describe --tags --always)"
fi
VERSION="${VERSION:-0-dev}"

BINARY="$ROOT/icytower"
if [[ ! -x "$BINARY" ]]; then
	echo "missing executable: $BINARY (run make icytower first)" >&2
	exit 1
fi

APP_NAME="Icy Tower.app"
STAGE="${OUT_DIR:-$ROOT/build/macos-bundle}"
mkdir -p "$STAGE"
rm -rf "$STAGE/$APP_NAME"
APP="$STAGE/$APP_NAME"
MACOS="$APP/Contents/MacOS"
RESOURCES="$APP/Contents/Resources"
FW="$APP/Contents/Frameworks"

mkdir -p "$MACOS" "$RESOURCES" "$FW"

cp "$BINARY" "$MACOS/icytower_exe"
chmod +x "$MACOS/icytower_exe"

cat >"$MACOS/icytower" <<'LAUNCH'
#!/bin/bash
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE/../Resources"
exec "$HERE/icytower_exe"
LAUNCH
chmod +x "$MACOS/icytower"

cp -R "$ROOT/gfx" "$ROOT/sfx" "$RESOURCES/"

BUNDLE_ID="${MACOS_BUNDLE_ID:-git.royeldar.icytower}"

cat >"$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>icytower</string>
  <key>CFBundleIdentifier</key>
  <string>${BUNDLE_ID}</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>Icy Tower</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>${VERSION}</string>
  <key>CFBundleVersion</key>
  <string>${VERSION}</string>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>LSMinimumSystemVersion</key>
  <string>12.0</string>
</dict>
</plist>
PLIST

echo "DylibBundler…"
dylibbundler -cd -of -b \
	-x "$MACOS/icytower_exe" \
	-d "$FW" \
	-p '@executable_path/../Frameworks/'

codesign --force --deep -s - "$APP"

if [[ "$DO_DMG" == true ]]; then
	DMG="$STAGE/IcyTower-${VERSION}-macOS.dmg"
	rm -f "$DMG"
	hdiutil create -volname "Icy Tower ${VERSION}" -srcfolder "$APP" -ov -format UDZO "$DMG"
	echo "DMG: $DMG"
else
	echo "App bundle: $APP"
fi
