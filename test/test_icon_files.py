#!/usr/bin/env python3
"""Checks the icon files the build uses (see contrib/icon/update-icons.sh):
that each holds FreeDV's icon, in the sizes each platform looks for, and that
they've been remade from what's in contrib/icon since it last changed.

    test_icon_files.py [source folder]
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from icon_images import ico_images, icns_images, looks_like_freedv_icon, png_size, read_png  # noqa: E402

SOURCE = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, ".."))
ICON = os.path.join(SOURCE, "contrib", "icon")

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
    return condition


def read(*path):
    with open(os.path.join(SOURCE, *path), "rb") as f:
        return f.read()


def check_image(data, what, transparent_corner=True):
    image = read_png(data)
    problem = looks_like_freedv_icon(image.pixel, image.width, image.height, transparent_corner)
    check(problem is None, "%s doesn't look like FreeDV's icon: %s" % (what, problem))


# The images drawn for Windows and Linux: each the size its name says, and
# FreeDV's icon (a blue circle).
for folder, names in [("Windows", ["16", "24", "32", "48", "256", "16@2x", "24@2x", "32@2x", "48@2x", "256@2x"]),
                      ("Linux", ["64", "96", "128", "256", "512"])]:
    for name in names:
        path = os.path.join("contrib", "icon", folder, name + ".png")
        if not check(os.path.exists(os.path.join(SOURCE, path)), "%s is missing" % path):
            continue
        data = read(path)
        size = int(name.split("@")[0]) * (2 if name.endswith("@2x") else 1)
        check(png_size(data) == (size, size), "%s is %s, not %dx%d" % (path, png_size(data), size, size))
        check_image(data, path)

# Windows: freedv.ico has the images that update-icons.sh puts in it, as they
# are now.
with open(os.path.join(ICON, "update-icons.sh")) as f:
    script = f.read()
ico_line = re.search(r'make_ico\.py"[^\n]*\\\n([^\n]*)', script)
ico_sources = re.findall(r'"\$W/([^"]+)"', ico_line.group(1)) if ico_line else []
check(ico_sources, "Can't find the images update-icons.sh puts in freedv.ico")
icons = ico_images(read("contrib", "freedv.ico"))
expected = {}
for name in ico_sources:
    data = read("contrib", "icon", "Windows", name)
    expected[png_size(data)[0]] = data
check(sorted(icons) == sorted(expected),
      "freedv.ico has the sizes %s, not %s" % (sorted(icons), sorted(expected)))
for size, data in sorted(icons.items()):
    if not check(expected.get(size) == data,
                 "freedv.ico's %dx%d image isn't contrib/icon/Windows's: run contrib/icon/update-icons.sh" % (size, size)):
        continue
    check(png_size(data) == (size, size), "freedv.ico's %dx%d image is %s" % (size, size, png_size(data)))
    check_image(data, "freedv.ico's %dx%d image" % (size, size))
rc = read("contrib", "freedv.rc.in").decode()
check(re.search(r'^freedv\s+ICON\s+"@CMAKE_SOURCE_DIR@/contrib/freedv\.ico"', rc, re.M),
      "freedv.rc.in doesn't put freedv.ico in the resource \"freedv\" (which src/gui/util/AppIcon.cpp loads)")

# macOS: freedv.icns (for whatever reads CFBundleIconFile) and Assets.car (for
# CFBundleIconName, which macOS 11 and later go by), both from FreeDV.icon.
icns = icns_images(read("src", "freedv.icns"))
sizes = sorted(set(size for _, size in icns))
for size in [32, 128, 256]:
    check(size in sizes, "freedv.icns has no %dx%d image (it has %s)" % (size, size, sizes))
for (kind, size), data in sorted(icns.items()):
    check_image(data, "freedv.icns's %s image (%dx%d)" % (kind, size, size))
car = read("src", "Assets.car")
check(car[:8] == b"BOMStore", "src/Assets.car isn't a compiled asset catalog")
check(b"FreeDV" in car, "src/Assets.car has nothing called FreeDV in it")
plist = read("src", "info.plist.in").decode()
check(re.search(r"<key>CFBundleIconName</key>\s*<string>FreeDV</string>", plist),
      "info.plist.in doesn't name the icon in Assets.car (CFBundleIconName FreeDV)")
check(re.search(r"<key>CFBundleIconFile</key>\s*<string>\$\{MACOSX_BUNDLE_ICON_FILE\}</string>", plist),
      "info.plist.in doesn't name freedv.icns (CFBundleIconFile)")
cmake = read("src", "CMakeLists.txt").decode()
check(re.search(r'MACOSX_BUNDLE_ICON_FILE "freedv"', cmake), "src/CMakeLists.txt doesn't call the icon file freedv")
check(re.search(r"set\(RES_FILES freedv\.icns Assets\.car\)", cmake),
      "src/CMakeLists.txt doesn't put freedv.icns and Assets.car in the app bundle")

# Linux: the desktop files name the icon that's installed (as freedv.png).
for path in [("contrib", "freedv.desktop"), ("appimage", "FreeDV.desktop")]:
    check(re.search(r"^Icon=freedv$", read(*path).decode(), re.M), "%s doesn't have Icon=freedv" % os.path.join(*path))

if failures:
    for failure in failures:
        print("FAILED: " + failure)
    sys.exit(1)
print("Icon files OK")
