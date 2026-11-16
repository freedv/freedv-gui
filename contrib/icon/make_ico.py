#!/usr/bin/env python3
"""Makes a Windows .ico file from PNG images, one per size.

    make_ico.py output.ico image.png...

Each image goes in as it is, as a PNG (which Windows has taken in icons since
Vista). The sizes must be square, 256 pixels across at most -- the most an
icon's directory can say -- and different from one another. Only the Python
standard library is used, so it runs wherever the build does.
"""

import struct
import sys


def png_size(data):
    """Returns (width, height) of the PNG image in data."""
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError("not a PNG image")
    return struct.unpack(">II", data[16:24])


def make_ico(images):
    """Returns the bytes of an .ico file holding the given PNG images, which
    are put in order of size."""
    entries = []
    for data in images:
        width, height = png_size(data)
        if width != height or not 1 <= width <= 256:
            raise ValueError("a %dx%d image can't go in an icon" % (width, height))
        entries.append((width, data))
    entries.sort(key=lambda entry: entry[0])
    sizes = [width for width, _ in entries]
    if len(set(sizes)) != len(sizes):
        raise ValueError("more than one image of the same size: %s" % sizes)

    header = struct.pack("<HHH", 0, 1, len(entries))
    offset = len(header) + 16 * len(entries)
    directory = b""
    for width, data in entries:
        # A width or height of 256 is written as 0; 32 bits per pixel.
        directory += struct.pack("<BBBBHHII", width % 256, width % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return header + directory + b"".join(data for _, data in entries)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    images = []
    for path in sys.argv[2:]:
        with open(path, "rb") as f:
            images.append(f.read())
    with open(sys.argv[1], "wb") as f:
        f.write(make_ico(images))


if __name__ == "__main__":
    main()
