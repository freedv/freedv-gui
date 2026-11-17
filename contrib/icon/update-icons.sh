#!/bin/bash -e
#
# Remakes the icon files that the build uses from what's in this folder:
#
#   FreeDV.icon      The macOS icon, made with Icon Composer. actool (Xcode 26
#                    or later) turns it into src/Assets.car, which macOS 11 and
#                    later show it from (as Liquid Glass on macOS 26), and
#                    src/freedv.icns, for anything that only looks at
#                    CFBundleIconFile.
#   Windows/*.png    The sizes put in contrib/freedv.ico.
#   Linux/*.png      Installed as they are (see contrib/CMakeLists.txt), with
#                    the smaller sizes from Windows/.
#
# The results are kept in the repository so that FreeDV can be built without
# Xcode (actool isn't in the Command Line Tools) and on other platforms. Run
# this on macOS after changing anything here, then check in what changed.

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../src"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

xcrun actool "$HERE/FreeDV.icon" --compile "$WORK" --platform macosx \
    --minimum-deployment-target 11.0 --app-icon FreeDV --include-all-app-icons \
    --output-partial-info-plist "$WORK/partial.plist" --output-format human-readable-text
# src/info.plist.in names the icon in Assets.car as actool says to. (The
# .icns file is renamed for CFBundleIconFile, which has always been freedv.)
[ "$(/usr/libexec/PlistBuddy -c "Print :CFBundleIconName" "$WORK/partial.plist")" = "FreeDV" ] || \
    { echo "actool now names the icon differently:"; cat "$WORK/partial.plist"; exit 1; }
cp "$WORK/Assets.car" "$SRC/Assets.car"
cp "$WORK/FreeDV.icns" "$SRC/freedv.icns"

W="$HERE/Windows"
python3 "$HERE/make_ico.py" "$HERE/../freedv.ico" \
    "$W/16.png" "$W/24.png" "$W/32.png" "$W/48.png" "$W/32@2x.png" "$W/48@2x.png" "$W/256.png"
