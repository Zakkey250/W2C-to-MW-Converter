"""Compile W2C environmental placements; reuse original MW effects without Carbon."""
from collections import Counter, defaultdict
from contextlib import closing
import json
import math
import re
import struct

from attributes_compile import compile_attributes
from native_textures import main as compile_native_textures
from source_ir import Container


# Only identities and compatibility choices. No game assets are distributed here.
MW_EFFECTS = {
    0x9576CACA: ("env_lavasparks1", "omitted", 0, 0, False),
    0xA9FF07DB: ("env_lavasplatter1", "omitted", 0, 0, False),
    0xA1053ABD: ("fxenv_bat", "fxenv_birdblack01", 0x39EDE226, 300, False),
    0x45F3B7ED: ("fxenv_bird", "fxenv_bird", 0x45F3B7ED, 300, True),
    0x39EDE226: ("fxenv_birdblack01", "fxenv_birdblack01", 0x39EDE226, 300, False),
    0x4D9E7F58: ("fxenv_blackbird02", "fxenv_blackbird02", 0x4D9E7F58, 300, False),
    0xFD888A4B: ("fxenv_chimney1", "fxenv_chimney1", 0xFD888A4B, 150, True),
    0x239F3CCF: ("fxenv_chimney2", "fxenv_chimney1", 0xFD888A4B, 150, True),
    0x58C1DE27: ("fxenv_damfall", "fxenv_fountain2", 0x85676C22, 150, True),
    0xF84B3FF5: ("fxenv_fireworks", "empty", 0, 0, False),
    0xB41EC2B9: ("fxenv_fireworks_red", "empty", 0, 0, False),
    0xD3BA28D7: ("fxenv_fireworks_yellow", "empty", 0, 0, False),
    0x378D447E: ("fxenv_fog1", "fxenv_fog1", 0x378D447E, 175, True),
    0x0B9204CE: ("fxenv_fog1thick", "fxenv_fog1thick", 0x0B9204CE, 200, True),
    0xF8170EED: ("fxenv_fog2", "fxenv_fog2", 0xF8170EED, 175, True),
    0x8010A402: ("fxenv_fountain1", "fxenv_fountain1", 0x8010A402, 75, True),
    0x85676C22: ("fxenv_fountain2", "fxenv_fountain2", 0x85676C22, 150, True),
    0x67306F57: ("fxenv_fountain3", "fxenv_fountain3", 0x67306F57, 150, True),
    0x36FB97EF: ("fxenv_fountain4", "fxenv_fountain3", 0x67306F57, 150, True),
    0xA5A82FEC: ("fxenv_fountain5", "fxenv_fountain3", 0x67306F57, 150, True),
    0x0AEE710E: ("fxenv_fountain6", "fxenv_fountain2", 0x85676C22, 150, True),
    0x80576A2B: ("fxenv_ghost", "omitted", 0, 0, False),
    0x18333526: ("fxenv_lava1", "omitted", 0, 0, False),
    0x96AC4F78: ("fxenv_leaves1", "fxenv_leaves1", 0x96AC4F78, 150, True),
    0x0978DA3C: ("fxenv_moths1", "fxenv_dustmotes1", 0x08259CCF, 200, True),
    0x2C685D14: ("fxenv_ocean1", "fxenv_ripple1", 0x303DC26F, 250, True),
    0x4B54AD54: ("fxenv_small_steam1", "fxenv_small_steam1", 0x4B54AD54, 200, True),
    0x31FB2AB6: ("fxenv_small_steam2", "fxenv_small_steam2", 0x31FB2AB6, 200, True),
    0xAA7DAB77: ("fxenv_small_steam3", "fxenv_small_steam3", 0xAA7DAB77, 200, True),
    0x2F23057A: ("fxenv_smokestack", "fxenv_smokestack", 0x2F23057A, 1000, True),
    0xFD8995C1: ("fxenv_smokestack_brn", "fxenv_smokestack_brn", 0xFD8995C1, 1000, True),
    0x73EF880E: ("fxenv_smokestack_refinery", "fxenv_smokestack", 0x2F23057A, 1000, True),
    0xBCB0DACB: ("fxenv_snow", "omitted", 0, 0, False),
    0x131296B4: ("fxenv_snwblower", "omitted", 0, 0, False),
    0x39C8DA21: ("fxenv_torchfire1", "fxfire_sm1", 0x9CD7D94E, 200, True),
    0xEFA8FADF: ("fxenv_torchfire2", "fxfire_sm1", 0x9CD7D94E, 200, True),
    0x7C9A286D: ("fxenv_torchfire3", "fxfire_sm1", 0x9CD7D94E, 200, True),
    0xE016F4CB: ("fxenv_torchfire4", "fxfire_sm1", 0x9CD7D94E, 200, True),
    0x53B9B550: ("fxnis_leafblast1", "fxnis_leafblast1", 0x53B9B550, 200, False),
    0x4E4E9BBE: ("fxnis_leafblast2", "fxnis_leafblast2", 0x4E4E9BBE, 500, False),
    0x5CFEB5AA: ("fxnis_leafblast3", "fxnis_leafblast3", 0x5CFEB5AA, 500, False),
    0x437A9A0B: ("fxwtr_fountain1", "fxwtr_fountain1", 0x437A9A0B, 200, False),
}
REF = re.compile(r"(\w+)\s*->\s*([^,\]|]+)")
SPRITE = re.compile(r"mEnum: (\w+) \| mIndex: (\d+)")
SPRITE_CAPACITY = 200


def groups(source):
    records = []
    with closing(Container(source, "systems")) as bank:
        for key, row in bank.entries.items():
            origin = row["metadata"]["origin"]
            if origin["chunk"] != "0003bc00" or origin["role"] not in ("master", "world-stream"):
                continue
            data = bytes(bank.get(key))
            at = (-(origin["offset"] + 8)) % 16
            reserved, version, count, _ = struct.unpack_from("<4I", data, at)
            at += 16
            if reserved or version != 5 or count >= 10000:
                raise ValueError("Invalid W2C ambient emitter bank.")
            for _ in range(count):
                effect, blank, section, extra, pointer = struct.unpack_from("<IIHHI", data, at)
                size = 80 + 48 * extra
                if blank or pointer or extra > 80 or at + size > len(data):
                    raise ValueError("Invalid W2C ambient emitter record.")
                records.append((effect, section, extra, data[at:at + size]))
                at += size
            if at != len(data):
                raise ValueError("Unexpected bytes in W2C ambient emitter bank.")
    if not records or len(records) >= 10000:
        raise ValueError("Invalid W2C ambient emitter count.")
    return records


def row_index(rows):
    return {(r["class"], r["name"]): r for r in rows}


def effective(rows, key, trail=()):
    if key in trail:
        raise ValueError("Particle attribute inheritance cycle.")
    r = rows[key]
    values = effective(rows, (key[0], r["parent"]), (*trail, key)) if r["parent"] else {}
    return values | r["fields"]


def references(value):
    return [(cls, name.strip()) for cls, name in REF.findall(value) if name.strip()]


def compact_emitter_list(value):
    if not value.startswith("[") or not value.endswith("]"):
        raise ValueError("Invalid Carbon emitter group list.")
    refs = []
    for slot in value[1:-1].split(","):
        slot = slot.strip()
        if not slot or re.fullmatch(r"(?:emitterdata\s*)?->", slot):
            continue
        match = REF.fullmatch(slot)
        if not match or match[1] != "emitterdata" or not match[2].strip():
            raise ValueError("Invalid Carbon emitter group reference: " + slot)
        refs.append(f"{match[1]} -> {match[2].strip()}")
    return "[" + ",".join(refs) + "]"


def texture_id(name):
    if name.isdigit():
        return int(name)
    h = 0xFFFFFFFF
    for c in name:
        h = (h * 33 + ord(c)) & 0xFFFFFFFF
    return h


def allocate_sprites(mw_rows, requested):
    """Map imported sprites without changing or overwriting existing slots."""
    candidates, owners = defaultdict(set), defaultdict(set)
    for row in mw_rows:
        match = SPRITE.fullmatch(row["fields"].get("Texture", "")) if row["class"] == "emitterdata" else None
        if match:
            key, slot = texture_id(match[1]), int(match[2])
            if slot < SPRITE_CAPACITY:
                owners[slot].add(key)
                if key:
                    candidates[key].add(slot)

    # Keep normal allocations above the native table. If it has reached the
    # limit, use unclaimed holes rather than assigning any slot twice.
    free = [i for i in range(1, SPRITE_CAPACITY) if i not in owners]
    high = max(owners, default=0)
    free = iter(sorted(free, key=lambda i: (i <= high, i)))
    mapping, omitted = {"0": {"index": 0}}, set()
    for key in sorted(set(requested) - {0}):
        recorded = candidates[key]
        slot = next(iter(recorded)) if len(recorded) == 1 else None
        if slot is None or not slot or owners[slot] != {key}:
            slot = next(free, None)
        if slot is None:
            omitted.add(key)
        else:
            mapping[str(key)] = {"index": slot}
    return mapping, omitted


def prepare_carbon(source, native, carbon_rows, mw_rows, template, output, import_sprites, *, namespace="w2v2_"):
    records = groups(source)
    # Carbon stores a fixed-size list padded with null attribute references.
    # MW validates every listed emitter (EN1.3 0050C850), including nulls,
    # and rejects the whole group when any reference cannot be resolved.
    carbon_rows = [
        {**row, "fields": {**row["fields"], "Emitters": compact_emitter_list(row["fields"]["Emitters"])}}
        if row["class"] == "emittergroup" and "Emitters" in row["fields"]
        else row for row in carbon_rows
    ]
    carbon = row_index(carbon_rows)
    carbon_groups = {int(row["hash"], 16): name for (cls, name), row in carbon.items() if cls == "emittergroup"}
    selected = sorted({carbon_groups[h] for h, _, _, _ in records if h in carbon_groups
                       and effective(carbon, ("emittergroup", carbon_groups[h]))["Emitters"] != "[]"})
    classes = {"emittergroup", "emitterdata", "effects", "fuelcell_effect", "fuelcell_emitter", "emitteruv"}
    requests = {}
    for name in selected:
        pending, done, requested = [("emittergroup", name)], set(), set()
        while pending:
            key = pending.pop()
            if key in done:
                continue
            done.add(key)
            fields = effective(carbon, key)
            match = SPRITE.fullmatch(fields.get("Texture", "")) if key[0] == "emitterdata" else None
            if match:
                requested.add(texture_id(match[1]))
            pending.extend(ref for value in fields.values() for ref in references(value) if ref[0] in classes)
        requests[name] = requested
    requested = set().union(*requests.values())
    mapping, exhausted = allocate_sprites(mw_rows, requested)
    fallback = {name for name in selected if requests[name] & exhausted}
    selected = [name for name in selected if name not in fallback]
    requested = set().union(*(requests[name] for name in selected))
    if selected:
        compile_attributes(output, mw_rows, carbon_rows, [], environment_groups=selected,
                           environment_textures=mapping, namespace=namespace)
    else:
        output.mkdir(parents=True, exist_ok=True)
    with closing(Container(native, "textures")) as bank, closing(Container(source, "textures", "textures-verified")) as primary:
        wanted = {f"{h:08x}" for h in requested if h and f"{h:08x}" not in bank.entries}
        missing = wanted - primary.entries.keys()
    supplement = import_sprites(missing) if missing else None
    if supplement:
        with closing(Container(supplement, "textures")) as bank:
            if not missing <= bank.entries.keys():
                raise ValueError("Selected Carbon resources lack original ambient sprites: " + ", ".join(sorted(missing - bank.entries.keys())))
    if wanted:
        (output / "texture-closure.json").write_text(json.dumps(sorted(wanted)), encoding="utf-8")
        compile_native_textures(source, native, output, template, output, 0, "W2CMW_V2_AMBIENT",
                                supplement=supplement)
    return records, carbon, carbon_groups, set(selected), namespace, sorted(fallback)


def compile_emitters(source, output, *, prepared=None, compiled_rows=()):
    output.mkdir(parents=True, exist_ok=True)
    records = prepared[0] if prepared else groups(source)
    mapped = row_index(compiled_rows)
    selected = {}
    if prepared:
        _, carbon, carbon_groups, originals, namespace, _ = prepared
        for key, name in carbon_groups.items():
            if name not in originals:
                continue
            fields = effective(carbon, ("emittergroup", name))
            repeat = any(effective(carbon, ref).get("IsOneShot", "False") != "True"
                         for ref in references(fields["Emitters"]))
            compiled = mapped["emittergroup", namespace + "env_emittergroup_" + name]
            selected[key] = (name, name, int(compiled["hash"], 16), float(fields.get("FarClip", "150")), repeat)
    counters, omissions, aliases = Counter(), Counter(), Counter()
    payload, seen = bytearray(), set()
    for effect, section, extra, original in records:
        name, target, key, distance, repeat = selected.get(effect) or MW_EFFECTS.get(
            effect, (f"{effect:08x}", "omitted", 0, 0, False))
        if not key:
            counters[target] += 1
            if target == "omitted":
                omissions[name] += 1
            continue
        if not 0 < distance <= 2000 or not all(math.isfinite(f) for f in struct.unpack_from("<16f", original, 16)):
            raise ValueError("Invalid W2C ambient emitter transform or range.")
        raw = bytearray(original)
        struct.pack_into("<I", raw, 0, key)
        struct.pack_into("<H", raw, 8, 2600)
        for at in (64, *(96 + 48 * i for i in range(extra))):
            x, y, z = struct.unpack_from("<3f", raw, at)
            if not all(math.isfinite(f) for f in (x, y, z)):
                raise ValueError("Invalid W2C ambient emitter trigger.")
            struct.pack_into("<2f", raw, at, x - 6016, y + 546)
        identity = bytes(raw)
        if identity in seen:
            counters["duplicates"] += 1
            continue
        seen.add(identity)
        if effect in selected:
            counters["carbon"] += 1
        elif name == target:
            counters["mwSameGroup"] += 1
        else:
            counters["mwSubstitutes"] += 1
            aliases[name + " -> " + target] += 1
        payload.extend(struct.pack("<IIfII", section, len(raw), distance, extra, int(repeat)))
        payload.extend(raw)
    (output / "emitters.bin").write_bytes(struct.pack("<II", 0x32454D57, len(seen)) + payload)
    report = {"sourcePlacements": len(records), "placements": len(seen),
              **{k: counters[k] for k in ("carbon", "mwSameGroup", "mwSubstitutes", "empty", "omitted", "duplicates")},
              "omittedGroups": dict(omissions), "substituteGroups": dict(aliases),
              "spriteCapacityFallbackGroups": prepared[5] if prepared else []}
    (output / "effect-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report

