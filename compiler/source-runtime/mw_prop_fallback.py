"""Restore W2C props where the local stock MW assets have a full match."""

from collections import Counter, defaultdict
import hashlib
import json
import math
import mmap
from pathlib import Path
import struct

from source_ir import Container, chunks
from source_systems import source_prop_rows, source_spawners
from terrain_compile import PUBLIC_DECORATION_PARAMETERS
from toll_compile import (TOLLS, aligned, native_models, native_textures, stock_trees,
                          word, write_native_models, write_native_textures)

# Both W2C meshes use the original gas-pump parameter. The verified MW pump
# uses that same parameter with its own destruction tree, markers and convex.
# Preserve the W2C placement and use the complete MW pump (rather than a
# visual-only W2C mesh) when the user has no Carbon assets.
GAS_PUMP_PARAMETER = 0x75CB7771
W2C_GAS_PUMPS = frozenset({(0x197C868F, 0x62C303C8), (0xD5458262, 0xAFBD4089)})
MW_GAS_PUMP = (0xDE004CB6, 0xDE004CB6, GAS_PUMP_PARAMETER, 0x7A365303)


def stock_templates(stream, chunks_in_stream):
    spawners = []
    scenery = defaultdict(list)
    for kind, _, start, end in chunks_in_stream:
        if kind == 0x34027:
            section, _, count = struct.unpack_from("<HHI", stream, start + 16)
            if end - start != 24 + count * 64:
                raise ValueError("MW prop spawner stride")
            spawners.extend((section, stream[at:at + 64]) for at in range(start + 24, end, 64)
                            if word(stream, at + 40) and word(stream, at + 32) not in TOLLS)
        elif kind == 0x80034100:
            parts = {tag: (a, b) for tag, _, a, b in chunks(stream, start, end)}
            if not {0x34101, 0x34102, 0x34103, 0x34106} <= parts.keys():
                continue
            section = word(stream, parts[0x34101][0] + 12)
            infos, info_end = parts[0x34102]
            begin, end = parts[0x34103]
            begin = aligned(begin, 16)
            a, b = parts[0x34106]
            for guid, index in struct.iter_unpack("<HH", stream[a:b]):
                at = begin + index * 64
                if at + 64 > end:
                    continue
                info = infos + struct.unpack_from("<h", stream, at + 62)[0] * 72
                if not infos <= info <= info_end - 72:
                    continue
                scenery[section, guid].append((struct.unpack_from("<3f", stream, at + 32),
                                                word(stream, info + 24), word(stream, info + 64)))
    templates = defaultdict(dict)
    for section, record in spawners:
        guid = struct.unpack_from("<H", record, 44)[0]
        x, y, z = struct.unpack_from("<3f", record, 16)
        matches = {(model, tree) for pos, model, tree in scenery[section, guid]
                   if model and math.dist(pos, (z, -x, y)) < 0.04}
        if len(matches) == 1:
            model, tree = matches.pop()
            triple = struct.unpack_from("<3I", record, 32)
            templates[(*triple, model)].setdefault(tree, record)
    return templates


def stock_definitions(master):
    trees = {}
    convex = set()
    for tag, _, start, end in chunks(master):
        if tag == 0x8003410B:
            for kind, _, a, b in chunks(master, start, end):
                if kind == 0x3410C:
                    root, count = struct.unpack_from("<II", master, a)
                    if not 1 <= count <= 32 or b - a != 8 + count * 16:
                        raise ValueError("MW original prop hierarchy stride")
                    trees[root] = master[a:b]
        if tag == 0x8003B900:
            convex.update(word(master, aligned(a, 16))
                          for kind, _, a, _ in chunks(master, start, end) if kind == 0x3B901)
    return trees, convex


def tree_models(tree):
    return {word(tree, 0), *(word(tree, 12 + i * 16) for i in range(word(tree, 4)))}


def compile_mw_fallback(source, w2c_master, mw_master, mw_stream, output):
    master = Path(mw_master).read_bytes()
    trees, convex = stock_definitions(master)
    original_toll_trees = stock_trees(master)
    toll_models = set().union(*(tree_models(tree) for tree in original_toll_trees.values()))
    candidates = defaultdict(list)
    counts = Counter()
    with Path(mw_stream).open("rb") as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as stream:
        rows = list(chunks(stream))
        templates = stock_templates(stream, rows)
        systems, scenes = Container(source, "systems"), Container(source, "scenes")
        try:
            for row in source_prop_rows(source_spawners(systems), scenes, Path(w2c_master).read_bytes()):
                if not row["active"]:
                    continue
                counts["active"] += 1
                record = row["record"]
                asset, secondary, parameter = (record[k] for k in ("asset", "secondary", "parameter"))
                if asset in TOLLS:
                    continue
                if parameter in PUBLIC_DECORATION_PARAMETERS:
                    counts["smallProps"] += 1
                model = row["solid"]
                matching = templates.get((asset, secondary, parameter, model), {})
                pump_substitution = (not matching and parameter == GAS_PUMP_PARAMETER
                                     and secondary == asset and (asset, model) in W2C_GAS_PUMPS)
                if pump_substitution:
                    asset, secondary, _, model = MW_GAS_PUMP
                    matching = templates.get(MW_GAS_PUMP, {})
                if len(matching) != 1 or asset not in convex or (secondary and secondary not in convex):
                    continue
                tree_key, template = next(iter(matching.items()))
                tree = trees.get(tree_key) if tree_key else trees.get(model)
                if tree is None and tree_key:
                    continue
                if tree is None:
                    tree = struct.pack("<6I", model, 1, 0x9756DF79, model, 0, 0xFF00)
                # This original MW roof uses its high-detail scenery solid
                # while intact and a distinct root solid once it breaks.
                gas_roof = (asset, secondary, parameter, model, tree_key) == (
                    0x969BFE7B, 0x969BFE7B, 0xF30B497B, 0x1978DAC3, 0x19796724)
                root = word(tree, 0)
                if (root != model and not (gas_roof and root == tree_key)) or tree_models(tree) & toll_models:
                    continue
                moved = bytearray(template)
                moved[:16] = record["raw"][:16]
                struct.pack_into("<3f", moved, 16, *row["mwPosition"])
                candidates[asset].append(((row["section"], row["instance"]), bytes(moved), tree, pump_substitution))
        finally:
            systems.close()
            scenes.close()
        unambiguous = {asset: matches for asset, matches in candidates.items()
                       if len({(value[1][32:44], value[2]) for value in matches}) == 1}
        wanted = set().union(*(tree_models(matches[0][2]) for matches in unambiguous.values()))
        geometry, geometry_template = native_models(stream, rows, wanted | toll_models, skip_ambiguous=True)
        toll_textures = set().union(*(geometry[key][2] for key in toll_models))
        requested = set().union(*(geometry[key][2] for key in wanted & geometry.keys())) - toll_textures
        textures, texture_template = native_textures(stream, rows, requested, skip_ambiguous=True)

    accepted = set()
    emitted = set()
    placements = bytearray()
    definitions = {}
    used_models = set()
    used_textures = set()
    for asset, matches in sorted(unambiguous.items()):
        tree = matches[0][2]
        needed = tree_models(tree)
        if needed - geometry.keys():
            continue
        materials = set().union(*(geometry[key][2] for key in needed)) - toll_textures
        if materials - textures.keys():
            continue
        definitions[asset] = tree
        used_models.update(needed)
        used_textures.update(materials)
        for source_id, record, _, pump_substitution in matches:
            accepted.add(source_id)
            if pump_substitution:
                counts["restoredGasPumps"] += 1
            if asset in (0x969BFE7B, 0xC299AC64):
                counts["restoredGasFacilityParts"] += 1
            if asset in (0x6EC75999, 0x68469917):
                counts["restoredTireFacilityParts"] += 1
            identity = (asset, record[:28])
            if identity in emitted:
                continue
            emitted.add(identity)
            placements.extend(record)

    output = Path(output)
    output.mkdir()
    report = dict(counts, restoredPlacements=len(accepted), uniqueSpawners=len(emitted),
                  assets=len(definitions), models=len(used_models), textures=len(used_textures))
    if accepted:
        models = write_native_models({key: geometry[key] for key in used_models}, geometry_template,
                                     b"W2CMW_MW_PROP_FALLBACK")
        headers = b"".join(struct.pack("<II", asset, len(tree)) + tree for asset, tree in definitions.items())
        payload = (struct.pack("<8s4I", b"W2MPROP\0", len(emitted), len(definitions), len(models), 0)
                   + placements + headers + models)
        (output / "placements.bin").write_bytes(payload)
        if not used_textures:
            raise ValueError("Expected at least one independent stock MW prop texture")
        texture_bank = write_native_textures({key: textures[key] for key in used_textures}, texture_template,
                                             b"W2CMW_MW_PROP_FALLBACK")
        (output / "textures.bin").write_bytes(texture_bank)
        report["sha256"] = {name: hashlib.sha256(data).hexdigest() for name, data in
                             (("placements.bin", payload), ("textures.bin", texture_bank))}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (output / "restored.json").write_text(json.dumps(sorted(accepted)), encoding="utf-8")
    return accepted, report
