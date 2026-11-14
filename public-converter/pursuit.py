"""Generate functional pursuit banks and a private merge of the user's MW vault."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from preflight import CATALOG, file_at, file_digest
from carbon_sequences import (
    adapt_original_prop_rows, compile_carbon_prop_payload, compile_carbon_pursuit_recipe,
    compile_carbon_sequences, join_carbon_pursuit_placements, original_prop_recipe,
)
from prepare_public_breaker_inputs import checked_inputs
from public_world_effects import compile_emitters, prepare_carbon


VAULT_NAMES = ("attributes.bin", "fe_attrib.bin", "gameplay.bin", "gameplay.lzc")
PROP_PAYLOAD = {
    "prop-placements": "props/catalog.bin",
    "prop-hierarchies": "props/hierarchies.bin",
    "prop-parameters": "props/parameters.bin",
    "prop-convex": "props/collision.bun",
    "prop-models": "models/models.bun",
    "prop-textures": "textures/textures.bin",
}
SEQUENCE_PAYLOAD = ("sequences", "sequence-routes", "cameras", "camera-markers")
PURSUIT_FEATURE = "carbon-pursuit"
PROP_TEXTURES = "Native/prop-textures.bin"
PURSUIT_FILES = (PROP_TEXTURES, *(f"Vault/{name}" for name in VAULT_NAMES))

BIN = Path(__file__).with_name("bin")
SOURCE_IMPORTER = Path(__file__).resolve().parents[1] / "compiler/source-world/bin/Release/net11.0/SourceWorld.dll"
TOOL_FILES = (
    "Attribulator.CLI.dll", "NativeLibrary.dll", "VaultLib.Core.dll",
    "plugins/Attribulator.Plugins.SpeedProfiles/VaultLib.Support.Carbon.dll",
    "plugins/Attribulator.Plugins.SpeedProfiles/VaultLib.Support.MostWanted.dll",
    "plugins/Attribulator.Plugins.SpeedProfiles/Resources/hashes.txt",
    "plugins/Attribulator.Plugins.ModScript/Attribulator.Plugins.ModScript.dll",
)
NATIVE_REFERENCES = {
    "mw-stream-pristine": ("TRACKS/STREAML2RA.BUN", "15016611bcb7511d255c092321f87c659b101910c054aba8b750d722036c111a"),
    "mw-master-pristine": ("TRACKS/L2RA.BUN", "b4da68460c9f642c76e2d603d3d50c6036f0c393bbdb11c7cb5ed690f7fd70d8"),
    "mw-globalb-pristine": ("GLOBAL/GLOBALB.BUN", "05e8e612e3b1126d2bb889ebb003f283c160ce9251b3ce970e24d01e351b3ef7"),
    "mw-ingamea-pristine": ("GLOBAL/InGameA.bun", "f65f05ef2ccea60a284af6c26923e55a95161f9ace7390dbbf237b964950b51d"),
    "mw-ingameb-pristine": ("GLOBAL/InGameB.bun", "934bae4ee1a728af252d65dbabde896eac665103d152b60099b705f499fb0879"),
}


def validate_vault_tool(tool):
    missing = [name for name in TOOL_FILES if not file_at(tool, name).is_file()]
    if missing:
        raise ValueError("OGVI 1.4 / Attribulator 2.0: " + ", ".join(missing))


def run(command, work):
    process = subprocess.run([str(part) for part in command], cwd=work, capture_output=True,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), timeout=480)
    if process.returncode:
        detail = (process.stderr or process.stdout).decode("utf-8", errors="replace")
        raise ValueError(detail[-3500:].strip() or f"Conversion helper exited with code {process.returncode}.")


def native_references(mw, references):
    result = {}
    for role, (name, expected) in NATIVE_REFERENCES.items():
        path = references.get(name) or file_at(mw, name)
        if not path.is_file() or file_digest(path).lower() != expected:
            raise ValueError(f"パースートブレーカーの復元にはMW原本の {name} が必要です。")
        result[role] = path
    return result


def import_native_mw(paths, output, work, **selection):
    config = work / (output.name + ".json")
    config.write_text(json.dumps({**selection, "files": [{"role": key, "path": str(path)}
                                                         for key, path in paths.items()]}), encoding="utf-8")
    command = [BIN / "SourceWorld.exe"] if getattr(sys, "frozen", False) else ["dotnet", SOURCE_IMPORTER]
    run([*command, "mw-target", config, output], work)


def copy_vault(root, stage, names):
    stage.mkdir()
    baseline = {}
    for name in names:
        original = file_at(root, "GLOBAL/" + name)
        if not original.is_file() or not 0 < original.stat().st_size <= 16 * 1024**2:
            raise ValueError(f"選択したゲームの物理属性ファイルが見つからないか、形式が異なります。GLOBAL/{name}")
        shutil.copyfile(original, stage / name)
        baseline[name] = file_digest(stage / name).lower()
        if file_digest(original).lower() != baseline[name]:
            raise ValueError(f"変換中に物理属性ファイルが変わりました。GLOBAL/{name}")
    return baseline


def available_overlay_namespace(rows):
    names = {row["name"].casefold() for row in rows}
    for attempt in range(len(names) + 1):
        prefix = "w2v2_" if attempt == 0 else f"w2v2_p{attempt:x}_"
        if not any(name.startswith(prefix) for name in names):
            return prefix
    raise ValueError("Could not allocate a private W2C attribute namespace.")


def merge_vault(tool, w2c, carbon, mw, master, native_master, source, native, ambient_template, work):
    helper = BIN / "PublicVault.exe"
    validate_vault_tool(tool)
    stage = work / "original-mw"
    carbon_stage = work / "original-carbon"
    baseline = copy_vault(mw, stage, VAULT_NAMES)
    copy_vault(carbon, carbon_stage, VAULT_NAMES[:-1])

    def audit(game, folder, name, plan=None):
        output = work / (name + ".json")
        run([helper, tool, "audit", game, folder, output, *([plan] if plan else [])], work)
        return json.loads(output.read_bytes())["rows"]

    carbon_rows = audit("CARBON", carbon_stage, "carbon-vault")
    target_rows = audit("MOST_WANTED", stage, "mw-vault")
    namespace = available_overlay_namespace(target_rows)
    source_rows, native_rows, substitutions = adapt_original_prop_rows(carbon_rows, target_rows)
    placements, join = join_carbon_pursuit_placements(source, native, master, work / "join")
    keys = {row["parameter"] for row in placements if row["active"]}
    parameters = {row["hash"].lower(): row["name"] for row in source_rows if row["class"] == "smackable"}
    recovered = []

    def restore_missing(missing):
        nonlocal carbon_stage
        script_path = "VLT_FILES/W2C_SMACKABLE_PROPS.NFSMS"
        expected = next(row for row in CATALOG["sharedSource"] if row["path"] == script_path)
        script = file_at(w2c, script_path).read_bytes()
        if len(script) != expected["bytes"] or hashlib.sha256(script).hexdigest().upper() != expected["sha256"]:
            raise ValueError("W2C 2.0.0の小物属性スクリプトが原本と一致しません。")
        recipe, expected_rows = original_prop_recipe(script.decode("utf-8-sig"), missing, set(parameters.values()))
        names = set(expected_rows.values())
        repair_id = f"carbon-props-{len(recovered)}"
        source_recipe = work / (repair_id + ".nfsms")
        source_recipe.write_text(recipe, encoding="utf-8")
        patched = work / repair_id
        run([helper, tool, "merge", carbon_stage, patched, source_recipe, "CARBON"], work)
        carbon_stage = patched / "main"
        rows = [row for row in audit("CARBON", carbon_stage, repair_id)
                if row["class"] == "smackable" and row["name"] in names]
        recovered_ids = {row["hash"].lower(): row["name"] for row in rows}
        if recovered_ids != expected_rows:
            raise ValueError("W2C原本から復元した小物属性を照合できませんでした。")
        source_rows.extend(rows)
        parameters.update(recovered_ids)
        recovered.extend(sorted(names))

    if missing := keys - parameters.keys():
        restore_missing(missing)
    roots = sorted({parameters[key] for key in keys})
    recipe_dir = work / "recipe"
    recipe = compile_carbon_pursuit_recipe(carbon, native_master, source_rows, native_rows, roots,
                                           recipe_dir, restore_missing, namespace=namespace)
    plan = recipe_dir / "attribute-adapter-plan.json"
    edits = json.loads(plan.read_bytes())
    ambient = work / "ambient"

    def import_sprites(keys):
        directory = work / "carbon-sprites"
        files = {f"carbon-sprites-{name}": file_at(carbon, "GLOBAL/" + name)
                 for name in ("GLOBALB.BUN", "InGameB.bun")}
        import_native_mw({key: path for key, path in files.items() if path.is_file()}, directory, work,
                         textureHashes=sorted(keys), modelHashes=[], includeSystems=False, carbonSprites=True)
        return directory

    prepared = prepare_carbon(source, native, carbon_rows, target_rows, ambient_template, ambient, import_sprites,
                              namespace=namespace)
    ambient_plan = ambient / "attribute-adapter-plan.json"
    if ambient_plan.is_file():
        edits.extend(json.loads(ambient_plan.read_bytes()))
        attributes = recipe_dir / "attributes.nfsms"
        original_script = attributes.read_text(encoding="utf-8")
        ambient_script = (ambient / "attributes.nfsms").read_text(encoding="utf-8").splitlines()
        if not ambient_script or ambient_script[0] != "game MW":
            raise ValueError("Unexpected ambient attribute recipe header.")
        attributes.write_text(original_script.rstrip() + "\n" + "\n".join(ambient_script[1:]) + "\n", encoding="utf-8")
    plan.write_text(json.dumps(edits, indent=2), encoding="utf-8")
    before = {(r["class"], r["name"]): r for r in target_rows}
    added = {(r["cls"], r["target"]) for r in edits}
    if len(added) != len(edits) or before.keys() & added or any(not name.startswith(namespace) for _, name in added):
        raise ValueError("Converted attributes overlap the destination vault.")
    run([helper, tool, "merge", stage, work / "merged", recipe_dir / "attributes.nfsms"], work)
    merged = work / "merged/main"
    # The runtime publishes additive exports from attributes.bin only. OGVI
    # reserializes the other banks too, which can change byte-encoded fields
    # in an unrelated event. Audit exactly what the runtime will use.
    for name in VAULT_NAMES:
        if name != "attributes.bin":
            shutil.copyfile(stage / name, merged / name)
    compiled_rows = audit("MOST_WANTED", merged, "mw-merged", plan)
    after = {(r["class"], r["name"]): r for r in compiled_rows}
    if after.keys() - before.keys() != added or any(after.get(key) != value for key, value in before.items()):
        raise ValueError("The merged vault changed pre-existing MW collections.")
    compile_emitters(source, ambient, prepared=prepared, compiled_rows=compiled_rows)
    for name in VAULT_NAMES:
        if not 0 < (merged / name).stat().st_size <= 16 * 1024**2:
            raise ValueError(f"Invalid merged MW vault file: {name}")
    return baseline, source_rows, native_rows, compiled_rows, placements, roots, {
        "sourceSubstitutions": substitutions, "sourceAttributesRecovered": recovered,
        "join": join, "recipe": recipe, "attributeNamespace": namespace,
    }


def build_pursuit(work, source, master, stream, model_index, w2c, mw, references, carbon, tool, progress):
    checked_inputs(carbon)
    paths = native_references(mw, references)
    work.mkdir()
    native = work / "mw-native"
    native_master_ir = work / "mw-master"
    progress("MW原本とパースートブレーカーの物理属性を照合", 52)
    import_native_mw(paths, native, work)
    import_native_mw({role: paths[role] for role in ("mw-master-pristine", "mw-globalb-pristine")},
                     native_master_ir, work)
    native_master = paths["mw-master-pristine"]
    baseline, carbon_rows, native_rows, compiled, placements, roots, report = merge_vault(
        tool, w2c, carbon, mw, master, native_master, source, native, paths["mw-globalb-pristine"], work)
    namespace = report["attributeNamespace"]
    progress("当たり判定・破片・専用カメラを生成", 58)
    geometry = compile_carbon_prop_payload(source, native, native_master_ir, carbon_rows, native_rows,
                                           compiled, placements, model_index, stream,
                                           paths["mw-globalb-pristine"], paths["mw-stream-pristine"], work, namespace=namespace)
    banks, sequence_report = compile_carbon_sequences(
        carbon, master, native_master, carbon_rows, compiled, roots, work / "camera-source", namespace=namespace)
    for key, value in banks.items():
        (work / key).write_bytes(value if isinstance(value, bytes)
                                 else json.dumps(value, separators=(",", ":")).encode("utf-8"))
    (work / "vault-base.json").write_text(json.dumps(baseline, sort_keys=True), encoding="utf-8")
    restored = {(r["section"], r["instance"]) for r in map(
        json.loads, (work / "props/records.jsonl").read_text(encoding="utf-8").splitlines())}
    if len(restored) != geometry["props"]["convertedPlacements"]:
        raise ValueError("Duplicate native pursuit placement.")
    summary = {"placements": len(restored), "sequences": sequence_report["sequenceAliases"],
               "cameras": len(sequence_report["cameras"]), "missingPlacements": geometry["props"]["unresolvedPlacements"],
               "sourceAttributesRecovered": report["sourceAttributesRecovered"]}
    (work / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
    (work / "pursuit-report.json").write_text(json.dumps(
        {**report, "geometry": geometry, "sequences": sequence_report}, indent=2), encoding="utf-8")
    return restored
