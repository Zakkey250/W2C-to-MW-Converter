"""Restore original sign emission and night lighting from local resources."""

import json
import math
import re
import struct

import yaml

from preflight import SUNSET_ASI, SUNSET_PACK, file_digest, sunset_resource_valid
from pursuit import import_native_mw
from source_ir import Container


# The stock exchange's scrolling text is its own diffuse texture. Its frame
# uses a separate material, so only the text should bypass scene lighting.
STOCK_TICKER = 0xDA463E9C
STOCK_TICKER_RULE = (0, 1., 1., 1., 2.0, 1, 1, 0)


def texture_hash(name):
    if not isinstance(name, str) or not name:
        return 0
    if re.fullmatch(r"0x[0-9a-fA-F]{8}", name):
        return int(name, 16)
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", name):
        return 0
    value = 0xFFFFFFFF
    for char in name.upper().encode("ascii"):
        value = (value * 33 + char) & 0xFFFFFFFF
    return value


def sunset_rules(mw, textures):
    path = mw / "scripts/SunSetData/Textures.yml"
    pack = mw / SUNSET_PACK / "TEXTURES.BIN"
    if not (mw / SUNSET_ASI).is_file() or not path.is_file() or path.stat().st_size > 2 * 1024**2:
        return {}, None
    if not sunset_resource_valid(pack, SUNSET_PACK + "/TEXTURES.BIN"):
        return {}, None
    data = yaml.safe_load(re.sub(rb"[ \t]+(?=#)", b" ", path.read_bytes()))
    rows = data.get("Textures", []) if isinstance(data, dict) else []
    if not isinstance(rows, list) or len(rows) > 4096:
        raise ValueError("SunSetの発光テクスチャ設定を確認してください。")
    rules = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        base, mask = texture_hash(row.get("Name")), texture_hash(row.get("Mask"))
        if base not in textures or not mask or base == mask or textures[base]["AlphaBlendType"] != 0:
            continue
        rgb, strength = row.get("Color"), row.get("Brightness")
        if not isinstance(rgb, list) or len(rgb) != 3 or not isinstance(strength, (int, float)):
            continue
        if not all(isinstance(c, (int, float)) and math.isfinite(c) and 0 <= c <= 4 for c in rgb):
            continue
        if not math.isfinite(strength) or not 0 < strength <= 12:
            continue
        rules[base] = (mask, *rgb, strength, 0, 0, 0)
    return rules, pack


def effective_night_materials(work, mw, material_bank):
    source = Container(work / "source", "textures", "textures-verified")
    try:
        textures = {int(key, 16): row["metadata"]["texture"]
                    for key, row in source.entries.items() if "/" not in key}
    finally:
        source.close()
    names = {row["Name"].upper(): key for key, row in textures.items()}
    night_rules = {}
    for name, base in names.items():
        if name.endswith("_D") and name[:-2] + "_E" in names and textures[base]["AlphaBlendType"] == 0:
            night_rules[base] = (names[name[:-2] + "_E"], 1., 1., 1., 2., 0, 0, 0)

    local_rules, pack = sunset_rules(mw, textures)
    missing = {mask for mask, *_ in local_rules.values()} - textures.keys()
    supplement = None
    imported = set()
    source_sha = None
    if missing:
        supplement = work / "sunset-night-masks"
        source_sha = file_digest(pack).lower()
        if not supplement.exists():
            import_native_mw({"sunset-original-masks": pack}, supplement, work,
                             modelHashes=[], textureHashes=[f"{h:08x}" for h in sorted(missing)],
                             includeSystems=False)
        from_summary = json.loads((supplement / "import-summary.json").read_bytes())
        if from_summary["sources"][0]["sha256"].lower() != source_sha:
            raise ValueError("変換中にSunSetの発光テクスチャ資源が変わりました。")
        bank = Container(supplement, "textures")
        try:
            imported = {int(key, 16) for key in bank.entries}
            if not imported <= missing:
                raise ValueError("SunSetの発光マスクの照合に失敗しました。")
        finally:
            bank.close()

    available = textures.keys() | imported
    night_rules.update({base: rule for base, rule in local_rules.items() if rule[0] in available})
    ticker = textures.get(STOCK_TICKER)
    ticker_emission = bool(ticker and ticker['Name'] == 'SGN_TNTICKERSCROLL_01_D'
                           and ticker['ScrollType'] == 1 and ticker['ScrollSpeedS'])
    if ticker_emission:
        night_rules[STOCK_TICKER] = STOCK_TICKER_RULE
    data = bytearray(material_bank.read_bytes())
    version, count, animations, stride = struct.unpack_from("<4I", data, 8)
    if data[:8] != b"W2MATB1\0" or (version, count, animations, stride) != (1, len(textures), 0, 88):
        raise ValueError("Unexpected base material format.")
    if len(data) != 128 + count * stride:
        raise ValueError("Unexpected base material size.")
    found = set()
    for at in range(128, len(data), stride):
        base, = struct.unpack_from("<I", data, at)
        if base in night_rules:
            struct.pack_into("<I4f3i", data, at + 52, *night_rules[base])
            found.add(base)
    if found != night_rules.keys():
        raise ValueError("Night materials do not match the converted textures.")
    emitted = frames = 0
    omitted = []
    assigned = set()
    for row in json.loads((work / "scene/texture-animations.json").read_bytes()):
        hashes = [int(key, 16) for key in row["frames"]]
        fps, mode = row["fps"], row["base"]
        if not (1 <= fps <= 255 and mode in (0, 1) and 1 <= len(hashes) <= 255
                and len(hashes) == row["count"]):
            raise ValueError("Unexpected source texture animation format.")
        missing_frames = set(hashes) - textures.keys()
        if missing_frames or assigned.intersection(hashes):
            omitted.append({"firstFrame": row["frames"][0],
                            "missingFrames": [f"{key:08x}" for key in sorted(missing_frames)],
                            "overlap": bool(assigned.intersection(hashes))})
            continue
        data.extend(struct.pack(f"<{3 + len(hashes)}I", fps, mode, len(hashes), *hashes))
        assigned.update(hashes)
        emitted += 1
        frames += len(hashes)
    struct.pack_into("<I", data, 16, emitted)
    return bytes(data), supplement, {
        "maskedMaterials": len(found) - int(ticker_emission), "stockTickerEmissive": ticker_emission,
        "importedSunSetMasks": len(imported),
        "unavailableSunSetMasks": len(missing - imported), "sunSetTextureBankSha256": source_sha,
        "textureAnimations": emitted, "textureFrames": frames, "omittedTextureAnimations": omitted,
    }
