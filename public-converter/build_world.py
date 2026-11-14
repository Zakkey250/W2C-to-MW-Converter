"""Convert original source IR into bounded MW runtime assets."""

import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys

from preflight import CATALOG, file_at, file_digest
from package_layout import CORE, RULES, STREAM, target_core


PROJECT = Path(__file__).resolve().parents[1]
REPACKER = (Path(__file__).with_name("bin") / "W2CMWv2Repack.exe" if getattr(sys, "frozen", False)
            else PROJECT / "compiler/native/bin/W2CMWv2Repack.exe")
sys.path[:0] = [str(PROJECT / "compiler/source-runtime"), str(PROJECT / "tools")]

from enrich_source_texture_index import enrich
from barriers_compile import main as compile_barriers
from bundle import ASSET_LIMIT, NAMES, Reader, Slice, Writer, canonical
from calculate_port import chunk
from collision_compile import compile_collision
from environment_compile import compile_environment
from model_gpu import compile_all
from mw_prop_fallback import compile_mw_fallback
from native_core import adapt_master
from native_sky import append_sky
from native_traffic import convert as build_traffic
from native_utilities import MODELS as UTILITY_MODELS, TEXTURES as UTILITY_TEXTURES, main as compile_utilities
from palmont_districts import compile_districts
from public_night_materials import effective_night_materials
from public_world_effects import compile_emitters
from pursuit import (PROP_PAYLOAD, PROP_TEXTURES, PURSUIT_FEATURE, PURSUIT_FILES, SEQUENCE_PAYLOAD, VAULT_NAMES,
                     build_pursuit, import_native_mw)
from scene_systems import compile_systems
from source_ir import Container
from terrain_compile import compile_terrain
from toll_compile import compile_tolls
from verify_original_models import verify as verify_models
from verify_original_textures import verify as verify_textures
from world_map_compile import main as compile_map


CITY_COVERAGE = Path(__file__).parent / "assets/world-city-coverage.w2g"
RUNTIME_ASI = Path(__file__).parent / "bin/W2CMWPublic.asi"
WORLD = (*NAMES.values(), "L5RA.symbols.json")
PACKAGE_FILES = (
    *(f"World/{name}" for name in WORLD),
    "Native/original-map-textures.bin",
    "Native/mw-toll-textures.bin",
    *(f"Core/{name}" for name in CORE),
    "W2CMWPublic.asi",
)
MW_PROP_TEXTURES = "Native/mw-prop-fallback-textures.bin"
MW_UTILITY_TEXTURES = "Native/utility-textures.bin"
AMBIENT_TEXTURES = "Native/ambient-textures.bin"
W2C_ROLES = {"world-stream", "master", "w2c-emitter-textures", "w2c-emitter-normals",
             "w2c-ingame-textures", "w2c-particle-textures"}
STOCK_BARRIER_BANKS = {"barrier-meshes": "barrier-meshes.bin", "barrier-groups": "barrier-groups.bin",
                       "barrier-reverse-vertices": "barrier-reverse-vertices.bin", "barrier-materials": "materials.json"}


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def build_assets(work, quality, progress=lambda *_: None, *, w2c=None, mw=None, carbon=None, vault_tool=None, mw_reference=None):
    source = work / "source"
    summary = json.loads((source / "import-summary.json").read_bytes())
    inputs = {entry["role"]: entry for entry in summary["sourceRows"]}
    carbon_inputs = CATALOG["carbonTextureBanks"]
    if not W2C_ROLES <= inputs.keys() <= W2C_ROLES | carbon_inputs.keys():
        raise ValueError("Unexpected W2C source inputs.")
    for role in inputs.keys() - W2C_ROLES:
        if inputs[role]["sha256"] != carbon_inputs[role]["sha256"]:
            raise ValueError(f"Carbon input changed during import: {role}")
    master, stream = (Path(inputs[key]["path"]) for key in ("master", "world-stream"))
    expected = {Path(row["path"]).name: row["sha256"] for row in CATALOG["qualities"][quality]}
    if file_digest(master) != expected["L5RA.BUN"] or file_digest(stream) != expected["STREAML5RA.BUN"]:
        raise ValueError("Source import and selected quality differ.")

    model_index = work / "original-models.jsonl"
    texture_index = work / "original-textures.jsonl"
    model_report = verify_models(stream, source, model_index, role="world-stream")
    if model_report["statistics"]["models"] != summary["models"]:
        raise ValueError("Model import count differs from the selected original stream.")
    write_json(work / "model-verification.json", model_report)
    w2c_inputs = (row for role, row in inputs.items() if role in W2C_ROLES)
    write_json(work / "texture-verification.json", verify_textures(w2c_inputs, source, texture_index))
    write_json(work / "texture-metadata.json", enrich(source, texture_index))

    progress("地形・モデル・テクスチャを生成", 45)
    compile_environment(source, master, work / "environment")
    # A user-selected SMB source is also read-only; scene_systems rehashes
    # each source before reading any offsets.
    compile_systems(source, master, work / "scene", allow_shared=True, texture_animation_roles=W2C_ROLES)
    compile_all(source, work / "models", model_index, stream)
    restored_props = set()
    if mw is not None:
        references = mw_inputs(mw, mw_reference)
        if carbon is not None and vault_tool is not None:
            restored_props = build_pursuit(work / "pursuit", source, master, stream, model_index, w2c, mw,
                                           references, carbon, vault_tool, progress)
        else:
            progress("MWの原本から動く小物を復元", 60)
            restored_props, _ = compile_mw_fallback(source, master, references["TRACKS/L2RA.BUN"],
                                                    references["TRACKS/STREAML2RA.BUN"], work / "mw-prop-fallback")
        progress("MW原本の純正イベントバリアを復元", 63)
        barrier_ir = work / "pursuit/mw-native"
        if not barrier_ir.is_dir():
            barrier_ir = work / "mw-barrier-source"
            import_native_mw({"mw-stream-pristine": references["TRACKS/STREAML2RA.BUN"],
                              "mw-master-pristine": references["TRACKS/L2RA.BUN"]}, barrier_ir, work,
                             modelHashes=["54a4fc3f", "54b22557", *(f"{key:08x}" for key in sorted(UTILITY_MODELS))],
                             textureHashes=["2c505700", "a711c606", *(f"{key:08x}" for key in sorted(UTILITY_TEXTURES))],
                             includeSystems=False)
        compile_barriers(barrier_ir, barrier_ir, references["TRACKS/L2RA.BUN"], work / "mw-stock-barriers",
                         master_role="mw-master-pristine")
        compile_utilities(work, references["TRACKS/STREAML2RA.BUN"], work / "mw-native-utilities",
                              imported=barrier_ir)
    progress("地形ページを生成", 65)
    compile_terrain(source, work / "models", master, work / "environment", work / "terrain", REPACKER,
                    public_decorations=True, source_quality=quality, native_prop_placements=restored_props)


def mw_inputs(mw, reference=None):
    result = {}
    for name, expected in RULES["mwInputs"].items():
        candidates = ((file_at(reference, name),) if reference else (
            file_at(mw, name), file_at(mw, "W2CMWPublic/originals/" + name)))
        match = next((path for path in candidates if path.is_file() and file_digest(path).lower() == expected), None)
        if match is None:
            raise ValueError(f"純正MWのフォルダーを参考元として指定してください: {name}")
        result[name] = match
    if reference:
        for name in ("GLOBAL/GLOBALB.BUN", "GLOBAL/InGameB.bun"):
            result[name] = file_at(reference, name)
    return result


def mw_target_base(mw):
    state = file_at(mw, "W2CMWPublic/installation.json")
    backup = target_core(json.loads(state.read_bytes()).get("targetCore", RULES["mwInputs"])) if state.is_file() else {}
    result = {}
    for name in (*CORE, STREAM):
        path = file_at(mw, name)
        if state.is_file() and (name in CORE or backup.get(name) != RULES["mwInputs"][name]):
            path = file_at(mw, "W2CMWPublic/originals/" + name)
        if not path.is_file() or (state.is_file() and file_digest(path).lower() != backup.get(name)):
            raise ValueError(f"導入前のMW資源を確認してください: {name}")
        result[name] = path
    return result


def read_rows(path):
    with path.open(encoding="utf-8") as stream:
        yield from map(json.loads, stream)


def add_file(writer, kind, key, path, metadata=None, *, role=3, count=1, stride=0):
    writer.add(kind, key, Slice(path, 0, path.stat().st_size), metadata,
               role=role, count=count, stride=stride)


def assemble_world(work, w2c, quality, mw, package, *, mw_reference=None, target_fingerprints=None):
    source = work / "source"
    night_materials, night_masks, night_report = effective_night_materials(work, mw, work / "terrain/materials.bank")
    source_info = json.loads((source / "import-summary.json").read_bytes())
    inputs = {row["role"]: row for row in source_info["sourceRows"]}
    master_path = Path(inputs["master"]["path"])
    master = master_path.read_bytes()
    references = mw_inputs(mw, mw_reference)
    target_paths = mw_target_base(mw)
    target = {name: file_digest(path).lower() for name, path in target_paths.items()}
    if target_fingerprints is not None and target != target_fingerprints:
        raise ValueError("導入先のMW資源が変換中に変更されました。")
    calibration = RULES["map"]
    if hashlib.sha256(chunk(master, 0x3b800)).hexdigest() != calibration["roadGraphSha256"]:
        raise ValueError("Selected quality does not match the calibrated W2C roads.")
    raw_map = w2c / "END_FILES/TRACKMAPS/CONTOUR_TRACKMAP_2048.DDS"
    calibration_path = work / "map-calibration.json"
    write_json(calibration_path, calibration)
    compile_map(raw_map, references["GLOBAL/InGameA.bun"], calibration_path, work / "map")
    collision_path = work / "collision.wmc"
    collision_report = compile_collision(source, master_path, None, None, collision_path,
                                         surface_policy=RULES["collisionSurfaces"], repair_bus_terminal=True)

    base = references["TRACKS/L2RA.BUN"].read_bytes()
    traffic, traffic_report = build_traffic(base, master, master_path)
    new_master, pursuit_markers, hiding_count, marker_count = adapt_master(base, master, traffic)
    if chunk(new_master, 0x8003b900) != chunk(base, 0x8003b900):
        raise ValueError("Generated MW master changed the stock prop collision bank.")
    toll_dir = work / "mw-tolls"
    tolls = compile_tolls(references["TRACKS/L2RA.BUN"], references["TRACKS/STREAML2RA.BUN"], toll_dir,
                         restore_hidden_booths=True)
    fallback_dir = work / "mw-prop-fallback"
    fallback = (json.loads((fallback_dir / "report.json").read_bytes())
                if (fallback_dir / "report.json").is_file() else {"restoredPlacements": 0, "uniqueSpawners": 0})
    pursuit_dir = work / "pursuit"
    pursuit = (json.loads((pursuit_dir / "summary.json").read_bytes())
               if (pursuit_dir / "summary.json").is_file() else None)
    ambient_dir = pursuit_dir / "ambient" if pursuit else work / "ambient"
    ambient = (json.loads((ambient_dir / "effect-report.json").read_bytes())
               if pursuit else compile_emitters(source, ambient_dir))
    has_ambient_textures = (ambient_dir / "textures.bin").is_file()
    barrier_dir = work / "mw-stock-barriers"
    barrier_report = json.loads((barrier_dir / "barrier-summary.json").read_bytes())
    if barrier_report["placements"] != 5771 or len(barrier_report["textures"]) != 2:
        raise ValueError("Original MW barrier inventory differs from the supported target.")
    terrain_report = json.loads((work / "terrain/terrain-summary.json").read_bytes())
    runtime_placements = (pursuit or {}).get("placements", 0) + fallback["restoredPlacements"]
    if terrain_report["statistics"].get("runtimePropPlacements", 0) != runtime_placements:
        raise ValueError("The terrain and moving props have different placement inventories.")
    has_fallback = fallback["uniqueSpawners"] > 0
    new_sky, _ = append_sky(target_paths["GLOBAL/InGameA.bun"].read_bytes(), references[STREAM], target_ingamea=True)
    package.mkdir()
    for name, content in zip(CORE, (new_master, new_sky)):
        path = package / "Core" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    (package / "Native").mkdir()
    shutil.copyfile(work / "map/textures.bin", package / "Native/original-map-textures.bin")
    shutil.copyfile(toll_dir / "mw-toll-textures.bin", package / "Native/mw-toll-textures.bin")
    utility_dir = work / "mw-native-utilities"
    shutil.copyfile(utility_dir / "textures.bin", package / MW_UTILITY_TEXTURES)
    if has_ambient_textures:
        shutil.copyfile(ambient_dir / "textures.bin", package / AMBIENT_TEXTURES)
    if has_fallback:
        shutil.copyfile(fallback_dir / "textures.bin", package / MW_PROP_TEXTURES)
    if pursuit:
        shutil.copyfile(pursuit_dir / PROP_PAYLOAD["prop-textures"], package / PROP_TEXTURES)
        (package / "Vault").mkdir()
        for name in VAULT_NAMES:
            shutil.copyfile(pursuit_dir / "merged/main" / name, package / "Vault" / name)
    shutil.copyfile(RUNTIME_ASI, package / "W2CMWPublic.asi")

    catalog_files = CATALOG["qualities"][quality] + CATALOG["sharedSource"]
    carbon_banks = CATALOG["carbonTextureBanks"]
    carbon_textures = []
    for row in read_rows(source / "textures-verified.jsonl"):
        role = row["metadata"]["origin"]["role"]
        if role in carbon_banks:
            if carbon_banks[role]["textures"].get(row["key"]) != row["sha256"]:
                raise ValueError(f"Unverified Carbon texture: {row['key']}")
            carbon_textures.append({"hash": row["key"], "name": row["metadata"]["texture"]["Name"],
                                    "bank": carbon_banks[role]["path"], "sha256": row["sha256"]})
    source_manifest = {
        "quality": quality, "version": CATALOG["w2cVersion"],
        "w2c": catalog_files, "mw": RULES["mwInputs"], "carbonUsed": bool(carbon_textures or pursuit),
    }
    if target != RULES["mwInputs"]:
        source_manifest["mwTarget"] = target
    if carbon_textures:
        source_manifest["carbonTextures"] = carbon_textures
    if night_report["sunSetTextureBankSha256"]:
        source_manifest["sunSetTextureBankSha256"] = night_report["sunSetTextureBankSha256"]
    world = {
        "schema": 1, "quality": quality, "runtime": "W2CMWPublic", "coordinateRevision": 1,
        "worldAndRadarShareMap": True, "gameNativeClosureComplete": True,
        "features": ["terrain", "collision", "roads", "traffic", "lights", "map", "radar", "decorative-props", "mw-tolls",
                     "mw-stock-barriers", "mw-native-spikes", "world-city-coverage", "night-building-lighting",
                     "world-animations", "world-ambient-effects"],
    }
    if has_fallback:
        world["features"].append("mw-prop-fallback")
    if carbon_textures:
        world["features"].append("carbon-texture-fallback")
    if pursuit:
        world["features"].append(PURSUIT_FEATURE)
        world["features"].append("palmont-neighborhood-names")
        source_manifest["mwVault"] = json.loads((pursuit_dir / "vault-base.json").read_bytes())
    disk_inputs = (Path(__file__).with_name("rules.json"), Path(__file__).with_name("catalog.json"), REPACKER, CITY_COVERAGE)
    identities = {p.name: file_digest(p) for p in disk_inputs}
    if getattr(sys, "frozen", False):
        compiled = json.loads(Path(__file__).with_name("compiler-identity.json").read_bytes())
        if any(compiled.get(name) != digest for name, digest in identities.items()):
            raise ValueError("The packaged converter components do not match their build identity.")
        identities = {**compiled, 'SourceWorld.exe': file_digest(Path(__file__).with_name('bin') / 'SourceWorld.exe')}
    else:
        identities[Path(__file__).name] = file_digest(Path(__file__))
        identities["palmont_districts.py"] = file_digest(Path(__file__).with_name("palmont_districts.py"))
        identities["public_night_materials.py"] = file_digest(Path(__file__).with_name("public_night_materials.py"))
        identities["public_world_effects.py"] = file_digest(Path(__file__).with_name("public_world_effects.py"))
        for name in ("mw_prop_fallback.py", "native_utilities.py", "toll_compile.py", "terrain_compile.py", "barriers_compile.py"):
            identities[name] = file_digest(PROJECT / "compiler/source-runtime" / name)
    compiler_id = hashlib.sha256(canonical(identities)).hexdigest()
    writer = Writer(package / "World", source_manifest, compiler_id, world)
    try:
        writer.add(1, "world-definition", canonical(world), {"schema": 1}, role=3)
        if target != RULES["mwInputs"]:
            writer.add(61, "native/mw-target-core", canonical(target), role=3)
        writer.add(11, "effective-materials", night_materials, {"schema": 1, **night_report}, role=3)
        add_file(writer, 51, "environment/world-city-coverage", CITY_COVERAGE, {"schema": 1, "cellMeters": 16})
        model_path = work / "models/models.gpu"
        for row in read_rows(work / "models/models.gpu.jsonl"):
            writer.add(22, "model/" + row["key"], Slice(model_path, row["offset"], row["bytes"], row["sha256"]),
                       row["metadata"], count=row["metadata"]["clusters"], stride=40)
        used_textures = set()
        for row in read_rows(work / "terrain/pages.jsonl"):
            section = row["sourceSection"]
            path = work / "terrain/pages" / f"{section}.page"
            writer.add(21, f"section/{section}", Slice(path, 0, path.stat().st_size, row["sha256"]), row,
                       count=row["clusters"], stride=40, dependencies=[(3, 11, "effective-materials")])
            used_textures.update(row["textures"])
        available_textures = set()
        for name, kind in (("scenes", 23), ("textures", 32)):
            bank = Container(source, name, "textures-verified" if name == "textures" else None)
            try:
                for key, row in bank.entries.items():
                    meta = {"schema": 1, "source": row["metadata"]}
                    if kind == 32:
                        variant = "/" in key
                        meta.update(sourceTextureHash=key.split("/")[0], variant=variant)
                        if not variant:
                            available_textures.add(int(key, 16))
                    writer.add(kind, name + "/" + key, Slice(bank.path, row["offset"], row["bytes"], row["sha256"]),
                               meta, role=2 if kind == 32 else 3)
            finally:
                bank.close()
        if night_masks is not None:
            bank = Container(night_masks, "textures")
            try:
                for key, row in bank.entries.items():
                    writer.add(32, "renderer-mask/" + key, Slice(bank.path, row["offset"], row["bytes"], row["sha256"]),
                               {"schema": 1, "source": row["metadata"], "sourceTextureHash": key, "variant": False}, role=2)
            finally:
                bank.close()
        for row in barrier_report["textures"]:
            alias = row["alias"]
            if int(alias, 16) in available_textures:
                raise ValueError("Stock barrier texture aliases conflict with W2C textures.")
            path = barrier_dir / (alias + ".dds")
            writer.add(32, "native-barrier-texture/" + alias, Slice(path, 0, row["bytes"], row["sha256"]),
                       {"sourceTextureHash": alias, "variant": False, "originalNativeTextureHash": row["sourceKey"]}, role=2)
        for key, name in STOCK_BARRIER_BANKS.items():
            add_file(writer, 61, "native/" + key, barrier_dir / name)
        missing_textures = sorted(used_textures - available_textures - {0})
        unimported = {int(value, 16) for value in source_info["missingTextures"]}
        if not set(missing_textures).issubset(unimported) or len(missing_textures) > 128:
            raise ValueError("Terrain has unexpected missing textures after conversion.")
        missing_ledger = [{"hash": f"{key:08x}", "reason": "W2C original and bundled W2C banks have no image"}
                          for key in missing_textures]
        writer.add(99, "audit/original-unavailable-runtime-textures", canonical(missing_ledger), role=3)
        for key, name in (
            ("weather-zones", "environment/weather-zones.bin"),
            ("lights", "environment/lights.bin"),
            ("visibility", "scene/visibility.json"),
            ("flares", "scene/flares.bin"),
            ("motions", "scene/world-animations.bin"),
        ):
            add_file(writer, 51, "environment/" + key, work / name)
        add_file(writer, 61, "native/original-map-textures", package / "Native/original-map-textures.bin")
        add_file(writer, 61, "native/original-map-aliases", work / "map/map-aliases.json")
        add_file(writer, 61, "native/mw-tolls", toll_dir / "mw-tolls.bin")
        add_file(writer, 61, "native/mw-toll-textures", package / "Native/mw-toll-textures.bin")
        add_file(writer, 61, "native/utility-models", utility_dir / "models.bun")
        add_file(writer, 61, "native/utility-textures", package / MW_UTILITY_TEXTURES)
        add_file(writer, 51, "environment/emitters", ambient_dir / "emitters.bin", ambient)
        if has_ambient_textures:
            add_file(writer, 61, "native/ambient-textures", package / AMBIENT_TEXTURES)
        if has_fallback:
            add_file(writer, 61, "native/mw-prop-fallback", fallback_dir / "placements.bin")
            add_file(writer, 61, "native/mw-prop-fallback-textures", package / MW_PROP_TEXTURES)
        if pursuit:
            writer.add(51, "environment/palmont-districts", compile_districts(pursuit_dir / "carbon-vault.json"),
                       {"source": "Carbon territory boundaries", "districts": 22}, role=3)
            for key, name in PROP_PAYLOAD.items():
                add_file(writer, 61, "native/" + key, pursuit_dir / name)
            for key in SEQUENCE_PAYLOAD:
                add_file(writer, 61, "native/" + key, pursuit_dir / key)
            add_file(writer, 61, "native/pursuit-vault-base", pursuit_dir / "vault-base.json")
            for name in VAULT_NAMES:
                add_file(writer, 62, "native-vault/" + name, package / "Vault" / name)
        writer.add(61, "native/pursuit-markers", pursuit_markers, role=3)
        for name in CORE:
            path = package / "Core" / name
            if path.stat().st_size <= ASSET_LIMIT:
                add_file(writer, 63, "native-core/" + name, path)
            else:
                # Native MW reads the loose core bank. Commit its exact digest
                # to the pack without creating a giant in-memory bundle asset.
                writer.add(63, "native-core-digest/" + name, bytes.fromhex(file_digest(path)), role=3)
        add_file(writer, 60, "collision", collision_path, {
            "encoding": "wmc1", "triangleCount": collision_report["triangles"],
            "nodeCount": collision_report["nodes"], "surfaceCount": len(collision_report["surfaces"]),
        },
                 count=collision_report["triangles"], stride=44)
        add_file(writer, 70, "world-and-radar-map", raw_map)
        add_file(writer, 71, "world-and-radar-calibration", calibration_path)
        result = writer.finish()
    finally:
        writer.close()

    reader = Reader(package / "World")
    try:
        for identity in reader.assets:
            reader.read_id(identity)
    finally:
        reader.close()
    files = []
    for name in (*PACKAGE_FILES, MW_UTILITY_TEXTURES, *((AMBIENT_TEXTURES,) if has_ambient_textures else ()),
                 *((MW_PROP_TEXTURES,) if has_fallback else ()), *(PURSUIT_FILES if pursuit else ())):
        path = package / name
        files.append({"path": name, "bytes": path.stat().st_size, "sha256": file_digest(path).lower()})
    write_json(package / "package.json", {
        "schema": 1, "packageId": "W2CMWPublic", "quality": quality, "buildId": result["buildId"],
        "files": files,
    })
    write_json(work / "conversion-result.json", {
        "quality": quality, "carbonUsed": bool(carbon_textures or pursuit), "carbonTextures": carbon_textures,
        "nightLighting": night_report, "worldEffects": ambient,
        "worldMotions": json.loads((work / "scene/scene-systems-summary.json").read_bytes())["ambientMotions"],
        "buildId": result["buildId"],
        "files": len(files) + 1, "bytes": sum(row["bytes"] for row in files) + (package / "package.json").stat().st_size,
        "verifiedBundleAssets": len(reader.assets), "terrainPages": terrain_report["pages"],
        "traffic": {"polygons": traffic_report["polygons"], "nodes": traffic_report["graph"]["nodes"],
                    "segments": traffic_report["graph"]["segments"], "uncoveredMainComponent": traffic_report["uncoveredMainComponent"]},
        "collisionTriangles": collision_report["triangles"], "nativeHidingRegions": hiding_count,
        "mwTolls": {key: tolls[key] for key in ("placements", "restoredBoothBodies", "models", "textures")},
        "mwFallbackProps": fallback,
        "pursuit": pursuit,
        "pursuitMarkersPreserved": marker_count, "missingTextures": missing_ledger,
        "stockBarrierPanels": barrier_report["placements"],
        "missingPlacements": terrain_report["missingPlacements"],
        "nativePropPlacementsPending": terrain_report["statistics"].get("nativePropPlacements", 0) - runtime_placements,
        "visualPropPlacements": terrain_report["statistics"].get("visualPropPlacements", 0),
        "pendingFeatures": ([] if pursuit else ["Dynamic physics and breakage for unmatched props",
                                               "Carbon pursuit breakers and their cameras"]),
    })
    return result


if __name__ == "__main__":
    build_assets(Path(sys.argv[1]).resolve(), sys.argv[2])
