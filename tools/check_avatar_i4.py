#!/usr/bin/env python3
"""Verify the generated 4-bit images are real LVGL I4 palettes and their web PNGs match.

Runs with Python's standard library only. Unlike a mere firmware compilation,
this checks pixel index packing, 16-entry BGRA palette layout, and visual
equivalence between the firmware resource and the web preview for all 30 images.
"""
import hashlib
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_avatar_art as artwork


def read_png_rgb(png_bytes, width, height):
    assert png_bytes.startswith(b"\x89PNG\r\n\x1a\n")
    offset = 8
    compressed = bytearray()
    while offset < len(png_bytes):
        length = struct.unpack_from(">I", png_bytes, offset)[0]
        tag = png_bytes[offset + 4:offset + 8]
        payload = png_bytes[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", png_bytes, offset + 8 + length)[0]
        assert zlib.crc32(tag + payload) & 0xFFFFFFFF == crc, "PNG CRC mismatch"
        if tag == b"IHDR":
            assert struct.unpack_from(">II", payload) == (width, height)
            assert payload[8:13] == b"\x08\x02\x00\x00\x00"
        elif tag == b"IDAT":
            compressed.extend(payload)
        elif tag == b"IEND":
            break
        offset += length + 12
    scan = zlib.decompress(bytes(compressed))
    stride = width * 3
    assert len(scan) == height * (stride + 1)
    assert all(scan[i * (stride + 1)] == 0 for i in range(height)), "PNG filters changed"
    return b"".join(scan[i * (stride + 1) + 1:(i + 1) * (stride + 1)] for i in range(height))


def main():
    assert artwork.IMAGE_BYTES == 786
    assert artwork.PALETTE_BYTES == 64
    seen_images = set()
    seen_pngs = set()
    for number, image, png in artwork.art_from_pack():
        assert 1 <= number <= 30
        assert len(image) == artwork.IMAGE_BYTES
        palette = [
            (image[i + 2], image[i + 1], image[i])
            for i in range(0, artwork.PALETTE_BYTES, 4)
        ]
        assert all(image[i + 3] == 255 for i in range(0, artwork.PALETTE_BYTES, 4))
        rgb = bytearray()
        for row in range(artwork.SIZE):
            start = artwork.PALETTE_BYTES + row * artwork.STRIDE
            packed = image[start:start + artwork.STRIDE]
            for x in range(artwork.SIZE):
                idx = (packed[x // 2] >> 4) if x % 2 == 0 else (packed[x // 2] & 15)
                rgb.extend(palette[idx])
        assert bytes(rgb) == read_png_rgb(png, artwork.SIZE, artwork.SIZE), (
            f"Avatar {number}: BGRA palette or index packing differs from web PNG"
        )
        seen_images.add(hashlib.sha256(image).digest())
        seen_pngs.add(hashlib.sha256(png).digest())

    assert len(seen_images) == len(seen_pngs) == 30, "Avatars must have distinct art"
    header = (artwork.ROOT / "firmware/components/ui/src/avatar_art_generated.hpp").read_text()
    assert header.count("LV_COLOR_FORMAT_I4") == 30, "All LVGL descriptors must use I4"
    assert "LV_COLOR_FORMAT_RGB565" not in header, "Old 16-bit images remain"
    print(
        "PASS: 30 unique I4 avatars, BGRA palettes, index packing and matching "
        f"web PNGs; {30 * artwork.IMAGE_BYTES} bytes of LVGL image data"
    )


if __name__ == "__main__":
    main()
