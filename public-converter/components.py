"""Prepare the bundled optional roads and LBx64 for a locally generated world."""

from collections import defaultdict
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import zlib


ROOT = Path(__file__).resolve().parent
CONVERTER_VERSION = "RC6.1"
SOURCES = ROOT / "optional"
PORTABLE_OPTIONAL = Path(sys.executable).parent / "optional" if getattr(sys, "frozen", False) else None
LB_SOURCE = PORTABLE_OPTIONAL or SOURCES / "LBx64"
VCACHE_SOURCE = PORTABLE_OPTIONAL or SOURCES / "VCache"
ADDON_NAME = "W2C 未開通区間の暫定開通"
LB_ARCHIVE = "LBx64 W2CtoMW RC3.1.zip"
LB_SHA256 = "ffa7174d1e9abde4866c1d102db0285ce3ec7edf69a0e9eea621eecac463fe4a"
LB_PREVIOUS_SHA256 = "66f71a5c527a4b6e1a21605fd733daa526948f18938ef21e65ba2e166ddde32c"
LB_GUIDE = "README-LBx64.txt"
LB_PACKAGE_PATH = f"Optional/LBx64/{LB_ARCHIVE}"
VCACHE_ARCHIVE = "NFSMWVCacheAffinity 0.7.1.zip"
VCACHE_GUIDE = "README-VCacheAffinity.txt"
VCACHE_SHA256 = "ac8a9c220bdfdacf835cdb1d8665ed12e7c7b3c15be524f0986aa9be8077665f"
VCACHE_FILES = ("scripts/NFSMWVCacheAffinity.asi", "scripts/NFSMWVCacheAffinity.ini",
                "LICENSES/NFSMWVCacheAffinity-MinHook.txt")
# Carbon's flat grey utility image is absent from Carbonless W2C sources.
# Use W2C's own grey cement on those authored road surfaces instead.
ROAD_MATERIAL_ALIASES = {0x8C58916B: 0x22FFF2E5}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def current_lbx64_files(optional, *, required=False):
    """Use the complete bundled LBx64 with older verified world packs."""
    archive = optional.get(LB_PACKAGE_PATH)
    if archive is None and not required:
        return {}
    if archive is not None:
        fingerprint = digest(archive.read_bytes())
        if fingerprint == LB_SHA256:
            return {LB_PACKAGE_PATH: archive}
        if fingerprint != LB_PREVIOUS_SHA256:
            raise ValueError("Invalid LBx64 RC3.1 archive.")
    current = LB_SOURCE / LB_ARCHIVE
    guide = LB_SOURCE / LB_GUIDE
    if digest(current.read_bytes()) != LB_SHA256 or not guide.is_file():
        raise ValueError("The bundled LBx64 update does not match this converter.")
    return {LB_PACKAGE_PATH: current, f"Optional/LBx64/{LB_GUIDE}": guide}


def bundled_vcache_files():
    archive = VCACHE_SOURCE / VCACHE_ARCHIVE
    guide = VCACHE_SOURCE / VCACHE_GUIDE
    if not archive.is_file() or digest(archive.read_bytes()) != VCACHE_SHA256 or not guide.is_file():
        raise ValueError("NFSMWVCacheAffinityの同梱ファイルを確認できません。ZIPを展開し直してください。")
    return {f"Optional/VCache/{file.name}": file for file in (archive, guide)}


def encode(value):
    return (json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


def face_map(data, wanted):
    magic, version, count, nodes, names = struct.unpack_from("<5I", data)
    if (magic, version) != (0x31434D57, 1):
        raise ValueError("Unsupported collision format for the optional roads.")
    materials = [data[20+i*80:20+(i+1)*80].split(b"\0")[0] for i in range(names)]
    offset = 20 + names * 80 + nodes * 32
    if offset + count * 44 != len(data):
        raise ValueError("Truncated collision bank.")
    result, seen = {}, defaultdict(int)
    for ordinal in range(count):
        at = offset + ordinal * 44
        surface, wall = struct.unpack_from("<II", data, at+36)
        signature = digest(data[at:at+36] + struct.pack("<I", wall) + materials[surface])
        if signature in wanted:
            result[(signature, seen[signature])] = ordinal
            seen[signature] += 1
    return result


def rebind_roads(reader, core):
    from audit_breaker_sequences import chunks
    from bundle import asset_id

    source = SOURCES / "AddOn"
    identities = json.loads((source / "source-identities.json").read_bytes())
    raw = {name: (source / name).read_bytes() for name in identities["files"]}
    if any(digest(data) != identities["files"][name] for name, data in raw.items()):
        raise ValueError("The bundled Addon files failed the integrity check. Extract a fresh copy of the converter ZIP and try again.")
    patch = json.loads(raw["collision.json"])
    collision = reader.read(60, "collision")
    signatures = identities["faces"]
    lookup = face_map(collision, {row["sha256"] for row in signatures.values()})
    remap = {}
    for old, row in signatures.items():
        key = row["sha256"], row["occurrence"]
        if key not in lookup:
            raise ValueError("The original W2C collision is incompatible with the bundled roads.")
        remap[int(old)] = lookup[key]
    for name in ("removeFloor", "removeWalls"):
        patch[name] = sorted(remap[ordinal] for ordinal in patch[name])
    for name in ("keepFloorFragments", "keepWallFragments"):
        patch[name] = [[remap[ordinal], vertices] for ordinal, vertices in patch[name]]
    patch["generation"] = reader.build.hex()
    patch["collisionSha256"] = digest(collision)
    native_props = reader.read(61, "native/prop-placements") if reader.contains(61, "native/prop-placements") else None
    prop_lookup = {}
    if native_props is not None:
        magic, count = struct.unpack_from("<II", native_props)
        if magic != 0x31504D57 or len(native_props) != 8 + count * 64:
            raise ValueError("Invalid native prop catalog for the optional roads.")
        for i in range(count):
            row = native_props[8+i*64:8+(i+1)*64]
            prop_lookup.setdefault(digest(row[:40] + row[44:]), i)
    def prop_binding(old):
        index = prop_lookup.get(identities["propSignatures"][str(old)])
        if index is None:
            return None
        return index, digest(native_props[8+index*64:8+(index+1)*64])
    patch["closureProps"] = [list(binding) for old, _ in patch["closureProps"] if (binding := prop_binding(old))]
    for row in patch["closureDraws"]:
        section, old, *_ = row
        page = reader.read(21, f"section/{section}")
        count = struct.unpack_from("<I", page, 32)[0]
        offset = struct.unpack_from("<Q", page, 64)[0]
        wanted = identities["closureDrawSignatures"][f"{section}/{old}"]
        matches = [i for i in range(count) if digest(page[offset+i*128+4:offset+i*128+8]
                                                    + page[offset+i*128+20:offset+i*128+48]) == wanted]
        if len(matches) != 1:
            raise ValueError(f"Could not identify the source road closure in section {section}.")
        row[1] = matches[0]
    bank = reader.read(11, "effective-materials")
    material_count = struct.unpack_from("<I", bank, 12)[0]
    materials = {struct.unpack_from("<I", bank, 128+i*88)[0] for i in range(material_count)}
    result = {}
    for row in patch["geometryPages"]:
        section = row[0]
        page = bytearray(raw[f"section-{section}.w2p"])
        draws = struct.unpack_from("<I", page, 32)[0]
        draw_at = struct.unpack_from("<Q", page, 64)[0]
        for i in range(draws):
            at = draw_at + i * 128 + 4
            texture = struct.unpack_from("<I", page, at)[0]
            if texture and texture not in materials:
                substitute = ROAD_MATERIAL_ALIASES.get(texture)
                if substitute in materials:
                    struct.pack_into("<I", page, at, substitute)
        textures = {struct.unpack_from("<I", page, draw_at+i*128+4)[0] for i in range(draws)}
        if textures - materials - {0}:
            raise ValueError(f"Missing source materials for optional road section {section}.")
        payload_at = struct.unpack_from("<Q", page, 208)[0]
        struct.pack_into("<I", page, 204, zlib.crc32(page[256:payload_at]) & 0xFFFFFFFF)
        page[168:200] = hashlib.sha256(bank).digest()
        struct.pack_into("<I", page, 200, 0)
        struct.pack_into("<I", page, 200, zlib.crc32(page[:256]) & 0xFFFFFFFF)
        original = reader.assets[asset_id(reader.source, 21, f"section/{section}")]
        row[1], row[2] = original["sha"].hex(), digest(page)
        result[f"W2CMWPublic/AddOns/section-{section}.w2p"] = bytes(page)
    result["W2CMWPublic/AddOns/collision.json"] = encode(patch)
    collision_binding = digest(result["W2CMWPublic/AddOns/collision.json"])
    for name, field in (("authored-ground.json", "collisionAddonSha256"),
                        ("authored-props.json", "collisionSha256"),
                        ("road-environment.json", "collisionAddonSha256"),
                        ("tunnel-lights.json", "collisionAddonSha256")):
        doc = json.loads(raw[name])
        doc.update(generation=reader.build.hex(), **{field: collision_binding})
        # Native prop templates are supplied by Carbon. The roads, walls and
        # tunnel lights remain usable when the user omits that optional game.
        if name == "authored-props.json":
            if native_props is None:
                continue
            placements = []
            for row in doc["placements"]:
                if prop := prop_binding(row["template"]):
                    row["template"], row["templateSha256"] = prop
                    placements.append(row)
            doc["placements"] = placements
        result[f"W2CMWPublic/AddOns/{name}"] = encode(doc)
    road, = (payload for tag, _, payload in chunks(raw["L2RA.BUN"]) if tag == 0x3B800)
    result["TRACKS/L2RA.BUN"] = b"".join(
        struct.pack("<II", tag, len(road if tag == 0x3B800 else payload))
        + (road if tag == 0x3B800 else payload) for tag, _, payload in chunks(core))
    return result


def prepare(package):
    from bundle import Reader

    destination = package / "Optional"
    destination.mkdir(exist_ok=True)
    reader = Reader(package / "World")
    try:
        roads = rebind_roads(reader, (package / "Core/TRACKS/L2RA.BUN").read_bytes())
    finally:
        reader.close()
    for name, data in roads.items():
        target = destination / "AddOn/game" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (destination / "AddOn/INSTALL.txt").write_text(
        "W2C 未開通区間の暫定開通 / W2C provisional access to unopened roads\n\n"
        "推奨：RC6コンバーターの「変換済みパックを配置」からこのパックを選び、"
        "Addonをオンにして配置してください。退避とロールバックを自動で管理します。\n"
        "ゲームを終了してから、game 内のフォルダーをゲームのルートへコピーしてください。"
        "上書きするファイルは先にバックアップしてください。この変換パック専用です。"
        "一部の装飾・可動物にはCarbonが必要です。\n\n"
        "Recommended: select this pack in RC6's Install an existing pack screen and enable Addon. "
        "This manages backups and rollback automatically.\n"
        "For manual installation, close the game. Back up matching files, then copy the contents of game to the game folder. "
        "These roads are bound to this converted world. Some movable props require Carbon.\n",
        encoding="utf-8")
    lb = destination / "LBx64"
    lb.mkdir(exist_ok=True)
    for name in (LB_ARCHIVE, LB_GUIDE):
        shutil.copyfile(LB_SOURCE / name, lb / name)
    if digest((lb / LB_ARCHIVE).read_bytes()) != LB_SHA256:
        raise ValueError("The bundled LBx64 RC3.1 archive has changed.")
    for name, source in bundled_vcache_files().items():
        file = package / name
        file.parent.mkdir(exist_ok=True)
        shutil.copyfile(source, file)
    return [{"path": file.relative_to(package).as_posix(), "bytes": file.stat().st_size,
             "sha256": digest(file.read_bytes())}
            for file in sorted(destination.rglob("*")) if file.is_file()]
