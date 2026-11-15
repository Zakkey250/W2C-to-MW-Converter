"""Restore original MW toll objects from stock local MW banks."""
from collections import Counter, defaultdict
import hashlib
import json
import math
import mmap
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "source-world"))
from source_ir import chunks


# Spawner asset -> intact geometry, stock Attrib collection.
TOLLS = {
    0x2528FE86: (0xAFE2D563, 0x2971FF64),
    0x7BC807F9: (0xE25C4C5E, 0x5AEFC6A8),
    0xAA27F4C6: (0xF2FBEBC0, 0x5AEFC6A8),
    0xB6D45FE1: (0x27042FE4, 0x2971FF64),
    0xBA0A9F07: (0xE4614A67, 0x1220B451),
}
# These MW booth bodies carry the default group's hide bit although their
# matching arms, lights and stop signs remain in the converted road layout.
BOOTHS_WITH_VISIBLE_LANES = {
    (424, 36), (1636, 4), (1636, 5), (1636, 6), (1833, 51), (1833, 52),
    (1836, 30), (1836, 31), (1836, 32),
}


def chunk(tag, data):
    return struct.pack("<II", tag, len(data)) + data


def word(buf, offset):
    return struct.unpack_from("<I", buf, offset)[0]


def aligned(offset, n):
    return (offset + n - 1) & -n


def pad_chunk(data, absolute, boundary):
    padding = (absolute - len(data)) % boundary
    if padding:
        if padding < 8:
            padding += boundary
        data.extend(chunk(0, bytes(padding - 8)))


def original_placements(stream, rows, *, restore_hidden_booths=False):
    placements = defaultdict(list)
    spawners = []
    for kind, _, start, end in rows:
        if kind == 0x34027:
            section, _, count = struct.unpack_from("<HHI", stream, start + 16)
            if stream[start:start + 16] != b"\x11" * 8 + struct.pack("<II", 11, 11) or end - start != 24 + 64 * count:
                raise ValueError("MW toll spawner envelope")
            for index in range(count):
                at = start + 24 + index * 64
                asset, secondary, param = struct.unpack_from("<III", stream, at + 32)
                if asset not in TOLLS:
                    continue
                if (secondary, param) != (asset, TOLLS[asset][1]):
                    raise ValueError("MW toll stock asset/parameter closure")
                spawners.append((section, index, asset, stream[at:at + 64]))
        if kind != 0x80034100:
            continue
        children = {tag: (a, b) for tag, _, a, b in chunks(stream, start, end)}
        if not {0x34101, 0x34103, 0x34106} <= children.keys():
            continue
        section = word(stream, children[0x34101][0] + 12)
        begin, end = children[0x34103]
        begin = aligned(begin, 16)
        if (end - begin) % 64:
            raise ValueError("MW toll placement stride")
        a, b = children[0x34106]
        for guid, index in struct.iter_unpack("<HH", stream[a:b]):
            at = begin + index * 64
            if at >= end:
                continue
            position = struct.unpack_from("<3f", stream, at + 32)
            placements[section, guid].append((position, word(stream, at + 24), index))
    result = []
    expected_booths = BOOTHS_WITH_VISIBLE_LANES if restore_hidden_booths else set()
    recovered = set()
    for section, ordinal, asset, record in spawners:
        guid = struct.unpack_from("<H", record, 44)[0]
        x, y, z = struct.unpack_from("<3f", record, 16)
        matches = [(flags, index) for pos, flags, index in placements[section, guid]
                   if math.dist(pos, (z, -x, y)) < 0.04]
        if len(matches) != 1:
            raise ValueError(f"Unresolved MW toll GUID: {section}:{guid:04x}")
        flags, index = matches[0]
        identity = (section, index)
        if identity in expected_booths:
            if asset not in (0x7BC807F9, 0xAA27F4C6) or flags & 0x3A != 0x20:
                raise ValueError("MW toll booth visibility differs from the verified source")
            recovered.add(identity)
        if not flags & 0x3a or identity in recovered:
            result.append((section, ordinal, index, asset, record))
    if recovered != expected_booths or len(result) != 293 + len(expected_booths) or len({r[4] for r in result}) != len(result):
        raise ValueError("MW active toll placement inventory changed")
    return result


def stock_trees(master):
    roots = {model for model, _ in TOLLS.values()}
    trees = {}
    collisions = set()
    for tag, _, start, end in chunks(master):
        if tag == 0x8003410b:
            for kind, _, a, b in chunks(master, start, end):
                if kind == 0x3410c and word(master, a) in roots:
                    key, count = struct.unpack_from("<II", master, a)
                    if key in trees or not 1 <= count <= 32 or b - a != 8 + 16 * count:
                        raise ValueError("MW toll tree envelope")
                    trees[key] = master[a:b]
        if tag == 0x8003b900:
            collisions.update(word(master, aligned(a, 16)) for kind, _, a, _ in chunks(master, start, end) if kind == 0x3b901)
    if set(TOLLS) - collisions or set(trees) != {0xE25C4C5E, 0xE4614A67, 0xF2FBEBC0}:
        raise ValueError("MW toll collision/hierarchy closure")
    for key in roots - trees.keys():
        # The stock light and sign each use one intact mesh.
        trees[key] = struct.pack("<6I", key, 1, 0x9756DF79, key, 0, 0xFF00)
    return {asset: trees[key] for asset, (key, _) in TOLLS.items()}


def model_bank(stream, rows, trees):
    needed = {word(tree, 12 + i * 16) for tree in trees.values() for i in range(word(tree, 4))}
    found, template = native_models(stream, rows, needed)
    textures = set().union(*(value[2] for value in found.values()))
    if found.keys() != needed or len(found) != 23 or len(textures) != 5:
        raise ValueError("MW toll native geometry/material closure")
    return write_native_models(found, template, b"W2CMW_MW_TOLLS"), textures


def native_models(stream, rows, needed, *, skip_ambiguous=False):
    found = {}
    ambiguous = set()
    template = None
    for tag, _, start, end in rows:
        if tag != 0x80134000:
            continue
        for kind, offset, a, b in chunks(stream, start, end):
            if kind == 0x80134001 and template is None:
                template = {k: stream[x:y] for k, _, x, y in chunks(stream, a, b)}
            if kind != 0x80134010:
                continue
            parts = {k: (x, y) for k, _, x, y in chunks(stream, a, b)}
            key = word(stream, aligned(parts[0x134011][0], 16) + 16)
            if key not in needed:
                continue
            if key in ambiguous:
                continue
            x, y = parts[0x134012]
            payload = stream[offset:b]
            if key in found:
                if found[key][1] == payload:
                    continue
                if not skip_ambiguous:
                    raise ValueError("Conflicting MW native model definitions")
                del found[key]
                ambiguous.add(key)
                continue
            found[key] = (offset, payload, {h for h, _ in struct.iter_unpack("<II", stream[x:y])})
    return found, template


def write_native_models(found, template, name):
    info = bytearray(template[0x134002])
    struct.pack_into("<I", info, 12, len(found))
    info[16:72] = name.ljust(56, b"\0")
    info[72:104] = b"W2CMW_V2".ljust(32, b"\0")
    header = chunk(0x134002, info) + chunk(0x134003, b"".join(struct.pack("<II", key, 0) for key in sorted(found)))
    bank = bytearray(bytes(8) + chunk(0x80134001, header + chunk(0x134004, b"")))
    for key in sorted(found):
        offset, payload, _ = found[key]
        pad_chunk(bank, offset, 128)
        bank.extend(payload)
    return chunk(0x80134000, bank[8:])


def texture_bank(stream, rows, needed):
    selected, template = native_textures(stream, rows, needed)
    if selected.keys() != needed:
        raise ValueError("Incomplete MW toll texture bank")
    return write_native_textures(selected, template, b"W2CMW_MW_TOLLS")


def native_textures(stream, rows, needed, *, skip_ambiguous=False):
    selected = {}
    ambiguous = set()
    template = None
    for tag, _, start, end in rows:
        if tag != 0xb3300000:
            continue
        info, formats, definitions = b"", b"", b""
        pixel_start = pixel_end = 0
        for kind, _, a, b in chunks(stream, start, end):
            if kind == 0xb3310000:
                head = {k: stream[x:y] for k, _, x, y in chunks(stream, a, b)
                        if k in (0x33310001, 0x33310004, 0x33310005)}
                info, definitions, formats = (head.get(k, b"") for k in (0x33310001, 0x33310004, 0x33310005))
            if kind == 0xb3320000:
                for k, _, x, y in chunks(stream, a, b):
                    if k == 0x33320002:
                        pixel_start, pixel_end = aligned(x, 128), y
        if not pixel_start or len(definitions) % 124 or len(formats) != len(definitions) // 124 * 32:
            continue
        for i in range(len(definitions) // 124):
            desc = definitions[i * 124:(i + 1) * 124]
            key = word(desc, 36)
            if key not in needed or key in ambiguous:
                continue
            offset, size = word(desc, 48), word(desc, 56)
            a = pixel_start + offset
            if not size or a + size > pixel_end or word(desc, 60):
                if skip_ambiguous:
                    ambiguous.add(key)
                    selected.pop(key, None)
                    continue
                raise ValueError("MW toll texture range/palette")
            row = (desc, formats[i * 32:(i + 1) * 32], stream[a:a + size])
            if key in selected and (selected[key][0][56:], *selected[key][1:]) != (desc[56:], *row[1:]):
                if skip_ambiguous:
                    ambiguous.add(key)
                    del selected[key]
                    continue
                raise ValueError("Conflicting MW toll texture definitions")
            selected.setdefault(key, row)
            template = info
    return selected, template


def write_native_textures(selected, template, name):
    pack_key = 0xFFFFFFFF
    for c in name:
        pack_key = (pack_key * 33 + c) & 0xFFFFFFFF
    info = bytearray(template)
    info[4:32] = name.ljust(28, b"\0")
    info[32:96] = (name + b".tpk").ljust(64, b"\0")
    struct.pack_into("<I", info, 96, pack_key)
    descriptors, formats, pixels = bytearray(), bytearray(), bytearray()
    for key, (original, fmt, image) in sorted(selected.items()):
        desc = bytearray(original)
        desc[:12] = bytes(12)
        struct.pack_into("<III", desc, 44, 0, len(pixels), 0xFFFFFFFF)
        descriptors.extend(desc)
        formats.extend(fmt)
        pixels.extend(image)
        pixels.extend(bytes(-len(pixels) % 128))
    header = (chunk(0x33310001, info) + chunk(0x33310002, b"".join(struct.pack("<II", k, 0) for k in sorted(selected)))
              + chunk(0x33310004, descriptors) + chunk(0x33310005, formats))
    bank = bytearray(bytes(8) + chunk(0, bytes(48)) + chunk(0xb3310000, header))
    pad_chunk(bank, 0, 128)
    body = bytearray(chunk(0x33320001, struct.pack("<6I", 0, 0, 1, pack_key, 0, 0)))
    base = len(bank) + 8
    # The native resource reader expects the pixel chunk header itself on a
    # 128-byte boundary, as well as the aligned beginning of its contents.
    pad_chunk(body, -base, 128)
    body.extend(chunk(0x33320002, bytes(-(base + len(body) + 8) % 128) + pixels))
    bank.extend(chunk(0xb3320000, body))
    return chunk(0xb3300000, bank[8:])


def compile_tolls(master, stream, output, *, restore_hidden_booths=False):
    master, stream, output = map(Path, (master, stream, output))
    if output.exists():
        raise FileExistsError(output)
    raw = master.read_bytes()
    trees = stock_trees(raw)
    with stream.open("rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        rows = list(chunks(data))
        placement = original_placements(data, rows, restore_hidden_booths=restore_hidden_booths)
        model, wanted = model_bank(data, rows, trees)
        textures = texture_bank(data, rows, wanted)
    # Runtime uses MW's stock L2RA collision collections and Attrib data.
    records = b"".join(r[4] for r in placement)
    payload = (struct.pack("<8sIIII", b"W2TOLL1\0", len(placement), len(trees), len(model), 0)
               + records + b"".join(struct.pack("<II", asset, len(tree)) + tree for asset, tree in sorted(trees.items())) + model)
    output.mkdir(parents=True)
    files = {"mw-tolls.bin": payload, "mw-toll-textures.bin": textures}
    for name, data in files.items():
        (output / name).write_bytes(data)
    report = dict(placements=len(placement), stockRecordSha256=hashlib.sha256(records).hexdigest(),
                  restoredBoothBodies=len(placement) - 293,
                  byAsset={f"{asset:08x}": n for asset, n in Counter(r[3] for r in placement).items()},
                  models=23, textures=len(wanted), stockCollision=True,
                  files={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest()) for name, data in files.items()},
                  sites=[dict(section=s, spawner=i, instance=k, asset=f"{asset:08x}", position=struct.unpack_from("<3f", r, 16))
                         for s, i, k, asset, r in placement])
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    result = compile_tolls(*sys.argv[1:])
    print(json.dumps({k: v for k, v in result.items() if k != "sites"}))
