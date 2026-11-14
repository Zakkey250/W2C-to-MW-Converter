"""Compile pursuit attributes, sequences and cameras from local source inputs."""

import copy
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from attributes_compile import compile_attributes
from carbon_camera_plan import plan_breaker_cameras
from native_models import compile_models
from native_textures import main as compile_textures
from mw_attribute_hash import attribute_hash
from prepare_public_breaker_inputs import checked_inputs
from prop_compile import compile_prop_assets
from sequence_plan import plan_sequence_assets
from sequences_compile import compile_sequence_assets
from source_systems import pair_props


def adapt_original_prop_rows(carbon_rows, mw_rows):
    """Use explicit MW templates only for known gaps in the original vault."""
    source = copy.deepcopy(carbon_rows)
    native = [r for r in mw_rows if not r["name"].startswith("w2mw_")]
    by_key = {(r["class"], r["name"]): r for r in source}
    by_native = {(r["class"], r["name"]): r for r in native}
    substitutions = []
    for name, template, hash_value in (
        ("cone_large", "cone", "09CE7073"), ("shopping_cart", "rack", "8CCA5D63"),
        ("wheelbarrow", "rack", "5940F4D3"), ("generator", "mtl_box", "263C5311"),
    ):
        if ("smackable", name) in by_key:
            continue
        row = copy.deepcopy(by_native["smackable", template])
        row.update(name=name, hash=hash_value, parent=template)
        row["fields"]["CollectionName"] = name
        source.append(row)
        by_key["smackable", name] = row
        substitutions.append({"name": name, "nativeTemplate": template})

    row = by_key.get(("smackable", "gettymonumentstrut"))
    missing = "rigidbodyspecs -> 0x9C0A7DEE"
    if row and row["fields"].get("rigidbodyspecs") == missing and ("rigidbodyspecs", "0x9C0A7DEE") not in by_key:
        seen = set()
        parent = row["parent"]
        while parent and parent not in seen:
            seen.add(parent)
            ancestor = by_key["smackable", parent]
            if "rigidbodyspecs" in ancestor["fields"]:
                inherited = ancestor["fields"]["rigidbodyspecs"]
                row["fields"]["rigidbodyspecs"] = inherited
                substitutions.append({"name": row["name"], "inheritedRigidBody": inherited})
                break
            parent = ancestor["parent"]
        else:
            raise ValueError("The source monument's parent has no valid rigid body.")
    return source, native, substitutions


def compile_carbon_pursuit_recipe(carbon, mw_master, source_rows, native_rows, roots, output, restore_missing, *, namespace="w2v2_"):
    """Emit attributes for both the active placements and sequencer children."""
    source, provenance = checked_inputs(carbon)
    track = mw_master.read_bytes()
    expected_gap = {"owner": "metal_gate", "engine": "metalgate_seq",
                    "kind": "original child collection absent", "hash": "0a8471ec"}
    while True:
        plan = plan_sequence_assets(source["sequences.bun"], track, source_rows, native_rows, roots, namespace=namespace)
        gaps = plan["closure"]["missingOriginalReferences"]
        if gaps == [expected_gap]:
            break
        missing = [gap for gap in gaps if gap != expected_gap]
        if not missing or any(gap["kind"] != "original child collection absent" for gap in missing):
            raise ValueError("Carbonの破片演出を照合できません。対応するCarbon原本を指定してください。")
        restore_missing({gap["hash"] for gap in missing})
    names = sorted(set(roots) | set(plan["closure"]["smackables"]))
    attributes = compile_attributes(output, native_rows, source_rows, names, namespace=namespace)
    return {"attributes": attributes, "originalInputs": provenance, "originalGaps": [expected_gap],
            "placementRoots": len(set(roots)), "sequenceAliases": plan["engineAliases"]}


def join_carbon_pursuit_placements(source, native, master, output):
    """Derive the required attribute roots from the selected source quality."""
    report = pair_props(source, native, master, output)
    with (Path(output) / "prop-source-join.jsonl").open(encoding="utf-8") as stream:
        return list(map(json.loads, stream)), report


def original_prop_recipe(text, missing, available):
    """Select missing collections from the verified W2C VltEd script."""
    commands = [line.split() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    parents = {parts[3]: parts[2] for parts in commands
               if len(parts) == 4 and parts[:2] == ["add_node", "smackable"]}
    names = {f"{attribute_hash(name):08x}": name for name in parents}
    if unsupported := missing - names.keys():
        raise ValueError("Carbonの物理属性が不足し、W2C原本から復元できません。"
                         "対応するCarbon原本を指定してください。属性ID: " + ", ".join(sorted(unsupported)))
    selected = {names[key] for key in missing}
    for name in list(selected):
        parent = parents[name]
        while parent not in available and parent not in selected:
            if parent not in parents:
                raise ValueError(f"Carbonに必要な親属性が見つかりません。smackable/{parent}")
            selected.add(parent)
            parent = parents[parent]
    result = ["game C"]
    for parts in commands:
        if len(parts) < 4 or parts[1] != "smackable":
            continue
        action = parts[0]
        if action == "add_node" and parts[3] in selected:
            result.append(" ".join(parts))
        elif action in ("add_field", "update_field") and parts[2] in selected:
            if len(parts) >= 6 and action == "update_field":
                if match := re.fullmatch(r"(Surface|Effect)(ClassKey|CollectionKey)", parts[4]):
                    parts[4:5] = match.groups()
                elif parts[3] == "rigidbodyspecs" and parts[4] in ("Class", "Collection"):
                    parts[4] += "Key"
            result.append(" ".join(parts))
    return "\n".join(result) + "\n", {key: name for key, name in names.items() if name in selected}


def compile_carbon_prop_payload(source, native, native_master_ir, source_rows, native_rows,
                                compiled_rows, placements, raw_index, raw_stream, mw_global,
                                mw_stream, work, *, namespace="w2v2_"):
    """Rebuild collision, fragment trees, and their model/texture closure locally."""
    work = Path(work)
    props, models, textures = (work / name for name in ("props", "models", "textures"))
    report = compile_prop_assets(source, native, native_master_ir, source_rows, native_rows,
                                 compiled_rows, placements, props, namespace=namespace)
    if report["unresolvedPlacements"]:
        unresolved = json.loads((props / "unresolved.json").read_text(encoding="utf-8"))
        known = {("1297:71", "219e3f87"), ("1398:106", "219e3f87")}
        actual = {(row["sourcePlacementId"], row["solid"]) for row in unresolved}
        if (len(actual) != len(unresolved) or actual - known or any(
                row["reason"] != "Original display model absent; no verified exact native fallback"
                for row in unresolved)):
            raise ValueError("Unexpected unresolved Carbon prop assets; see the local unresolved ledger.")
    compile_models(source, native, props, raw_index, raw_stream, mw_global, models, mw_stream)
    compile_textures(source, native, models, mw_global, textures)
    return {"props": report,
            "models": json.loads((models / "native-model-summary.json").read_text(encoding="utf-8")),
            "textures": json.loads((textures / "native-texture-summary.json").read_text(encoding="utf-8"))}


def decode_camera_assets(executable, original_cameras, master, scene_plan, work):
    plan = work / "camera-plan.json"
    plan.write_text(json.dumps(scene_plan), encoding="utf-8")
    output = work / "decoded"
    env = None
    if getattr(sys, "frozen", False):
        # Unicorn's longjmp is incompatible with the Windows PyInstaller bootloader's CFG.
        decoder = Path(sys._MEIPASS) / "camera-decoder"
        command = [decoder / "python.exe", "-I", "-B", decoder / "cameras_compile.py"]
        if not command[0].is_file():
            raise ValueError("専用カメラ変換ツールが見つかりません。修正版ZIPをフォルダーごと展開してください。")
        env = dict(os.environ, LIBUNICORN_PATH=str(decoder / "unicorn/lib"))
    else:
        script = Path(__file__).resolve().parents[1] / "compiler/source-runtime/cameras_compile.py"
        command = [sys.executable, "-B", script]
    command += ["--executable", executable, "--ingame", original_cameras, "--track", master,
                "--scene-plan", plan, "--output", output]
    process = subprocess.run([str(part) for part in command], cwd=work, env=env, capture_output=True,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), timeout=480)
    if process.returncode:
        details = process.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(f"専用カメラの変換に失敗しました (0x{process.returncode & 0xffffffff:08X})。\n{details[-1400:]}")
    report = json.loads((output / "moment-build-report.json").read_bytes())
    return ((output / "carbon-moment-cameras.bun").read_bytes(),
            (output / "carbon-moment-markers.bin").read_bytes(), report["scenes"])


def compile_carbon_sequences(carbon, master, mw_master, source_rows, target_rows, roots, work, *, namespace="w2v2_"):
    """Return the runtime assets after checking the compiled MW attribute closure."""
    extracted, provenance = checked_inputs(carbon)
    sequence_bank = extracted["sequences.bun"]
    native_track = mw_master.read_bytes()
    plan = plan_sequence_assets(sequence_bank, native_track, source_rows, target_rows, roots, namespace=namespace)
    missing = plan["missingConvertedChildParameters"]
    if missing:
        raise ValueError("Missing MW pursuit attributes: " + ", ".join(missing))
    scene_plan, camera_source = plan_breaker_cameras(
        sequence_bank, plan["engineRenames"],
        (("carbon-ice", extracted["cameras-ingameb.bun"]),
         ("mw-native-keys", extracted["cameras-ingamec.bun"])),
        master.read_bytes(),
    )
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    original_cameras = work / "original-camera-groups.bun"
    with original_cameras.open("xb") as stream:
        stream.write(camera_source)
    cameras, markers, camera_report = decode_camera_assets(
        carbon / provenance["decoder"]["path"], original_cameras, master, scene_plan, work)
    sequences, routes, sequence_report, gaps = compile_sequence_assets(
        sequence_bank, native_track, source_rows, target_rows, plan, scene_plan, namespace=namespace)
    return {
        "sequences": sequences,
        "cameras": cameras,
        "camera-markers": markers,
        "sequence-routes": routes,
    }, {
        "originalInputs": provenance,
        "sequenceGroups": sequence_report,
        "sequenceAliases": len(routes["engines"]),
        "cameras": camera_report,
        "originalGaps": gaps,
    }
