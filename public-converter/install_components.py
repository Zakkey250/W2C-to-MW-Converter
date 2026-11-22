"""Optional component placement participates in the world install transaction."""

import configparser
import io
import json
import re
from pathlib import Path
from zipfile import ZipFile

from components import (
    LB_ARCHIVE, LB_SHA256, VCACHE_ARCHIVE, VCACHE_FILES, VCACHE_SHA256, bundled_vcache_files, digest, encode,
)

LB_MODULES = ("NFSMWLimitBreaker", "MWLimitBrakerX64")
VCACHE_MODULES = ("NFSMWVCacheAffinity",)


def legacy_module_paths(modules, game=None):
    from preflight import ASI_FOLDERS
    folders = list(ASI_FOLDERS)
    if game is not None:
        from install import under
        for prefix in ASI_FOLDERS:
            if prefix and under(game, prefix.rstrip("/")).is_dir():
                for child in under(game, prefix.rstrip("/")).iterdir():
                    if child.is_dir() and not child.is_symlink() and not child.is_junction():
                        folders.append(prefix + child.name + "/")
    return tuple(folder + name + extension for folder in folders
                 for name in modules for extension in (".asi", ".ini"))


def legacy_module_path(name, modules):
    """The loader scans root, plugin directories and one child level."""
    parts = name.replace("\\", "/").split("/")
    if any(part in ("", ".", "..") or ":" in part for part in parts):
        return False
    return (len(parts) <= 3 and (len(parts) == 1 or parts[0].casefold() in {"scripts", "plugins", "update"})
            and parts[-1].casefold() in {module.casefold() + ext for module in modules for ext in (".asi", ".ini")})


def component_state(game):
    from install import COMPONENT_STATE, MANIFEST, under, sha
    record = under(game, COMPONENT_STATE)
    if record.is_file():
        return json.loads(record.read_bytes())
    manifest = under(game, MANIFEST)
    installed = json.loads(manifest.read_bytes()) if manifest.is_file() else {}
    legacy = json.loads(Path(__file__).with_name("components-catalog.json").read_bytes())["legacyRoads"]
    if installed.get("generation") == legacy["generation"] and all(
            sha(under(game, name)) == expected for name, expected in legacy["files"].items()):
        return {"files": legacy["files"], "originals": {name: None for name in legacy["files"]},
                "adoptedLegacyRoads": True}
    return {"files": {}}


def space_budget(package, game, *, lbx64, vcache=False):
    """Include optional copies, original protection and interrupted-commit data."""
    from install import component_paths, under
    manifest = json.loads((package / "package.json").read_bytes())
    copied = sum(row["bytes"] for row in manifest.get("optionalFiles", []))
    expanded = 0
    if lbx64:
        with ZipFile(package / "Optional/LBx64" / LB_ARCHIVE) as archive:
            expanded = sum(row.file_size for row in archive.infolist())
    if vcache:
        local_copy = package / "Optional/VCache" / VCACHE_ARCHIVE
        archive_path = local_copy if local_copy.is_file() else bundled_vcache_files()[f"Optional/VCache/{VCACHE_ARCHIVE}"]
        with ZipFile(archive_path) as archive:
            expanded += sum(row.file_size for row in archive.infolist())
    existing = sum(path.stat().st_size for name in set(component_paths()) |
                   set(legacy_module_paths(LB_MODULES + VCACHE_MODULES, game))
                   if (path := under(game, name)).is_file())
    return 3 * copied + 3 * expanded + 2 * existing


def merge_ini(original, default):
    # Existing user values win. New sections and keys get release defaults.
    codec = "utf-8-sig"
    if original.startswith((b"\xff\xfe", b"\xfe\xff")):
        codec = "utf-16"
    try:
        text = original.decode(codec)
    except UnicodeDecodeError:
        codec = "cp932"
        text = original.decode(codec)
    current = configparser.ConfigParser(interpolation=None, strict=False)
    current.optionxform = str
    current.read_string(text)
    defaults = configparser.ConfigParser(interpolation=None, strict=False)
    defaults.optionxform = str
    template = default.decode("utf-16" if default.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")
    defaults.read_string(template)
    existing = {section.casefold(): section for section in current.sections()}
    for section in defaults.sections():
        target = existing.get(section.casefold(), section)
        if not current.has_section(target):
            current.add_section(target)
        keys = {key.casefold() for key in current[target]}
        for key, value in defaults[section].items():
            if key.casefold() not in keys:
                current[target][key] = value
    output = io.StringIO()
    current.write(output)
    # Retain the release explanations when an existing INI supplies values.
    # They follow the documented keys even when users vary section/key casing.
    comments = {}
    section = ""
    pending = []
    for line in template.splitlines():
        if line.startswith(";"):
            pending.append(line)
        elif match := re.fullmatch(r"\[([^]]+)\]", line.strip()):
            section = match[1].casefold()
            comments[(section, "")] = pending
            pending = []
        elif "=" in line:
            comments[(section, line.split("=", 1)[0].strip().casefold())] = pending
            pending = []
    section = ""
    lines = []
    for line in output.getvalue().splitlines():
        if match := re.fullmatch(r"\[([^]]+)\]", line):
            section = match[1].casefold()
            lines.extend(comments.get((section, ""), ()))
        elif "=" in line and not line.startswith((" ", "\t")):
            lines.extend(comments.get((section, line.split("=", 1)[0].strip().casefold()), ()))
        lines.append(line)
    return ("\r\n".join(lines) + "\r\n").encode("utf-16" if any(ord(c) > 127 for c in template) else codec)


def plan_components(package, game, *, addon, lbx64, vcache=False):
    from install import CONTROL, COMPONENT_STATE, under, sha, replace_file, component_paths, allowed_component

    manifest = json.loads((package / "package.json").read_bytes())
    rows = manifest.get("optionalFiles", [])
    if not rows and (addon or lbx64):
        raise ValueError("Rebuild the world with RC6 to select its bundled components.")
    previous = component_state(game)
    if not rows and not vcache and isinstance(previous, dict) and not previous.get("vcache"):
        return {}, {"addon": False, "lbx64": False, "vcache": False}
    optional = {row["path"]: under(package, row["path"]) for row in rows}
    vcache_archive = f"Optional/VCache/{VCACHE_ARCHIVE}"
    if vcache_archive not in optional:
        # Earlier RC6 conversions use the same world data. Attach this
        # converter's verified CPU component without rebuilding that world.
        optional.update(bundled_vcache_files())
    changes = {f"{CONTROL}/{name}": source for name, source in optional.items()}
    if (not isinstance(previous, dict) or not isinstance(previous.get("files"), dict)
            or not all(allowed_component(name) for name in previous["files"])
            or not previous["files"].keys() <= previous.get("originals", {}).keys()):
        raise ValueError("Invalid optional component installation record.")
    before = dict(previous.get("originals", {}))
    managed = {}

    def include(name, data):
        current = under(game, name)
        if (name in previous["files"] and sha(current) != previous["files"][name]
                and not name.lower().endswith(".ini")):
            raise ValueError(f"The optional component was edited after installation: {name}")
        backup = under(game, f"{CONTROL}/component-originals/{name}")
        if name not in before:
            # A failed install may already have retained the original. Never
            # replace a recoverable original with a newer installed component.
            if backup.exists() and sha(backup) != sha(current):
                raise ValueError(f"Retained original component differs: {name}")
            before[name] = sha(current)
            if current.is_file() and not backup.exists():
                replace_file(backup, current)
        elif before[name] is not None and sha(backup) != before[name]:
            raise ValueError(f"Original component backup differs: {name}")
        changes[name] = data
        managed[name] = sha(data) if isinstance(data, Path) else (digest(data) if data is not None else None)

    if addon:
        base = package / "Optional/AddOn/game"
        for file in sorted(base.rglob("*")):
            if file.is_file():
                include(file.relative_to(base).as_posix(), file)
    if lbx64 or vcache:
        from preflight import CATALOG, file_digest
        if file_digest(game / "speed.exe").upper() not in {
                CATALOG["targets"]["NFSPatcher EN-US 1.3 LAA"]["sha256"],
                "80774C2E5D619B4F120B48D4462896FD504C263399D203A238769CFFDE1D253C"}:
            if vcache:
                raise ValueError("NFSMWVCacheAffinityには対応する英語版1.3の実行ファイルが必要です。")
            raise ValueError("LBx64 RC3.1 requires the supported English 1.3 executable. Its manual package is included.")
    if lbx64:
        archive_path = package / "Optional/LBx64" / LB_ARCHIVE
        if digest(archive_path.read_bytes()) != LB_SHA256:
            raise ValueError("Invalid LBx64 RC3.1 archive.")
        # Keep the original outside every ASI search directory. Removing a
        # legacy duplicate is part of the same recoverable commit as the new LB.
        candidates = set(legacy_module_paths(LB_MODULES, game)) | {
            name for name in previous["files"] if legacy_module_path(name, LB_MODULES)}
        for name in sorted(candidates):
            if (name.endswith(".asi") and name != "scripts/NFSMWLimitBreaker.asi"
                    and (under(game, name).is_file() or name in previous["files"])):
                include(name, None)
        with ZipFile(archive_path) as archive:
            for name in archive.namelist():
                if name.startswith("/") or ".." in Path(name).parts:
                    raise ValueError("Invalid LBx64 archive path.")
                if name not in component_paths():
                    raise ValueError(f"Unexpected LBx64 component: {name}")
                data = archive.read(name)
                current = under(game, name)
                if name.endswith(".ini") and current.is_file():
                    data = merge_ini(current.read_bytes(), data)
                include(name, data)
    if vcache:
        archive_path = optional[vcache_archive]
        payload = archive_path.read_bytes()
        if digest(payload) != VCACHE_SHA256:
            raise ValueError("NFSMWVCacheAffinityの同梱ファイルを確認できません。ZIPを展開し直してください。")
        main_asi, main_ini, _ = VCACHE_FILES
        candidates = set(legacy_module_paths(VCACHE_MODULES, game)) | {
            name for name in previous["files"] if legacy_module_path(name, VCACHE_MODULES)}
        for name in sorted(candidates):
            if name.endswith(".asi") and name != main_asi and (
                    under(game, name).is_file() or name in previous["files"]):
                include(name, None)
        with ZipFile(io.BytesIO(payload)) as archive:
            if sorted(archive.namelist()) != sorted(VCACHE_FILES):
                raise ValueError("Invalid NFSMWVCacheAffinity archive inventory.")
            config = next((path for name in [main_ini, *sorted(candidates)]
                           if name.endswith(".ini") and (path := under(game, name)).is_file()), None)
            for name in VCACHE_FILES:
                data = archive.read(name)
                if name == main_ini and config is not None:
                    data = merge_ini(config.read_bytes(), data)
                include(name, data)
    for name, expected in previous["files"].items():
        if name in managed:
            continue
        if sha(under(game, name)) != expected:
            raise ValueError(f"Restore cancelled because a component file was edited: {name}")
        original = before[name]
        source = under(game, f"{CONTROL}/component-originals/{name}")
        if original is not None and sha(source) != original:
            raise ValueError(f"Missing original component backup: {name}")
        # The main installer already selects the new base road graph.
        if name != "TRACKS/L2RA.BUN":
            changes[name] = source if original is not None else None
    options = {"addon": addon, "lbx64": lbx64, "vcache": vcache}
    state = {"converterVersion": "RC6", **options, "files": managed, "originals": before}
    changes[COMPONENT_STATE] = encode(state)
    return changes, options
