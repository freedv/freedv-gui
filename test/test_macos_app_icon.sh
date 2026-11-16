#!/bin/bash -e
#
# Checks the icon macOS shows for FreeDV.app on this version of macOS (see
# macos_app_icon.mm):
#
#     test_macos_app_icon.sh <FreeDV.app> [PNG to save what's shown to]
#     test_macos_app_icon.sh --stub [PNG to save what's shown to]
#
# With --stub, it's checked in an app bundle put together here from what the
# build puts in FreeDV.app's (src/info.plist.in, src/freedv.icns and
# src/Assets.car), for checking on versions of macOS that FreeDV isn't built
# on without building it.

HERE="$(cd "$(dirname "$0")" && pwd)"
SOURCE="$HERE/.."
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

clang++ -std=c++17 -fobjc-arc -framework AppKit -framework UniformTypeIdentifiers \
    -o "$WORK/macos_app_icon" "$HERE/macos_app_icon.mm"

APP="$1"
if [ "$APP" = "--stub" ]; then
    APP="$WORK/FreeDV.app"
    mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
    # Info.plist as CMake makes it (see src/CMakeLists.txt), as far as the
    # icon's concerned.
    sed -e 's/\${MACOSX_BUNDLE_ICON_FILE}/freedv/' \
        -e 's/\${MACOSX_BUNDLE_EXECUTABLE_NAME}/FreeDV/' \
        -e 's/\${MACOSX_BUNDLE_GUI_IDENTIFIER}/org.freedv.freedv.icontest/' \
        -e 's/\${MACOSX_BUNDLE_BUNDLE_NAME}/FreeDV/' \
        -e 's/\${[A-Z_]*}/1.0/g' -e 's/@FREEDV_BUNDLE_LOCALIZATIONS@/<string>en<\/string>/' \
        -e 's/<@DARK_MODE_DISABLE@ \/>/<false \/>/' \
        "$SOURCE/src/info.plist.in" > "$APP/Contents/Info.plist"
    plutil -lint "$APP/Contents/Info.plist"
    cp "$SOURCE/src/freedv.icns" "$SOURCE/src/Assets.car" "$APP/Contents/Resources/"
    printf 'int main() { return 0; }\n' | clang -x c -o "$APP/Contents/MacOS/FreeDV" -
fi
if [ -z "$APP" ] || [ ! -d "$APP" ]; then
    echo "usage: $0 <FreeDV.app> | --stub [PNG]"
    exit 2
fi

"$WORK/macos_app_icon" "$APP" $2
