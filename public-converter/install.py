"""Verify and install a generated world with a recoverable commit."""

import argparse
from contextlib import contextmanager
import csv
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import NamedTemporaryFile, TemporaryDirectory
from zipfile import BadZipFile, ZipFile
import yaml

from preflight import (
    ASI_FOLDERS, CATALOG, SUNSET_ASI, SUNSET_CONFIG, SUNSET_FILES, SUNSET_PACK, TEXWIZARD_ASI, TEXWIZARD_CONFIG,
    check_target, checked_folder, file_digest, has_sunset_pack, runtime_rows, sunset_resource_valid,
    sunset_config, sunset_row, texwizard_settings,
)
from package_layout import CORE, RULES, STREAM, target_core


CONTROL = "W2CMWPublic"
ASI = "W2CMWPublic.asi"
MANIFEST = f"{CONTROL}/installation.json"
SETTINGS = f"{CONTROL}/runtime.ini"
ROLLBACK_EXE = f"{CONTROL}/W2CMW-Rollback.exe"
ROLLBACK_RECEIPT = f"{CONTROL}/last-rollback.json"
ROLLBACK_SOURCE = (
    Path(__file__).with_name("bin") if getattr(sys, "frozen", False)
    else Path(__file__).resolve().parents[1] / "evidence/public-converter-2.0.0/desktop-build/rollback"
) / "W2CMW-Rollback.exe"
FILES_TO_COMMIT = (*CORE, STREAM, SETTINGS, ASI, ROLLBACK_EXE, ROLLBACK_RECEIPT, MANIFEST)
SUNSET_TO_COMMIT = (*SUNSET_FILES, TEXWIZARD_CONFIG, TEXWIZARD_ASI)
SUNSET_TRANSACTION = f"{CONTROL}/sunset-transaction.json"
DEFAULT_INI = b"[Debug]\r\nLogging=0\r\n\r\n[Renderer]\r\nFrameTelemetry=0\r\nProfilePresent=0\r\n"
MIN_FREE_BYTES = 50 * 1024**3
# These previews use the same verified world format. Install the converter's
# current ASI after validating the selected package's own manifest and assets.
UPGRADEABLE_PREVIEWS = {
    "cd8af723b1e266edf612d26064dcb13c230c6a33319df61db91e1a987af698d8",
    "b8cd1845237a56e0e9d97a1d7b41eafac1a47563e995caf1e44ff93d52bc108a",
    "ee18686aec1e035d62124927de08344eb59ac526a0a6a94c6909e87df42e8e1b",
    "aeb0a7765303dd51a50c839bbeb5137990c13472e698d0b799e390a6eb8eac1f",
    "4575d0fc495978b91dbeab3c8f17143d17869531db99486ecb9dc44aa1ef602f",
    "1476d4dbef3fa47abdbc031524744e1d03d71c661d724be1c86c36db0ca5f1bd",
    "1fb868a2ae5991f42b13c094ef4e6501015e146bfda00115a7d807bb8da9b4b3",
    "de2b6c00c7d0e793ac9d1c41a31c87cee915535035c6a1b391988b37dbe3158f",
    "fa89c7ca9c1c01941f5e0404ea1a5e2d774d75f409cd21c24cba47e05cb9c838",
    "d4b43bef6b077a18476fd4a14e196a3525dcac828ac962fc0e757047d039f8a1",
    "6170621c0a0ded7103a586aa0dcaebf12069198a9a621da4cbebd3ce402bc7ef",
    "754f812e7eb701a1a876891da97fbd456ffc7d3140fa98b7b9a426f0777ba3c3",
}


def under(root, name):
    path = root
    for part in name.split("/"):
        path /= part
        if path.is_symlink() or path.is_junction():
            raise ValueError(f"Reparse point in target path: {path}")
    return path


def sha(path):
    return file_digest(path).lower() if path.is_file() else None


def require_game_closed():
    result = subprocess.run(["tasklist.exe", "/FI", "IMAGENAME eq speed.exe", "/FO", "CSV", "/NH"],
                            check=True, capture_output=True, encoding="oem", errors="replace",
                            creationflags=subprocess.CREATE_NO_WINDOW)
    if any(row and row[0].casefold() == "speed.exe" for row in csv.reader(result.stdout.splitlines())):
        raise ValueError("Close Most Wanted before applying or recovering an installation.")


def unplaced_package_present(mw):
    return (under(mw, "package.json").is_file() and under(mw, ASI).is_file()
            and not under(mw, MANIFEST).exists())


def checked_package(package, *, in_game=False):
    from build_world import (AMBIENT_TEXTURES, MW_PROP_TEXTURES, MW_UTILITY_TEXTURES, PACKAGE_FILES,
                             RUNTIME_ASI, STOCK_BARRIER_BANKS, Reader)
    from pursuit import PROP_PAYLOAD, PROP_TEXTURES, PURSUIT_FEATURE, PURSUIT_FILES, SEQUENCE_PAYLOAD, VAULT_NAMES

    if in_game and not unplaced_package_present(package):
        raise ValueError("導入先に未配置の変換済みパックが見つかりません。")
    manifest = json.loads(under(package, "package.json").read_bytes())
    if (manifest.get("schema") != 1 or manifest.get("packageId") != CONTROL
            or manifest.get("quality") not in CATALOG["qualities"]
            or not re.fullmatch("[0-9a-f]{64}", manifest.get("buildId", ""))):
        raise ValueError("Invalid generated package manifest.")
    rows = manifest["files"]
    names = {row["path"] for row in rows}
    allowed = set(PACKAGE_FILES)
    if MW_PROP_TEXTURES in names:
        allowed.add(MW_PROP_TEXTURES)
    if MW_UTILITY_TEXTURES in names:
        allowed.add(MW_UTILITY_TEXTURES)
    if AMBIENT_TEXTURES in names:
        allowed.add(AMBIENT_TEXTURES)
    if names.intersection(PURSUIT_FILES):
        allowed.update(PURSUIT_FILES)
    if len(rows) != len(allowed) or names != allowed:
        raise ValueError("Unexpected generated package file list.")
    # An explicitly selected game folder also contains MW's own files.
    # Only the exact manifest allowlist is read from that folder.
    if not in_game:
        actual_names = set()
        for directory, subdirectories, files in os.walk(package):
            parent = Path(directory)
            for name in subdirectories:
                path = parent / name
                if path.is_symlink() or path.is_junction():
                    raise ValueError(f"Reparse point in generated package: {path.name}")
            actual_names.update((parent / name).relative_to(package).as_posix() for name in files)
        if actual_names != names | {"package.json"}:
            raise ValueError("Generated package contains an undeclared or missing file.")
    for row in rows:
        path = under(package, row["path"])
        if path.stat().st_size != row["bytes"] or sha(path) != row["sha256"]:
            raise ValueError(f"Generated file fingerprint differs: {row['path']}")
    if sha(under(package, ASI)) not in {sha(RUNTIME_ASI), *UPGRADEABLE_PREVIEWS}:
        raise ValueError("Generated package uses a different runtime from this converter.")
    reader = Reader(package / "World")
    try:
        world = json.loads(reader.read(1, "world-definition"))
        if (manifest["buildId"] != reader.build.hex() or manifest["quality"] != world["quality"]
                or world["runtime"] != CONTROL or world["gameNativeClosureComplete"] is not True):
            raise ValueError("Generated package world does not match its installation manifest.")
        for name in CORE:
            resource = under(package, "Core/" + name)
            key = "native-core-digest/" + name
            if reader.contains(63, key):
                matches = not reader.contains(63, "native-core/" + name) and reader.read(63, key).hex() == sha(resource)
            else:
                matches = reader.read(63, "native-core/" + name) == resource.read_bytes()
            if not matches:
                raise ValueError(f"Generated native resource differs from its world: {name}")
        manifest["targetCore"] = (
            target_core(json.loads(reader.read(61, "native/mw-target-core")))
            if reader.contains(61, "native/mw-target-core") else RULES["mwInputs"])
        if reader.read(61, "native/original-map-textures") != under(package, "Native/original-map-textures.bin").read_bytes():
            raise ValueError("Generated radar resource differs from its world.")
        if (not reader.read(61, "native/mw-tolls").startswith(b"W2TOLL1\0")
                or reader.read(61, "native/mw-toll-textures") != under(package, "Native/mw-toll-textures.bin").read_bytes()):
            raise ValueError("Generated MW toll resources differ from their world.")
        if ("mw-native-spikes" in world["features"]) != (MW_UTILITY_TEXTURES in names):
            raise ValueError("Incomplete MW spike-strip resources.")
        if MW_UTILITY_TEXTURES in names:
            if (not reader.read(61, "native/utility-models").startswith(b"\x00\x40\x13\x80")
                    or reader.read(61, "native/utility-textures") != under(package, MW_UTILITY_TEXTURES).read_bytes()):
                raise ValueError("Generated spike-strip resources differ from their world.")
        if "world-ambient-effects" in world["features"]:
            if not reader.read(51, "environment/emitters").startswith(b"WME2"):
                raise ValueError("Invalid W2C ambient effect placement bank.")
        if AMBIENT_TEXTURES in names:
            if ("world-ambient-effects" not in world["features"] or PURSUIT_FEATURE not in world["features"]
                    or reader.read(61, "native/ambient-textures") != under(package, AMBIENT_TEXTURES).read_bytes()):
                raise ValueError("Generated ambient textures differ from their world.")
        has_fallback = "mw-prop-fallback" in world["features"]
        if has_fallback != (MW_PROP_TEXTURES in names):
            raise ValueError("Generated MW prop fallback is missing a declared resource.")
        if has_fallback and (not reader.read(61, "native/mw-prop-fallback").startswith(b"W2MPROP\0")
                             or reader.read(61, "native/mw-prop-fallback-textures") != under(package, MW_PROP_TEXTURES).read_bytes()):
            raise ValueError("Generated MW prop fallback resources differ from their world.")
        has_pursuit = PURSUIT_FEATURE in world["features"]
        if has_pursuit != (PROP_TEXTURES in names) or has_pursuit and has_fallback:
            raise ValueError("Unexpected combination of pursuit and fallback prop resources.")
        if "palmont-neighborhood-names" in world["features"]:
            districts = reader.read(51, "environment/palmont-districts")
            if not has_pursuit or len(districts) != 10916 or districts[:12] != b"W2PD\x01\0\0\0\x16\0\0\0":
                raise ValueError("Generated Palmont neighborhoods are invalid.")
        if "mw-stock-barriers" in world["features"]:
            for key in STOCK_BARRIER_BANKS:
                if not reader.read(61, "native/" + key):
                    raise ValueError(f"Missing MW stock barrier resource: {key}")
            for alias in ("4245fe23", "c9046f25"):
                if not reader.read(32, "native-barrier-texture/" + alias).startswith(b"DDS "):
                    raise ValueError("Invalid MW stock barrier texture.")
        if has_pursuit:
            for key in (*PROP_PAYLOAD, *SEQUENCE_PAYLOAD):
                if not reader.read(61, "native/" + key):
                    raise ValueError(f"Missing pursuit resource: {key}")
            baseline = json.loads(reader.read(61, "native/pursuit-vault-base"))
            if (not isinstance(baseline, dict) or baseline.keys() != set(VAULT_NAMES)
                    or not all(isinstance(value, str) and re.fullmatch("[0-9a-f]{64}", value)
                               for value in baseline.values())):
                raise ValueError("Invalid pursuit vault baseline.")
            if reader.read(61, "native/prop-textures") != under(package, PROP_TEXTURES).read_bytes():
                raise ValueError("Pursuit textures differ from their world.")
            for name in VAULT_NAMES:
                if reader.read(62, "native-vault/" + name) != under(package, f"Vault/{name}").read_bytes():
                    raise ValueError(f"Pursuit vault differs from its world: {name}")
    finally:
        reader.close()
    return manifest


def replace_file(target, value, *, expected_sha=None):
    target.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(dir=target.parent, prefix=".w2c-", delete=False) as pending:
        temp = Path(pending.name)
        try:
            if isinstance(value, Path):
                with value.open("rb") as source:
                    shutil.copyfileobj(source, pending, length=4 * 1024**2)
            else:
                pending.write(value)
            pending.flush()
            os.fsync(pending.fileno())
            if expected_sha is not None:
                pending.seek(0)
                if hashlib.file_digest(pending, "sha256").hexdigest() != expected_sha:
                    raise ValueError(f"Staged generation fingerprint differs: {target.name}")
            pending.close()
            os.replace(temp, target)
        finally:
            pending.close()
            temp.unlink(missing_ok=True)


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


@contextmanager
def installer_lock(mw):
    lock = under(mw, f"{CONTROL}/install.lock")
    lock.parent.mkdir(exist_ok=True)
    with lock.open("a+b") as stream:
        stream.seek(0)
        try:
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise ValueError("An installation operation is already in progress.") from error
        try:
            yield
        finally:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def transaction_paths(sunset):
    if sunset:
        return SUNSET_TRANSACTION, f"{CONTROL}/sunset-rollback", SUNSET_TO_COMMIT
    return f"{CONTROL}/transaction.json", f"{CONTROL}/rollback", FILES_TO_COMMIT


def recover(mw, *, sunset=False):
    journal_name, rollback, allowed = transaction_paths(sunset)
    journal = under(mw, journal_name)
    if not journal.is_file():
        return False
    require_game_closed()
    rows = json.loads(journal.read_bytes())
    if not isinstance(rows, list) or not 0 < len(rows) <= len(allowed):
        raise ValueError("Invalid installation journal.")
    names = [row["path"] for row in rows]
    if len(names) != len(set(names)) or any(name not in allowed for name in names):
        raise ValueError("Invalid installation journal paths.")
    for row in rows:
        name = row["path"]
        target = under(mw, name)
        backup = under(mw, f"{rollback}/{name}")
        actual = sha(target)
        if actual not in (row["before"], row["after"]):
            raise ValueError(f"Recovery stopped because the destination was edited: {name}")
        if row["before"] is not None and sha(backup) != row["before"]:
            raise ValueError(f"Recovery backup fingerprint differs: {name}")
    for row in reversed(rows):
        name = row["path"]
        target = under(mw, name)
        if sha(target) != row["before"]:
            if row["before"] is None:
                target.unlink()
            else:
                replace_file(target, under(mw, f"{rollback}/{name}"))
        if sha(target) != row["before"]:
            raise ValueError(f"Recovery verification failed: {name}")
    journal.unlink()
    return True


def commit_changes(mw, changes, *, sunset=False, before_commit=lambda _: None):
    journal_name, rollback, allowed = transaction_paths(sunset)
    if any(name not in allowed for name in changes):
        raise ValueError("Unexpected installation paths.")
    rows = []
    for name, content in changes.items():
        path = under(mw, name)
        before = sha(path)
        if isinstance(content, Path):
            after = sha(content)
            if after is None:
                raise ValueError(f"Missing installation source: {name}")
        else:
            after = None if content is None else hashlib.sha256(content).hexdigest()
        if before == after:
            continue
        if path.exists() and before is None:
            raise ValueError(f"Expected a regular file at {name}")
        if before is not None:
            replace_file(under(mw, f"{rollback}/{name}"), path)
        rows.append({"path": name, "before": before, "after": after})
    if not rows:
        return []
    journal = under(mw, journal_name)
    require_game_closed()
    replace_file(journal, json_bytes(rows))
    try:
        for row in rows:
            require_game_closed()
            name = row["path"]
            target = under(mw, name)
            if sha(target) != row["before"]:
                raise ValueError(f"Target file changed during placement: {name}")
            before_commit(name)
            if changes[name] is None:
                target.unlink()
            else:
                replace_file(target, changes[name])
            if sha(target) != row["after"]:
                raise ValueError(f"Placement fingerprint differs: {name}")
        journal.unlink()
    except BaseException:
        recover(mw, sunset=sunset)
        raise
    return [row["path"] for row in rows]


def stage_sunset_source(source, stage, names):
    if source is None:
        return {}
    files = {}
    budget = 160 * 1024**2
    if source.is_dir():
        for name in names:
            candidates = [p for prefix in ("", "SunSet/", "TexWizard_MW/")
                          if (p := under(source, prefix + name)).is_file()]
            if len(candidates) != 1 or not 0 < candidates[0].stat().st_size <= budget:
                raise ValueError(f"指定したMODのフォルダーに必要なファイルがありません: {name}")
            budget -= candidates[0].stat().st_size
            dest = stage / name
            replace_file(dest, candidates[0])
            files[name] = dest
        return files
    with ZipFile(source) as archive:
        members = archive.infolist()
        for name in names:
            accepted = {prefix.casefold() + name.casefold() for prefix in ("", "SunSet/", "TexWizard_MW/")}
            matches = [entry for entry in members if entry.filename.casefold() in accepted]
            if (len(matches) != 1 or not 0 < matches[0].file_size <= budget
                    or matches[0].external_attr >> 28 == 0xA):
                raise ValueError(f"指定したMODのZIPに必要なファイルがありません: {name}")
            budget -= matches[0].file_size
            dest = stage / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(matches[0]) as stream, dest.open("xb") as out:
                shutil.copyfileobj(stream, out, length=1024**2)
            files[name] = dest
    return files


def install_sunset(mw, sunset_source=None, texwizard_source=None, *, reduce_existing_glare=False):
    target = check_target(mw)
    if target["status"] != "ok":
        raise ValueError(target["detail"])
    require_game_closed()
    catalog = CATALOG["recommendedSunSet"]
    windows = Path(os.environ.get("WINDIR", r"C:\Windows"))
    with installer_lock(mw), TemporaryDirectory(prefix=".sunset-", dir=under(mw, CONTROL)) as workspace:
        recover(mw)
        recover(mw, sunset=True)
        if shutil.disk_usage(mw).free < MIN_FREE_BYTES + 384 * 1024**2:
            raise ValueError("SunSetの配置には50 GiBの予備領域に加えて384 MiBの空きが必要です。")
        stage = Path(workspace)
        supplied = stage_sunset_source(sunset_source, stage, SUNSET_FILES)
        supplied.update(stage_sunset_source(texwizard_source, stage, (TEXWIZARD_ASI,)))
        for name, expected in ((SUNSET_ASI, catalog["sha256"]), (TEXWIZARD_ASI, catalog["texWizardSha256"])):
            if name in supplied and file_digest(supplied[name]) != expected:
                raise ValueError(f"検証済みの公式配布ファイルと一致しません: {name}")
        for name, path in supplied.items():
            if not sunset_resource_valid(path, name):
                raise ValueError(f"指定したMODの資源を確認できません: {name}")
        overlay = {}
        for name, source in supplied.items():
            variants = ([folder + Path(name).name for folder in ASI_FOLDERS] if name in (SUNSET_ASI, TEXWIZARD_ASI)
                        else [name])
            if not any(under(mw, variant).exists() for variant in variants):
                overlay[name] = source
        if SUNSET_CONFIG in overlay:
            config = sunset_config(overlay[SUNSET_CONFIG])
            preset = sunset_config(Path(__file__).with_name("presets") / "SunSet/Config.yml")
            for section, options in preset.items():
                if isinstance(options, dict):
                    config.setdefault(section, {}).update(options)
            overlay[SUNSET_CONFIG].write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")
        elif reduce_existing_glare:
            existing = under(mw, SUNSET_CONFIG)
            if existing.is_file() and sunset_config(existing)["Config"].get("Bloom") is not False:
                data, count = re.subn(rb"(?m)^(  Bloom:[ \t]*)true(?=[ \t#\r]*$|[ \t]+#)",
                                      rb"\g<1>false", existing.read_bytes())
                if count != 1:
                    raise ValueError("Config.yml の Config → Bloom を false に設定してから再検査してください。")
                changed = stage / "tuned-Config.yml"
                changed.write_bytes(data)
                if sunset_config(changed)["Config"].get("Bloom") is not False:
                    raise ValueError("Config.yml の Config → Bloom を確認してください。")
                overlay[SUNSET_CONFIG] = changed
        config_path = under(mw, TEXWIZARD_CONFIG)
        config = None
        if not config_path.exists():
            config = {"packs": [], "autoScan": True}
        else:
            config = texwizard_settings(config_path)
            if not config.get("autoScan", True) and not has_sunset_pack(config):
                config["packs"].append(SUNSET_PACK)
            else:
                config = None
        if config is not None:
            new_config = stage / TEXWIZARD_CONFIG
            new_config.parent.mkdir(parents=True, exist_ok=True)
            new_config.write_bytes(json_bytes(config))
            overlay[TEXWIZARD_CONFIG] = new_config
        checked = sunset_row(mw, windows, overlay)
        if checked["status"] != "ok":
            raise ValueError(checked["detail"] + "\n既存のMODファイルは上書きしていません。")
        # Place resources before enabling a newly installed ASI. Existing
        # SunSet/TexWizard files and presets are left intact.
        ordered = {name: overlay[name] for name in (*SUNSET_FILES[1:], TEXWIZARD_CONFIG, TEXWIZARD_ASI, SUNSET_ASI)
                   if name in overlay}
        return {"installed": commit_changes(mw, ordered, sunset=True)}


def copy_generation(mw, package, manifest):
    generation = f"{CONTROL}/generations/{manifest['buildId']}"
    root = under(mw, generation)
    pending = []
    for row in manifest["files"]:
        if row["path"] == ASI:
            continue
        path = under(mw, generation + "/" + row["path"])
        if sha(path) != row["sha256"]:
            pending.append((row, path))
    required = sum(row["bytes"] for row, _ in pending)
    if shutil.disk_usage(mw).free < required + 32 * 1024**2 + MIN_FREE_BYTES:
        raise ValueError("Insufficient space for the generated world and rollback while retaining 50 GiB free.")
    for row, path in pending:
        replace_file(path, under(package, row["path"]), expected_sha=row["sha256"])
    return root


def installed_world(mw):
    manifest = under(mw, MANIFEST)
    if not manifest.is_file():
        return None
    state = json.loads(manifest.read_bytes())
    if (not isinstance(state, dict) or state.get("schema") != 1 or state.get("packageId") != CONTROL
            or not re.fullmatch("[0-9a-f]{64}", str(state.get("generation", "")))
            or not re.fullmatch("[0-9a-f]{64}", str(state.get("runtimeSha256", "")))):
        raise ValueError("Unrecognized installation state.")
    if "targetCore" in state:
        target_core(state["targetCore"])
    return state


def uninstall(mw):
    """Restore the original MW world using the same journal as installation."""
    require_game_closed()
    with installer_lock(mw):
        recover(mw)
        state = installed_world(mw)
        if state is None:
            return False
        changes = {}
        baseline = state.get("targetCore", RULES["mwInputs"])
        stream_swapped = baseline[STREAM] != RULES["mwInputs"][STREAM]
        for name in (*CORE, *((STREAM,) if stream_swapped else ())):
            original = under(mw, f"{CONTROL}/originals/{name}")
            original_sha = baseline[name]
            if sha(original) != original_sha:
                raise ValueError(f"復元用の原本が見つからないか、内容が異なります。{name}")
            current = under(mw, name)
            generation = under(mw, f"{CONTROL}/generations/{state['generation']}/Core/{name}")
            installed_sha = RULES["mwInputs"][STREAM] if name == STREAM else sha(generation)
            current_sha = sha(current)
            if current_sha != original_sha and (installed_sha is None or current_sha != installed_sha):
                raise ValueError(f"配置後にファイルが変更されたため、復元を中止しました。{name}")
            changes[name] = original
        runtime = under(mw, ASI)
        if sha(runtime) not in (None, state["runtimeSha256"]) or (runtime.exists() and not runtime.is_file()):
            raise ValueError(f"配置後にファイルが変更されたため、復元を中止しました。{ASI}")
        rollback_sha = state.get("rollbackSha256")
        if rollback_sha and sha(under(mw, ROLLBACK_EXE)) == rollback_sha:
            changes[ROLLBACK_RECEIPT] = json_bytes({"rollbackSha256": rollback_sha, "language": state.get("language")})
        changes.update({ASI: None, MANIFEST: None})
        commit_changes(mw, changes)
        return True


def install(package, mw, *, language=None, mw_reference=None):
    from build_world import RUNTIME_ASI, Reader, mw_inputs
    from pursuit import PROP_TEXTURES

    target = check_target(mw)
    if target["status"] != "ok":
        raise ValueError(target["detail"])
    in_game = package == mw
    package_info = checked_package(package, in_game=in_game)
    base_core = package_info["targetCore"]
    stream_swapped = base_core[STREAM] != RULES["mwInputs"][STREAM]
    stream_source = None
    baseline = None
    if any(row["path"] == PROP_TEXTURES for row in package_info["files"]):
        reader = Reader(package / "World")
        try:
            baseline = json.loads(reader.read(61, "native/pursuit-vault-base"))
        finally:
            reader.close()

    def verify_vault():
        for name, expected in (baseline or {}).items():
            if sha(under(mw, "GLOBAL/" + name)) != expected:
                raise ValueError(f"導入先の属性データが変換時と異なります。再変換してください: GLOBAL/{name}")

    package_runtime_sha = next(row["sha256"] for row in package_info["files"] if row["path"] == ASI)
    runtime_sha = sha(RUNTIME_ASI)
    if runtime_sha is None:
        raise ValueError("The converter runtime is missing.")
    require_game_closed()
    with installer_lock(mw):
        recover(mw)
        recover(mw, sunset=True)
        deployment = list(runtime_rows(mw, Path(os.environ.get("WINDIR", r"C:\Windows")), target=target))
        blocking = [f"{row['name']}: {row['detail']} {row.get('url', '')}".strip()
                    for row in deployment if row["status"] in {"stop", "action"}]
        if blocking or under(mw, f"scripts/{ASI}").exists():
            raise ValueError("\n".join(blocking) or f"Competing scripts/{ASI} detected.")
        for row in deployment:
            if row["status"] == "warning":
                print(f"{row['name']}: {row['detail']}", file=sys.stderr)

        existing_asi = under(mw, ASI)
        previous = installed_world(mw)
        if in_game and (previous is not None or sha(existing_asi) != package_runtime_sha):
            raise ValueError("コピー済みパックの配置状態が変わりました。導入先を再検査してください。")
        previous_runtime = previous.get("runtimeSha256") if previous else None
        if (previous is None and existing_asi.exists() and not in_game) or (previous and
                sha(existing_asi) != previous_runtime):
            raise ValueError("Unexpected runtime ASI in target. Use its matching converter to recover it.")
        stream_path = under(mw, STREAM)
        if previous is not None and previous.get("targetCore", RULES["mwInputs"]) != base_core:
            raise ValueError("導入前のMW資源が変換時と異なります。再変換してください。")
        if sha(stream_path) != (base_core[STREAM] if previous is None else RULES["mwInputs"][STREAM]):
            raise ValueError(f"Target MW resource has changed: {STREAM}")
        if stream_swapped and previous is None:
            stream_source = mw_inputs(mw, mw_reference)[STREAM]
        protected_core = (*CORE, *((STREAM,) if stream_swapped else ()))
        verify_vault()
        for name in protected_core:
            path = under(mw, name)
            expected = base_core[name]
            if previous is not None:
                current = under(mw, f"{CONTROL}/generations/{previous['generation']}/Core/{name}")
                expected = RULES["mwInputs"][STREAM] if name == STREAM else sha(current)
            if expected is None or sha(path) != expected:
                raise ValueError(f"Target MW resource has changed: {name}")
            backup = under(mw, f"{CONTROL}/originals/{name}")
            if previous is not None or backup.exists():
                if sha(backup) != base_core[name]:
                    raise ValueError(f"Original MW backup fingerprint differs: {name}")
        new_generation_bytes = sum(row["bytes"] for row in package_info["files"])
        stage_budget = 64 * 1024**2 + 3 * sum(under(mw, name).stat().st_size for name in protected_core)
        generation_index = under(mw, f"{CONTROL}/generations/{package_info['buildId']}/World/L5RA.w2i")
        if shutil.disk_usage(mw).free < MIN_FREE_BYTES + stage_budget + (0 if generation_index.exists() else new_generation_bytes):
            raise ValueError("Insufficient space for the generated world and rollback while retaining 50 GiB free.")
        for name in protected_core:
            path = under(mw, name)
            original = under(mw, f"{CONTROL}/originals/{name}")
            if previous is None and not original.exists():
                replace_file(original, path, expected_sha=base_core[name])

        staged = copy_generation(mw, package, package_info)
        installation = {
            "schema": 1, "packageId": CONTROL, "generation": package_info["buildId"],
            "quality": package_info["quality"], "indexSha256": sha(staged / "World/L5RA.w2i"),
            "runtimeSha256": runtime_sha,
            "targetCore": base_core,
        }
        if language is not None:
            installation["language"] = language
        changes = {name: staged / "Core" / name for name in CORE}
        if stream_source is not None:
            changes[STREAM] = stream_source
        if not under(mw, SETTINGS).exists():
            changes[SETTINGS] = DEFAULT_INI
        changes[ASI] = RUNTIME_ASI
        if ROLLBACK_SOURCE.is_file():
            rollback_sha = sha(under(mw, ROLLBACK_EXE))
            receipt = under(mw, ROLLBACK_RECEIPT)
            retired = json.loads(receipt.read_bytes()) if receipt.is_file() else {}
            if not isinstance(retired, dict):
                raise ValueError("Unrecognized installation state.")
            if rollback_sha not in (None, sha(ROLLBACK_SOURCE), (previous or {}).get("rollbackSha256"),
                                     retired.get("rollbackSha256")):
                raise ValueError("Unexpected rollback executable in the target.")
            changes[ROLLBACK_EXE] = ROLLBACK_SOURCE
            installation["rollbackSha256"] = sha(ROLLBACK_SOURCE)
            changes[ROLLBACK_RECEIPT] = None
        elif getattr(sys, "frozen", False):
            raise ValueError("The rollback executable is missing from the converter.")
        changes[MANIFEST] = json_bytes(installation)
        def verify_runtime(name):
            if name == MANIFEST:
                if sha(existing_asi) != runtime_sha:
                    raise ValueError("Runtime ASI changed during placement.")
                if sha(stream_path) != RULES["mwInputs"][STREAM]:
                    raise ValueError("MW stream fingerprint changed during placement.")
                verify_vault()

        commit_changes(mw, changes, before_commit=verify_runtime)
        return installation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mw", required=True)
    parser.add_argument("--mw-reference")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--package")
    mode.add_argument("--recover", action="store_true")
    mode.add_argument("--setup-sunset", action="store_true")
    parser.add_argument("--sunset")
    parser.add_argument("--texwizard")
    parser.add_argument("--reduce-existing-glare", action="store_true")
    args = parser.parse_args()
    try:
        mw = checked_folder(args.mw)
        if args.recover:
            with installer_lock(mw):
                world = recover(mw)
                print(json.dumps({"recovered": recover(mw, sunset=True) or world}))
        elif args.setup_sunset:
            sunset = Path(args.sunset).resolve(strict=True) if args.sunset else None
            texwizard = Path(args.texwizard).resolve(strict=True) if args.texwizard else None
            print(json.dumps(install_sunset(mw, sunset, texwizard, reduce_existing_glare=args.reduce_existing_glare)))
        else:
            print(json.dumps(install(checked_folder(args.package), mw, mw_reference=checked_folder(args.mw_reference))))
    except (OSError, ValueError, KeyError, BadZipFile, subprocess.CalledProcessError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    main()
