"""Offline TU102 ROM/BIT/FWSEC parser; never accesses hardware."""

import argparse
import hashlib
import json
import struct
from pathlib import Path


def bounded(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(
            f"truncated input: offset={offset:#x} size={size:#x} available={len(data):#x}"
        )
    return data[offset : offset + size]


def u16(data, offset):
    return struct.unpack("<H", bounded(data, offset, 2))[0]


def u32(data, offset):
    return struct.unpack("<I", bounded(data, offset, 4))[0]


def parse(rom):
    if not 512 <= len(rom) <= 1048576:
        raise ValueError("expected bounded ROM capture")
    images = []
    pos = 0
    for _ in range(8):
        if u16(rom, pos) not in (0xAA55, 0x4E56, 0xBB77):
            raise ValueError(f"invalid ROM signature at {pos:#x}")
        pcir = pos + u16(rom, pos + 0x18)
        if bounded(rom, pcir, 4) not in (b"PCIR", b"NPDS", b"RGIS"):
            raise ValueError("invalid image descriptor")
        size = u16(rom, pcir + 0x10) * 512
        kind = bounded(rom, pcir + 0x14, 1)[0]
        last = bounded(rom, pcir + 0x15, 1)[0] & 0x80
        npde = (pcir + u16(rom, pcir + 0xA) + 15) & ~15
        if rom[npde : npde + 4] == b"NPDE" and u16(rom, npde + 4) in (0x100, 0x101):
            length = u16(rom, npde + 6)
            if length < 10:
                raise ValueError("short NPDE")
            bounded(rom, npde, length)
            sub = u16(rom, npde + 8) * 512
            last = rom[npde + 10] & 0x80 if length > 10 else (0 if sub < size else last)
            size = sub
        if size == 0:
            raise ValueError("empty image")
        bounded(rom, pos, size)
        images.append({"offset": pos, "size": size, "type": kind, "last": bool(last)})
        if last:
            break
        pos += size
    else:
        raise ValueError("unterminated image chain")
    base = next((i["size"] for i in images if i["type"] == 0), 0)
    extension = next((i["offset"] for i in images if i["type"] == 0xE0), 0)
    adjust = extension - base if extension and base else 0
    if adjust < 0:
        raise ValueError("invalid expansion adjustment")
    bit = rom.find(b"\xff\xb8BIT\x00")
    if bit < 0:
        raise ValueError("missing BIT")
    hsize, stride, count = bounded(rom, bit + 8, 3)
    if hsize < 12 or stride < 6 or sum(bounded(rom, bit, hsize)) % 256:
        raise ValueError("invalid BIT header/checksum")
    tokens = bounded(rom, bit + hsize, stride * count)
    matches = []
    for i in range(count):
        token = tokens[i * stride : (i + 1) * stride]
        if token[0:2] != b"p\x02" or u16(token, 2) < 4:
            continue
        ptr = u32(token, 4) if stride >= 8 else u16(token, 4)
        table = adjust + u32(rom, ptr)
        version, header, entry_size, entries = bounded(rom, table, 4)
        if version != 1 or header < 6 or entry_size < 6:
            raise ValueError("unsupported ucode table")
        bounded(rom, table + header, entries * entry_size)
        for j in range(entries):
            at = table + header + j * entry_size
            if rom[at] == 0x85:
                matches.append(adjust + u32(rom, at + 2))
    if len(matches) != 1:
        raise ValueError(f"expected one FWSEC entry, got {len(matches)}")
    off = matches[0]
    fields = struct.unpack("<15I", bounded(rom, off, 60))
    desc_size = fields[0] >> 16
    if ((fields[0] >> 8) & 255) != 2 or not fields[0] & 1 or desc_size < 60:
        raise ValueError("requires Turing-family v2 descriptor")
    if not fields[1] or fields[1] != fields[2] or fields[3]:
        raise ValueError("unsupported compressed/entry layout")
    blob = bounded(rom, off + desc_size, fields[1])
    bounded(blob, 0, fields[6])
    dmem = bounded(blob, fields[10], fields[12])
    version, header, stride, count = bounded(dmem, fields[4], 4)
    if (version, header, stride) != (1, 4, 8) or not 1 <= count <= 32:
        raise ValueError("unsupported FWSEC interface")
    table = fields[4] + header
    bounded(dmem, table, count * stride)
    mapper_offsets = [
        u32(dmem, table + i * stride + 4)
        for i in range(count)
        if u32(dmem, table + i * stride) == 4
    ]
    if len(mapper_offsets) != 1:
        raise ValueError("ambiguous mapper")
    mapper = mapper_offsets[0]
    words = struct.unpack("<16I", bounded(dmem, mapper, 64))
    if words[:2] != (0x50414D44, 0x00400003):
        raise ValueError("unsupported mapper")
    cmd, size = words[2:4]
    if size < 48 or cmd % 4:
        raise ValueError("invalid command buffer")
    bounded(dmem, cmd, size)
    if cmd < mapper + 64 and mapper < cmd + size:
        raise ValueError("command overlaps mapper")
    return {
        "rom_sha256": hashlib.sha256(rom).hexdigest(),
        "images": images,
        "expansion_adjust": adjust,
        "fwsec_descriptor_offset": off,
        "fwsec_descriptor_words": fields,
        "fwsec_image_offset": off + desc_size,
        "fwsec_bytes": len(blob),
        "fwsec_sha256": hashlib.sha256(blob).hexdigest(),
        "fwsec_interface_word": u32(dmem, fields[4]),
        "fwsec_interface_entries": u32(dmem, fields[4]) >> 24,
        "mapper_offset": mapper,
        "command_offset": cmd,
        "command_size": size,
        "hardware_validated": False,
        "execution_allowed": False,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("rom", type=Path)
    p.add_argument(
        "--extract", type=Path, help="write native unmodified FWSEC bytes to a NEW file"
    )
    args = p.parse_args()
    data = args.rom.read_bytes()
    result = parse(data)
    if args.extract:
        with args.extract.open("xb") as f:
            f.write(bounded(data, result["fwsec_image_offset"], result["fwsec_bytes"]))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
