"""Reading FreeDV's icon files (contrib/icon) with nothing but the Python
standard library, for test_icon_files.py and the GUI tests: PNG images, and
the images in .ico and .icns files.

What's checked of an image is what makes it FreeDV's icon, in any of its
shapes (a circle on Windows and Linux, a rounded square on macOS) and at any
size: see looks_like_freedv_icon()."""

import struct
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class Image:
    """An image as rows of (red, green, blue, alpha) pixels."""

    def __init__(self, width, height, rows):
        self.width = width
        self.height = height
        self.rows = rows

    def pixel(self, x, y):
        return self.rows[y][x]


def png_size(data):
    """(width, height) of the PNG image in data."""
    if data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR":
        raise ValueError("not a PNG image")
    return struct.unpack(">II", data[16:24])


def read_png(data):
    """Decodes a PNG image (8 bits a sample, not interlaced; any of the colour
    types without a palette) into an Image."""
    if data[:8] != PNG_SIGNATURE:
        raise ValueError("not a PNG image")
    position = 8
    compressed = b""
    header = None
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    width, height, depth, colour_type, _, _, interlace = header
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colour_type)
    if depth != 8 or channels is None or interlace != 0:
        raise ValueError("unsupported PNG: depth %d, colour type %d, interlace %d" % (depth, colour_type, interlace))

    raw = zlib.decompress(compressed)
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    position = 0
    for _ in range(height):
        kind = raw[position]
        line = bytearray(raw[position + 1:position + 1 + stride])
        position += 1 + stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            up_left = previous[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + left) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + up) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif kind == 4:
                estimate = left + up - up_left
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - up_left))
                nearest = (left, up, up_left)[distances.index(min(distances))]
                line[i] = (line[i] + nearest) & 0xFF
        previous = line
        row = []
        for x in range(width):
            sample = line[x * channels:(x + 1) * channels]
            if channels == 1:
                row.append((sample[0], sample[0], sample[0], 255))
            elif channels == 2:
                row.append((sample[0], sample[0], sample[0], sample[1]))
            elif channels == 3:
                row.append((sample[0], sample[1], sample[2], 255))
            else:
                row.append(tuple(sample))
        rows.append(row)
    return Image(width, height, rows)


def ico_images(data):
    """The images in an .ico file, as {size: image data}. Each is as it is in
    the file: a PNG image, or (as icons were before Windows Vista) a bitmap."""
    reserved, kind, count = struct.unpack("<HHH", data[:6])
    if reserved != 0 or kind != 1:
        raise ValueError("not an icon file")
    images = {}
    for index in range(count):
        width, _, _, _, _, _, size, offset = struct.unpack("<BBBBHHII", data[6 + 16 * index:22 + 16 * index])
        images[width or 256] = data[offset:offset + size]
    return images


def icns_images(data):
    """The PNG images in a macOS .icns file, as {(type, pixels across): PNG data}."""
    if data[:4] != b"icns" or struct.unpack(">I", data[4:8])[0] != len(data):
        raise ValueError("not an icns file")
    images = {}
    position = 8
    while position < len(data):
        kind, length = struct.unpack(">4sI", data[position:position + 8])
        body = data[position + 8:position + length]
        if body[:8] == PNG_SIGNATURE:
            images[(kind.decode("ascii"), png_size(body)[0])] = body
        position += length
    return images


def is_blue(pixel):
    red, green, blue, alpha = pixel
    return alpha >= 192 and blue >= 128 and blue > red + 40 and blue >= green


def looks_like_freedv_icon(pixel_at, width, height, transparent_corner=True):
    """Why the image (pixel_at(x, y) giving its (r, g, b, a)) isn't FreeDV's
    icon, or None if it looks like it is: blue all across the middle (the
    background, and the microphone in front of it), and with nothing in the
    corner, as the icon is round or has rounded corners."""
    for name, x, y in [("middle", width // 2, height // 2), ("left of the middle", width // 4, height // 2),
                       ("above the middle", width // 2, height // 4)]:
        if not is_blue(pixel_at(x, y)):
            return "the %s is %s, not blue" % (name, pixel_at(x, y))
    if transparent_corner and pixel_at(0, 0)[3] > 32:
        return "the corner is %s, not transparent" % (pixel_at(0, 0),)
    return None
