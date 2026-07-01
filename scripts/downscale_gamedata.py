#!/usr/bin/env python3
"""
Downscale gamedata textures for low-memory ARM devices.

Copies the gamedata/ directory, resizing TGA textures (and VW2D inside
models.pack) by a given factor. UI textures (menu/, game/, loading/, lang/)
are excluded from resizing.

Requirements: Python 3.6+, Pillow (pip install Pillow)

Usage:
    python3 downscale_gamedata.py \
        --src /path/to/gamedata/ \
        --dst /path/to/gamedata-lowres/ \
        --factor 0.5 \
        --exclude menu,game,loading,lang
"""

import argparse
import os
import shutil
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("ERROR: Pillow is required. Install with: pip install Pillow")


def parse_args():
    parser = argparse.ArgumentParser(description="Downscale gamedata textures for ARM")
    parser.add_argument("--src", required=True, help="Source gamedata directory")
    parser.add_argument("--dst", required=True, help="Destination directory for downscaled data")
    parser.add_argument("--factor", type=float, default=0.5, help="Scale factor (0.5 = half)")
    parser.add_argument("--exclude", default="menu,game,loading,lang",
                        help="Comma-separated top-level dirs to exclude from resizing")
    return parser.parse_args()


def should_exclude(rel_path: str, exclude_dirs: list) -> bool:
    """Check if a file path starts with any excluded directory."""
    parts = Path(rel_path).parts
    if not parts:
        return True
    top_dir = parts[0]
    if top_dir in exclude_dirs:
        return True
    # Also exclude lang/*/menu/* and lang/*/game/* patterns
    if len(parts) >= 3 and parts[0] == "lang" and parts[2] in ("menu", "game"):
        return True
    return False


def resize_tga(src_path: str, dst_path: str, factor: float) -> bool:
    """Resize a TGA file by the given factor. Returns True on success."""
    try:
        img = Image.open(src_path)
        new_w = max(1, int(img.width * factor))
        new_h = max(1, int(img.height * factor))
        if new_w == img.width and new_h == img.height:
            shutil.copy2(src_path, dst_path)
            return True
        img_resized = img.resize((new_w, new_h), Image.BILINEAR)
        img_resized.save(dst_path)
        return True
    except Exception as e:
        print(f"  WARNING: Failed to resize {src_path}: {e}", file=sys.stderr)
        shutil.copy2(src_path, dst_path)
        return False


# --- VFS v1.6 parser for models.pack ---

VFS_HEADER_SIZE = 16  # 'VFS_' (4) + version (4) + build_number (4) + file_table_offset (4)


def parse_vfs(data: bytes) -> tuple:
    """
    Parse a VFS v1.6 file into its components.
    Returns (build_number, entries) where entries is a list of (name, file_data).
    """
    if len(data) < VFS_HEADER_SIZE:
        raise ValueError("VFS file too small")

    sign = data[0:4]
    if sign != b"VFS_":
        raise ValueError(f"Invalid VFS signature: {sign}")

    version = data[4:8]
    if version != b"v1.6":
        raise ValueError(f"Unsupported VFS version: {version}")

    build_number = struct.unpack_from("<I", data, 8)[0]
    file_table_offset = struct.unpack_from("<I", data, 12)[0]

    entries = []
    pos = file_table_offset

    while pos < len(data):
        if pos + 2 > len(data):
            break
        name_size = struct.unpack_from("<H", data, pos)[0]
        pos += 2

        if pos + name_size > len(data):
            break
        name = data[pos:pos + name_size].decode("utf-8", errors="replace")
        pos += name_size

        if pos + 8 > len(data):
            break
        offset, size = struct.unpack_from("<II", data, pos)
        pos += 8

        file_data = data[offset:offset + size]
        entries.append((name, file_data))

    return build_number, entries


def build_vfs(build_number: int, entries: list) -> bytes:
    """
    Build a VFS v1.6 binary from a list of (name, file_data) entries.
    """
    # Calculate data section size
    data_offset_start = VFS_HEADER_SIZE
    current_offset = data_offset_start

    offsets = []
    for name, file_data in entries:
        offsets.append(current_offset)
        current_offset += len(file_data)

    file_table_offset = current_offset

    # Build the binary
    buf = bytearray()
    # Header
    buf += b"VFS_"
    buf += b"v1.6"
    buf += struct.pack("<I", build_number)
    buf += struct.pack("<I", file_table_offset)

    # Data section
    for name, file_data in entries:
        buf += file_data

    # File table
    for i, (name, file_data) in enumerate(entries):
        name_bytes = name.encode("utf-8")
        buf += struct.pack("<H", len(name_bytes))
        buf += name_bytes
        buf += struct.pack("<II", offsets[i], len(file_data))

    return bytes(buf)


def resize_vw2d(data: bytes, factor: float) -> bytes:
    """Resize a VW2D texture by the given factor."""
    if len(data) < 16 or data[0:4] != b"VW2D":
        return data  # not a valid VW2D, return unchanged

    width, height, channels = struct.unpack_from("<iii", data, 4)

    expected_size = 16 + width * height * channels
    if len(data) < expected_size:
        return data  # corrupted, return unchanged

    new_w = max(1, int(width * factor))
    new_h = max(1, int(height * factor))

    if new_w == width and new_h == height:
        return data

    pixels = data[16:16 + width * height * channels]

    mode = "RGBA" if channels == 4 else "RGB"
    try:
        img = Image.frombytes(mode, (width, height), pixels)
        img_resized = img.resize((new_w, new_h), Image.BILINEAR)
        header = b"VW2D" + struct.pack("<iii", new_w, new_h, channels)
        return header + img_resized.tobytes()
    except Exception as e:
        print(f"  WARNING: Failed to resize VW2D ({width}x{height}x{channels}): {e}",
              file=sys.stderr)
        return data


def process_models_pack(src_path: str, dst_path: str, factor: float) -> bool:
    """Parse models.pack, resize VW2D entries, and write a new pack."""
    print(f"  Processing models.pack...")

    with open(src_path, "rb") as f:
        data = f.read()

    try:
        build_number, entries = parse_vfs(data)
    except ValueError as e:
        print(f"  WARNING: Cannot parse models.pack: {e}", file=sys.stderr)
        shutil.copy2(src_path, dst_path)
        return False

    resized_count = 0
    new_entries = []
    for name, file_data in entries:
        if name.endswith(".vw2d"):
            new_data = resize_vw2d(file_data, factor)
            if new_data != file_data:
                resized_count += 1
            new_entries.append((name, new_data))
        else:
            new_entries.append((name, file_data))

    print(f"  Resized {resized_count}/{len(entries)} entries in models.pack")

    new_vfs = build_vfs(build_number, new_entries)
    with open(dst_path, "wb") as f:
        f.write(new_vfs)

    return True


def main():
    args = parse_args()

    src_dir = Path(args.src).resolve()
    dst_dir = Path(args.dst).resolve()
    factor = args.factor
    exclude_dirs = [d.strip() for d in args.exclude.split(",") if d.strip()]

    if not src_dir.is_dir():
        sys.exit(f"ERROR: Source directory not found: {src_dir}")

    if factor <= 0 or factor > 1:
        sys.exit(f"ERROR: Factor must be between 0 (exclusive) and 1 (inclusive)")

    print(f"Downscaling gamedata: {src_dir} -> {dst_dir}")
    print(f"  Factor: {factor}")
    print(f"  Excluding: {exclude_dirs}")

    # Remove destination if it exists, then copy everything
    if dst_dir.exists():
        shutil.rmtree(dst_dir)
    shutil.copytree(src_dir, dst_dir)

    # Now walk the destination and resize TGA files (and models.pack)
    tga_resized = 0
    tga_skipped = 0

    for root, dirs, files in os.walk(dst_dir):
        for filename in files:
            filepath = Path(root) / filename
            rel_path = str(filepath.relative_to(dst_dir))

            # Handle models.pack specially
            if filename == "models.pack":
                process_models_pack(str(filepath), str(filepath), factor)
                continue

            # Only process TGA files
            if not filename.lower().endswith(".tga"):
                continue

            # Check exclusions
            if should_exclude(rel_path, exclude_dirs):
                tga_skipped += 1
                continue

            # Resize in-place (already copied)
            if resize_tga(str(filepath), str(filepath), factor):
                tga_resized += 1

    print(f"\nDone!")
    print(f"  TGA resized: {tga_resized}")
    print(f"  TGA skipped (UI): {tga_skipped}")
    print(f"  Output: {dst_dir}")


if __name__ == "__main__":
    main()
