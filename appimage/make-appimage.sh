#!/bin/bash -e

export APPNAME="FreeDV"
export APPEXEC=../build_linux/src/freedv

DESKTOP_FILE="$APPNAME.desktop"
APPDIR="$APPNAME.AppDir"
BUILDDIR="../"
MACH_ARCH=`uname -m`

export NO_STRIP=1

# Change to the directory where this script is located
cd "$(dirname "$(realpath "$0")")"

if [ -d "$APPDIR" ]; then
    echo "Deleting $APPDIR..."
    rm -rf "$APPDIR"
else
    echo "$APPDIR does not exist."
fi

echo "Bundle dependencies..."
if test -f linuxdeploy-${MACH_ARCH}.AppImage; then
  echo "linuxdeploy exists"
else
    wget -c "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh"
    wget https://github.com/linuxdeploy/linuxdeploy/releases/latest/download/linuxdeploy-${MACH_ARCH}.AppImage
    chmod +x linuxdeploy-${MACH_ARCH}.AppImage linuxdeploy-plugin-gtk.sh
fi

# The GTK plugin's AppRun hook forces GDK_BACKEND=x11, overwriting whatever the user
# set. On Wayland desktops that runs the bundled (older) GTK under XWayland, where
# panels flicker and the Settings dialog redraws over itself. Leave the backend to
# GTK instead: Wayland when available, else X11 (and GDK_BACKEND is respected again).
sed -i '/^export GDK_BACKEND=x11/d' linuxdeploy-plugin-gtk.sh

./linuxdeploy-${MACH_ARCH}.AppImage \
  --executable "$APPEXEC" \
  --appdir "$APPDIR" \
  --icon-file ../contrib/freedv256x256.png \
  --custom-apprun "AppRun.sh" \
  --desktop-file $DESKTOP_FILE

# Manually copy over /etc/ssl to APPDIR. Needed for OpenSSL to behave properly on non-Ubuntu
# distros.
mkdir -p "$APPDIR/etc/ssl/certs"
cp -aL /etc/ssl/certs/* "$APPDIR/etc/ssl/certs"

# Create the output
./linuxdeploy-${MACH_ARCH}.AppImage \
  --appdir "$APPDIR" \
  --plugin gtk \
  --output appimage

# Fail rather than ship an AppImage that still forces X11 (e.g. if the plugin changes).
if grep -q "GDK_BACKEND" "$APPDIR"/apprun-hooks/*.sh; then
    echo "AppRun hook still sets GDK_BACKEND"
    exit 1
fi

# Include version number in AppImage filename
FREEDV_VERSION=`cat ../build_linux/freedv-version.txt`
mv ${APPNAME}-${MACH_ARCH}.AppImage ${APPNAME}-$FREEDV_VERSION-${MACH_ARCH}.AppImage

echo "Done"
