"""Build a local W2C 2.0.0 world for Most Wanted."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from preflight import CATALOG, carbon_banks, check_target, checked_folder, file_at, source_rows


PROJECT = Path(__file__).resolve().parents[1]
SOURCE_COMPILER = PROJECT / "compiler/source-world/bin/Release/net11.0/SourceWorld.dll"
W2C_BANKS = {
    "w2c-emitter-textures": "END_FILES/GLOBALB/EMITTER_SYSTEM_TEXTURE_PAGE.BIN",
    "w2c-emitter-normals": "END_FILES/GLOBALB/EMITTER_SYSTEM_NORMALMAPS_PAGE.BIN",
    "w2c-ingame-textures": "END_FILES/INGAMEA/INGAMETEXTURES.BIN",
    "w2c-particle-textures": "END_FILES/INGAMEB/PARTICLETEXTURES.BIN",
}


def source_paths(w2c, quality):
    rows = list(source_rows(w2c, quality))
    failures = [row["name"] for row in rows if row["status"] != "ok"]
    if failures:
        raise ValueError("W2C 2.0.0 source mismatch: " + ", ".join(failures))
    files = {Path(row["path"]).name: file_at(w2c, row["path"]) for row in CATALOG["qualities"][quality]}
    return files["L5RA.BUN"], files["STREAML5RA.BUN"]


def import_original(w2c, quality, workspace, *, carbon=None):
    master, stream = source_paths(w2c, quality)
    roles = {"world-stream": stream, **{role: file_at(w2c, name) for role, name in W2C_BANKS.items()}, "master": master}
    workspace.mkdir(parents=True)
    config = workspace / "input.json"
    files = [{"role": role, "path": str(path), "import": True} for role, path in roles.items()]
    files.extend({"role": role, "path": str(path), "import": True, "missingTextureFallbacks": textures}
                 for role, path, textures in carbon_banks(carbon))
    config.write_text(json.dumps({"sourceVariant": quality, "files": files}, ensure_ascii=False), encoding="utf-8")
    from setup_sourceworld import importer_command
    command = importer_command()
    subprocess.run([*command, "export", str(config), str(workspace / "source")], check=True,
                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))


def convert(w2c, mw, quality, output, work=None, *, carbon=None, vault_tool=None, mw_reference=None,
            progress=lambda *_: None):
    from build_world import assemble_world, build_assets, mw_inputs, mw_target_base, file_digest

    w2c, mw, carbon, vault_tool, mw_reference = (
        path.resolve() if path is not None else None for path in (w2c, mw, carbon, vault_tool, mw_reference))

    target = check_target(mw)
    if target["status"] != "ok":
        raise ValueError(target["detail"])
    mw_inputs(mw, mw_reference)
    target_fingerprints = {name: file_digest(path).lower() for name, path in mw_target_base(mw).items()}
    if not output.parent.is_dir() or output.exists():
        raise ValueError("Choose a new output folder inside an existing directory.")
    inputs = tuple(path for path in (w2c, mw, carbon, vault_tool, mw_reference) if path is not None)
    if any(output.is_relative_to(root) for root in inputs):
        raise ValueError("Choose an output folder outside the original game and mod directories.")
    if work is not None and (work.exists() or any(work.is_relative_to(root) for root in (*inputs, output))):
        raise ValueError("Choose a new workspace outside the input and output directories.")
    with TemporaryDirectory(prefix=".w2c-conversion-", dir=output.parent) as folder:
        staging = Path(folder)
        workspace = work or staging / "build"
        work_volume = next(parent for parent in workspace.parents if parent.is_dir())
        available = shutil.disk_usage(work_volume).free
        same_volume = work_volume.anchor.casefold() == output.anchor.casefold()
        # Exported IR, compiled assets and the staged package coexist only on
        # a shared volume. A separately selected workspace needs less space.
        workspace_gib = 12 if same_volume else 8
        if available < (50 + workspace_gib) * 1024**3 or shutil.disk_usage(output.parent).free < 55 * 1024**3:
            raise ValueError("Insufficient workspace to retain the validated package and 50 GiB free.")
        progress("原本を読み込む", 10)
        import_original(w2c, quality, workspace, carbon=carbon)
        progress("原本のモデル・テクスチャを照合", 25)
        build_assets(workspace, quality, progress, w2c=w2c, mw=mw, carbon=carbon, vault_tool=vault_tool,
                     mw_reference=mw_reference)
        package = (workspace if work is not None else staging) / "package"
        progress("ワールドパックを組み立てて検証", 80)
        result = assemble_world(workspace, w2c, quality, mw, package, mw_reference=mw_reference,
                                target_fingerprints=target_fingerprints)
        report = json.loads((workspace / "conversion-result.json").read_bytes())
        result["resourceSummary"] = {
            "carbonUsed": report["carbonUsed"],
            "carbonTextures": report["carbonTextures"],
            "missingTerrainTextures": report["missingTextures"],
            "missingStaticPlacements": len(report["missingPlacements"]),
            "unconvertedDynamicPropPlacements": report["nativePropPlacementsPending"],
            "visualPropPlacements": report["visualPropPlacements"],
            "mwFallbackProps": report["mwFallbackProps"]["restoredPlacements"],
            "mwTolls": report["mwTolls"],
            "nightLighting": report["nightLighting"],
            "worldEffects": report["worldEffects"],
            "worldMotions": report["worldMotions"],
            "pursuit": report["pursuit"],
            "carbonPursuitOmission": report["carbonPursuitOmission"],
            "pendingFeatures": report["pendingFeatures"],
        }
        from components import prepare
        manifest_path = package / "package.json"
        manifest = json.loads(manifest_path.read_bytes())
        manifest["converterVersion"] = "RC6"
        manifest["optionalFiles"] = prepare(package)
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
        progress("検証済みパックを保存", 95)
        os.replace(package, output)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--w2c", required=True)
    parser.add_argument("--quality", required=True, choices=tuple(CATALOG["qualities"]))
    parser.add_argument("--mw", required=True, help="Most Wanted game directory with supported MW resources")
    parser.add_argument("--mw-reference", help="Unmodified MW game used as a read-only resource source")
    parser.add_argument("--carbon", help="Optional Carbon directory with supported original textures")
    parser.add_argument("--vault-tool", help="Optional local OGVI 1.4 / Attribulator 2.0 directory for Carbon pursuit assets")
    parser.add_argument("--output", required=True, type=Path, help="New folder for the converted package")
    parser.add_argument("--work", type=Path, help="Optional new workspace to retain diagnostics")
    parser.add_argument("--install", action="store_true", help="Apply the verified package to the selected MW installation")
    args = parser.parse_args()
    try:
        w2c = checked_folder(args.w2c)
        mw = checked_folder(args.mw)
        output = args.output.resolve()
        result = convert(w2c, mw, args.quality, output, args.work.resolve() if args.work else None,
                         carbon=checked_folder(args.carbon), vault_tool=checked_folder(args.vault_tool),
                         mw_reference=checked_folder(args.mw_reference))
        print(json.dumps({"exported": str(output), "buildId": result["buildId"],
                          "resources": result["resourceSummary"]}, ensure_ascii=False), flush=True)
        if args.install:
            from install import install
            print(json.dumps({"installed": install(output, mw, mw_reference=checked_folder(args.mw_reference))}))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    main()
