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

# The icon, in each size that's installed outside an AppImage (see
# contrib/CMakeLists.txt). linuxdeploy names them for their files, which have
# to match Icon= in the desktop file.
ICON_ARGS=()
rm -rf icons
for size in 16 24 32 48 64 96 128 256 512; do
    if [ $size -le 48 ]; then ICON_SRC=../contrib/icon/Windows/$size.png; else ICON_SRC=../contrib/icon/Linux/$size.png; fi
    mkdir -p icons/$size
    cp $ICON_SRC icons/$size/freedv.png
    ICON_ARGS+=(--icon-file icons/$size/freedv.png)
done

./linuxdeploy-${MACH_ARCH}.AppImage \
  --executable "$APPEXEC" \
  --appdir "$APPDIR" \
  "${ICON_ARGS[@]}" \
  --custom-apprun "AppRun.sh" \
  --desktop-file $DESKTOP_FILE

# Fail rather than ship an AppImage without its icon in every size.
for size in 16 24 32 48 64 96 128 256 512; do
    cmp icons/$size/freedv.png "$APPDIR/usr/share/icons/hicolor/${size}x${size}/apps/freedv.png" || \
        { echo "The ${size}x${size} icon isn't in the AppImage"; exit 1; }
done

# Copy over translations, if any were built.
if [ -d "$BUILDDIR/build_linux/share/locale" ]; then
    mkdir -p "$APPDIR/usr/share/locale"
    cp -a "$BUILDDIR/build_linux/share/locale/." "$APPDIR/usr/share/locale/"
fi

# Manually copy over /etc/ssl to APPDIR. Needed for OpenSSL to behave properly on non-Ubuntu
# distros.
mkdir -p "$APPDIR/etc/ssl/certs"
cp -aL /etc/ssl/certs/* "$APPDIR/etc/ssl/certs"

# Create the output
./linuxdeploy-${MACH_ARCH}.AppImage \
  --appdir "$APPDIR" \
  --plugin gtk \
  --output appimage

# Likewise without the icon shown for the AppImage itself (appimagetool makes
# .DirIcon, so this can only be checked now).
[ -e "$APPDIR/.DirIcon" ] || { echo "The AppImage has no .DirIcon"; exit 1; }

# Fail rather than ship an AppImage that still forces X11 (e.g. if the plugin changes).
if grep -q "GDK_BACKEND" "$APPDIR"/apprun-hooks/*.sh; then
    echo "AppRun hook still sets GDK_BACKEND"
    exit 1
fi

# Include version number in AppImage filename
FREEDV_VERSION=`cat ../build_linux/freedv-version.txt`
mv ${APPNAME}-${MACH_ARCH}.AppImage ${APPNAME}-$FREEDV_VERSION-${MACH_ARCH}.AppImage

echo "Done"
