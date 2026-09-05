#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/environment.sh"
cmake -S "$ALIAS" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DSNAP_ROM="$ALIAS/pokemonsnap.z64"
cmake --build "$BUILD" --target Snap64Recomp -j 8
SNAP_BUILD_DIR="$BUILD" "$ROOT/macos/script/build_icon.sh"
APP="${SNAP_APP_PATH:-$HOME/Applications/Pokemon Snap.app}"
if lsof -t "$APP/Contents/MacOS/Snap64Recomp" >/dev/null 2>&1; then
  echo "Compilation finished. Quit $APP before replacing its running bundle." >&2
  exit 1
fi
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources/menu_text"
cp "$BUILD/libSnapSettings.dylib" "$APP/Contents/Frameworks/"
cp "$ROOT/macos/assets/AppIcon.icns" "$APP/Contents/Resources/"
cp "$BUILD/Snap64Recomp" "$APP/Contents/MacOS/"
cp "$BUILD/lib/SDL/libSDL2-2.0.0.dylib" "$APP/Contents/Frameworks/"
install_name_tool -delete_rpath "$BUILD/lib/SDL" "$APP/Contents/MacOS/Snap64Recomp"
install_name_tool -change '@rpath/libSnapSettings.dylib' '@executable_path/../Frameworks/libSnapSettings.dylib' "$APP/Contents/MacOS/Snap64Recomp"
install_name_tool -change '@rpath/libSDL2-2.0.0.dylib' '@executable_path/../Frameworks/libSDL2-2.0.0.dylib' "$APP/Contents/MacOS/Snap64Recomp"
cp "$ROOT/menu_text/recomp_logo.png" "$APP/Contents/Resources/menu_text/"
cp "$ROOT/LICENSE" "$ROOT/NOTICE.md" "$APP/Contents/Resources/"
cp -R "$ROOT/licenses" "$APP/Contents/Resources/"
python3 - "$APP" <<'PY'
import plistlib,sys
from pathlib import Path
app=Path(sys.argv[1])
info=dict(CFBundleExecutable='Snap64Recomp',CFBundleIdentifier='local.snap64.macos',CFBundleName='Pokemon Snap',CFBundleDisplayName='Pokémon Snap',CFBundlePackageType='APPL',CFBundleShortVersionString='1.0.0',CFBundleVersion='2',CFBundleIconFile='AppIcon',LSMinimumSystemVersion='14.0',NSHighResolutionCapable=True,SDL_FILESYSTEM_BASE_DIR_TYPE='resource')
(app/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
PY
xattr -cr "$APP"
codesign --force --sign - "$APP/Contents/Frameworks/libSDL2-2.0.0.dylib"
codesign --force --sign - "$APP/Contents/Frameworks/libSnapSettings.dylib"
codesign --force --sign - "$APP"
# Cloud-synced folders may reattach Finder metadata during signing.
xattr -cr "$APP"
codesign --verify --deep --strict "$APP"
DATA="$HOME/Library/Application Support/Snap64 Recomp"
mkdir -p "$DATA"
[[ -f "$DATA/pokemonsnap.z64" ]] || cp "$ROOT/pokemonsnap.z64" "$DATA/pokemonsnap.z64"
echo "Built app: $APP"
if [[ "${1:-}" == --build-only ]]; then exit 0; fi
if pgrep -x Snap64Recomp >/dev/null; then
  echo "App built at $APP. Quit the existing game before launching this copy."
  exit 0
fi
open "$APP"
if [[ "${1:-}" == --verify ]]; then sleep 3; pgrep -x Snap64Recomp; fi
