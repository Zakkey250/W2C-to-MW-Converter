"""Optional component placement participates in the world install transaction."""

import configparser
import io
import json
from pathlib import Path
from zipfile import ZipFile

from components import LB_ARCHIVE, LB_SHA256, digest, encode


def legacy_lb_paths(game=None):
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
                 for name in ("NFSMWLimitBreaker", "MWLimitBrakerX64") for extension in (".asi", ".ini"))


def legacy_lb_path(name):
    """The loader scans root, plugin directories and one child level."""
    parts = name.replace("\\", "/").split("/")
    if any(part in ("", ".", "..") or ":" in part for part in parts):
        return False
    return (len(parts) <= 3 and (len(parts) == 1 or parts[0].casefold() in {"scripts", "plugins", "update"})
            and parts[-1].casefold() in {"nfsmwlimitbreaker.asi", "nfsmwlimitbreaker.ini",
                                                   "mwlimitbrakerx64.asi", "mwlimitbrakerx64.ini"})


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


def space_budget(package, game, *, lbx64):
    """Include optional copies, original protection and interrupted-commit data."""
    from install import component_paths, under
    manifest = json.loads((package / "package.json").read_bytes())
    copied = sum(row["bytes"] for row in manifest.get("optionalFiles", []))
    expanded = 0
    if lbx64:
        with ZipFile(package / "Optional/LBx64" / LB_ARCHIVE) as archive:
            expanded = sum(row.file_size for row in archive.infolist())
    existing = sum(path.stat().st_size for name in set(component_paths()) | set(legacy_lb_paths(game))
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
    defaults.read_string(default.decode("utf-16" if default.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"))
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
    return output.getvalue().replace("\n", "\r\n").encode(codec)


def plan_components(package, game, *, addon, lbx64):
    from install import CONTROL, COMPONENT_STATE, under, sha, replace_file, component_paths, allowed_component

    manifest = json.loads((package / "package.json").read_bytes())
    rows = manifest.get("optionalFiles", [])
    if not rows:
        if addon or lbx64:
            raise ValueError("Rebuild the world with RC6 to select its bundled components.")
        return {}, {"addon": False, "lbx64": False}
    changes = {f"{CONTROL}/{row['path']}": under(package, row["path"]) for row in rows}
    previous = component_state(game)
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
    if lbx64:
        archive_path = package / "Optional/LBx64" / LB_ARCHIVE
        if digest(archive_path.read_bytes()) != LB_SHA256:
            raise ValueError("Invalid LBx64 RC3.1 archive.")
        from preflight import CATALOG, file_digest
        if file_digest(game / "speed.exe").upper() not in {
                CATALOG["targets"]["NFSPatcher EN-US 1.3 LAA"]["sha256"],
                "80774C2E5D619B4F120B48D4462896FD504C263399D203A238769CFFDE1D253C"}:
            raise ValueError("LBx64 RC3.1 requires the supported English 1.3 executable. Its manual package is included.")
        # Keep the original outside every ASI search directory. Removing a
        # legacy duplicate is part of the same recoverable commit as the new LB.
        candidates = set(legacy_lb_paths(game)) | {name for name in previous["files"] if legacy_lb_path(name)}
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
    state = {"converterVersion": "RC6", "addon": addon, "lbx64": lbx64, "files": managed, "originals": before}
    changes[COMPONENT_STATE] = encode(state)
    return changes, {"addon": addon, "lbx64": lbx64}
