"""Read-only input and dependency checks for W2C to MW Converter 2.0.0."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import yaml


ROOT = Path(__file__).resolve().parent
CATALOG = json.loads((ROOT / "catalog.json").read_text(encoding="utf-8"))
COMPETING_PROVIDERS = (
    "World2MWNativeLoader.asi", "AAA_World2MWProbe.asi", "NFSMWMapLoader.asi", "W2CMWv2.asi",
    "AAA_W2CMWv2Bootstrap.asi",
)
OPTIONAL_MODULES = ("NFSMostWanted.WidescreenFix.asi", "TexWizard.asi")
OPTIONAL_GAMEPLAY = (
    ("Unlimiter", "NFSMWUnlimiter.asi", "https://github.com/nlgxzef/NFSMWUnlimiter/releases"),
    ("Extra Options", "NFSMWExtraOptions.asi", "https://github.com/ExOptsTeam/NFSMWExOpts/releases"),
)
ASI_FOLDERS = ("scripts/", "", "plugins/", "update/")
SUNSET_ASI = "scripts/SunSet.asi"
SUNSET_CONFIG = "scripts/SunSetData/Config.yml"
TEXWIZARD_ASI = "scripts/TexWizard.asi"
TEXWIZARD_CONFIG = "scripts/TexWizard.json"
SUNSET_PACK = "TexturePacks/SunSet"
SUNSET_FILES = (
    SUNSET_ASI, "scripts/yaml-cpp.dll",
    *("scripts/SunSetData/" + name for name in (
        "Config.yml", "Flares.yml", "Meshes.yml", "SpotLights.yml", "Textures.yml", "Weather.yml", "volume.dds")),
    *("shaders/IDI_" + name + "_FX" for name in (
        "CAR", "GLOSSYWINDOW", "PARTICLES", "SCREENFILTER", "SKYBOX", "TREE", "WORLD",
        "WORLDNOFOG", "WORLDNORMALMAP", "WORLDNORMALMAPNOFOG", "WORLDPRELIT", "WORLDREFLECT")),
    SUNSET_PACK + "/meta.json", SUNSET_PACK + "/TEXTURES.BIN",
)


def texwizard_settings(path):
    if not path.is_file():
        raise ValueError("TexWizard.json がありません。")
    config = json.loads(path.read_text(encoding="utf-8-sig"))
    if (not isinstance(config, dict) or not isinstance(config.get("autoScan", True), bool)
            or not isinstance(config.get("packs"), list)
            or any(not isinstance(pack, str) for pack in config["packs"])):
        raise ValueError("TexWizard.json の形式を確認してください。")
    return config


def has_sunset_pack(config):
    return any(pack.replace("\\", "/").strip("/").casefold() == SUNSET_PACK.casefold()
               for pack in config.get("packs", []))


def sunset_config(path):
    # The official config has tabs before inline comments. yaml-cpp
    # accepts them; normalize that spacing only for our read check.
    cfg = yaml.safe_load(re.sub(rb"[ \t]+(?=#)", b" ", path.read_bytes()))
    if not isinstance(cfg, dict) or not isinstance(cfg.get("Config"), dict) or not isinstance(cfg.get("Time"), dict):
        raise ValueError("SunSetの Config.yml を確認してください。")
    return cfg


def sunset_resource_valid(path, name):
    if not path.is_file() or not path.stat().st_size:
        return False
    try:
        if name.endswith("/meta.json"):
            meta = json.loads(path.read_bytes())
            return isinstance(meta, dict) and isinstance(meta.get("textures"), list) and bool(meta["textures"])
        if name == SUNSET_CONFIG:
            sunset_config(path)
            return True
        with path.open("rb") as stream:
            if name.endswith("/TEXTURES.BIN"):
                header = stream.read(8)
                if len(header) != 8:
                    return False
                kind, size = struct.unpack("<II", header)
                return kind == 0xB3300000 and 48 <= size <= path.stat().st_size - 8
            if name.startswith("shaders/"):
                return stream.read(4) == bytes.fromhex("0109fffe")
            if name.endswith(".dds"):
                return stream.read(4) == b"DDS "
        if name.endswith((".dll", ".asi")):
            return bool((pe_info(path) or {}).get("win32"))
    except (ValueError, yaml.YAMLError):
        return False
    return True


def file_digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def file_at(root, relative):
    path = root.joinpath(*relative.split("/")).resolve()
    if not path.is_relative_to(root):
        raise ValueError(f"Path leaves the selected folder: {relative}")
    return path


def pe_info(path):
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b"MZ":
            return None
        offset = struct.unpack_from("<I", header, 60)[0]
        if not 64 <= offset <= 1024 * 1024:
            return None
        stream.seek(offset)
        header = stream.read(26)
        if len(header) != 26 or header[:4] != b"PE\0\0":
            return None
        machine, flags, magic = (struct.unpack_from("<H", header, at)[0] for at in (4, 22, 24))
        return {"win32": machine == 0x14C and magic == 0x10B, "laa": bool(flags & 0x20)}


def row(scope, name, status, detail, **extra):
    return {"scope": scope, "name": name, "status": status, "detail": detail, **extra}


def source_rows(root, quality):
    files = CATALOG["qualities"][quality] + CATALOG["sharedSource"]
    for expected in files:
        relative = expected["path"]
        path = file_at(root, relative)
        if not path.is_file():
            yield row("source", relative, "stop", "指定品質のW2C 2.0.0原本を展開してください。")
            continue
        if path.stat().st_size != expected["bytes"]:
            yield row("source", relative, "stop", "原本とファイルサイズが一致しません。")
            continue
        actual = file_digest(path)
        if actual != expected["sha256"]:
            yield row("source", relative, "stop", "選択した品質・版の原本ハッシュと一致しません。", sha256=actual)
            continue
        yield row("source", relative, "ok", "W2C 2.0.0原本と一致。", sha256=actual)


def mw_runtime_layout(path):
    # The ASI calls into these sections at fixed addresses. Localized PE
    # resources and file padding are independent of the native interfaces.
    expected = (
        (b".text", 0x1000, 0x48E2A5, 0x48F000, 0x60000020,
         "f258b7f607d10109c577e63497041476935793b8211e5acfd3cb88ac66b24470"),
        (b".rdata", 0x490000, 0x5971D, 0x5A000, 0x40000040,
         "fb27d471178fed85c12d31ee6a5c8dd3e891868ed9c04f19c0fecdbbe660ddc1"),
        (b".data", 0x4EA000, 0xDCE10, 0x24000, 0xC0000040,
         "2f00dcd3c0321f2da51d2b09e23ace8c3491400dc5e78da635d15b8ad3daa190"),
    )
    with path.open("rb") as stream:
        dos = stream.read(64)
        if len(dos) != 64 or dos[:2] != b"MZ":
            return False
        offset, = struct.unpack_from("<I", dos, 60)
        if not 64 <= offset <= 1024 * 1024:
            return False
        stream.seek(offset)
        header = stream.read(248)
        if len(header) != 248 or header[:4] != b"PE\0\0":
            return False
        machine, count, _, _, _, optional_size, flags = struct.unpack_from("<HHIIIHH", header, 4)
        if machine != 0x14C or not 3 <= count <= 96 or optional_size != 224 or flags & 0x2022 != 0x22:
            return False
        optional = header[24:]
        if (struct.unpack_from("<H", optional)[0] != 0x10B
                or struct.unpack_from("<I", optional, 16)[0] != 0x3C4040
                or struct.unpack_from("<II", optional, 28) != (0x400000, 0x1000)
                or struct.unpack_from("<H", optional, 70)[0] & 0x40):
            return False
        table = stream.read(count * 40)
        if len(table) != count * 40:
            return False
        for index, (label, va, virtual, raw, permissions, digest) in enumerate(expected):
            name, vs, rva, size, at, _, _, _, _, attrs = struct.unpack_from("<8sIIIIIIHHI", table, index * 40)
            if (name.rstrip(b"\0"), rva, vs, size, attrs) != (label, va, virtual, raw, permissions):
                return False
            stream.seek(at)
            if hashlib.sha256(stream.read(min(virtual, raw))).hexdigest() != digest:
                return False
        return True


def check_target(root):
    path = file_at(root, "speed.exe")
    if not path.is_file():
        return row("target", "speed.exe", "stop", "Most Wantedの導入先を指定してください。")
    if not mw_runtime_layout(path):
        return row("target", "speed.exe", "stop", "MW 1.3 LAA (32-bit) runtime layout required for ASI deployment.")
    actual = file_digest(path)
    name = next((name for name, profile in CATALOG["targets"].items() if actual == profile["sha256"]), None)
    return row("target", "speed.exe", "ok", "MW 1.3 LAA runtime layout verified.", distribution=name, sha256=actual)


def asi_loader_row(root):
    checked = CATALOG["checkedAsiLoader"]
    loader = file_at(root, checked["file"])
    digest = None
    if loader.is_file():
        if (pe_info(loader) or {}).get("win32"):
            digest = file_digest(loader)
            version = checked["builds"].get(digest)
            if version:
                return row("deployment", "Win32 ASI Loader", "ok",
                           f"Ultimate ASI Loader {version} (Win32 dinput8.dll) を照合しました。",
                           version=version, sha256=digest)
            distribution = checked["redistributedBuilds"].get(digest)
            if distribution:
                return row("deployment", "Win32 ASI Loader", "ok",
                           f"{distribution} 同梱の32ビット dinput8.dll を照合しました。",
                           distribution=distribution, sha256=digest)
            detail = "32ビットの dinput8.dll を検出しましたが、検証済みビルドと一致しません。"
        else:
            detail = "dinput8.dll が有効な32ビットDLLとして認識されません。"
    elif file_at(root, "version.dll").is_file():
        detail = "version.dll を検出しました。確認版の配置には検証済みの dinput8.dll が必要です。"
    else:
        detail = "ゲームフォルダーに検証済みの32ビット dinput8.dll がありません。"
    return row("deployment", "Win32 ASI Loader", "action",
               detail + " Ultimate ASI Loader: " + " / ".join(dict.fromkeys(checked["builds"].values())) + ".",
               sha256=digest, url=checked["url"])


def sunset_row(root, windows, overlay=None):
    catalog = CATALOG["recommendedSunSet"]
    overlay = overlay or {}

    def located(name):
        return overlay[name] if name in overlay else file_at(root, name)

    binaries = [folder + "SunSet.asi" for folder in ASI_FOLDERS
                if located(folder + "SunSet.asi").is_file()]

    def result(status, detail, **extra):
        return row("deployment", "SunSet", status, detail,
                   url=catalog["url"], texWizardUrl=catalog["texWizardUrl"], **extra)

    if not binaries:
        return result("recommend", "照明・天候・影の再現には導入を推奨します。未導入のままでも配置できます。")
    if len(binaries) != 1:
        return result("action", "SunSet.asi が複数あります。使うファイルを1つにして再検査してください。", detected=binaries)
    dll = located(binaries[0])
    if not (pe_info(dll) or {}).get("win32"):
        return result("action", "SunSet.asi を有効な32ビット版として確認できません。")
    digest = file_digest(dll)
    missing = [name for name in SUNSET_FILES if name != SUNSET_ASI
               and not sunset_resource_valid(located(name), name)]
    tex = [located(folder + "TexWizard.asi") for folder in ASI_FOLDERS
           if located(folder + "TexWizard.asi").is_file()]
    if len(tex) != 1 or file_digest(tex[0]) != catalog["texWizardSha256"]:
        missing.append("検証済みのWin32 TexWizard 1.3（1個）")
    for name in ("msvcp140.dll", "vcruntime140.dll"):
        if not any(path.is_file() and (pe_info(path) or {}).get("win32") for path in (
            *(located(folder + name) for folder in ASI_FOLDERS),
            windows / "SysWOW64" / name, windows / "System32" / name,
        )):
            missing.append("Win32 " + name)
    try:
        config = texwizard_settings(located(TEXWIZARD_CONFIG))
        if not config.get("autoScan", True) and not has_sunset_pack(config):
            missing.append("TexWizardのSunSetパック参照")
    except (TypeError, ValueError):
        missing.append("有効なTexWizard.json")
    if missing:
        return result("action", "SunSetに必要なファイルがそろっていません。自動セットアップで不足を補ってください。\n"
                      + "、".join(missing), missing=missing, sha256=digest)
    if digest != catalog["sha256"]:
        return result("action", "SunSet.asi が検証済みの公式配布ZIP " + catalog["distribution"]
                      + " と一致しません。", sha256=digest)
    return result("ok", "公式配布ZIP " + catalog["distribution"] + " のSunSet、TexWizard 1.3、描画資源を確認しました。", sha256=digest)


def redux_graphics_row(root):
    # Redux's embedded effects rely on its renderer and external shader files.
    required = (
        "scripts/NextGenGraphics.MostWanted.asi", "scripts/fmt.dll", "scripts/NFSMWCustomShaders.asi",
        "IDI_CAR_FX", "IDI_VISUALTREATMENT_FX", "IDI_WORLDPRELIT_FX", "IDI_WORLDREFLECT_FX",
        "NEXTGENGRAPHICS/GenericTextures/2DNOISE.png", "NEXTGENGRAPHICS/GenericTextures/SFX_WAVES_NORM.dds",
        "NEXTGENGRAPHICS/GenericTextures/WATER_R0_NORMAL.png",
    )
    missing = []
    for name in required:
        path = file_at(root, name)
        if (not path.is_file() or not path.stat().st_size
                or (path.suffix in {".asi", ".dll"} and not (pe_info(path) or {}).get("win32"))):
            missing.append(name)
    if missing:
        return row("deployment", "Redux 3.04 / NextGenGraphics", "action",
                   "Redux 3.04の同梱描画資源を復元してください。不足したまま起動すると3D画面が黒くなる場合があります。\n"
                   + " / ".join(missing), missing=missing)
    return row("deployment", "Redux 3.04 / NextGenGraphics", "ok", "Redux 3.04の同梱描画資源を確認しました。")


def runtime_rows(root, windows, *, target):
    yield asi_loader_row(root)
    if target.get("distribution") == "Redux 3.04":
        yield redux_graphics_row(root)
    yield sunset_row(root, windows)
    yield row("recommended", "ワールドの影", "manual",
              "SunSetとTexWizardを導入し、MWの画面設定で「Shadow Detail（影の詳細）」を有効にしてください。"
              "同梱設定では夜間の影を無効にしています。動く小物自身の影は未対応です。")
    for title, filename, url in OPTIONAL_GAMEPLAY:
        candidates = [folder + filename for folder in ASI_FOLDERS
                      if file_at(root, folder + filename).is_file()]
        yield row("optional", title, "info", "検出" if candidates else "未導入",
                  detected=candidates, url=url)
    d3dx_name = "d3dx9_43.dll"
    game_d3dx = file_at(root, d3dx_name)
    paths = [game_d3dx] if game_d3dx.is_file() else [windows / folder / d3dx_name for folder in ("SysWOW64", "System32")]
    for path in paths:
        if path.is_file() and (pe_info(path) or {}).get("win32"):
            yield row("deployment", d3dx_name, "ok", "Win32 D3DXを検出。", location=str(path))
            break
    else:
        yield row("deployment", d3dx_name, "action", "Win32 D3DXを導入、またはゲーム内の候補DLLを確認してください。",
                  url="https://www.microsoft.com/en-us/download/details.aspx?id=8109")

    for folder in ASI_FOLDERS:
        for name in COMPETING_PROVIDERS:
            if file_at(root, folder + name).is_file():
                yield row("deployment", folder + name, "stop", "既存のワールドローダーを検出。独立した導入先を選んでください。")
        for name in OPTIONAL_MODULES:
            if file_at(root, folder + name).is_file():
                yield row("optional", folder + name, "info", "検出。対応版と必要な設定は適用前に確認します。")
    widescreen = file_at(root, "scripts/NFSMostWanted.WidescreenFix.ini")
    if widescreen.is_file() and re.search(rb"(?mi)^\s*SkipIntro\s*=\s*1\b", widescreen.read_bytes()):
        yield row("optional", "scripts/NFSMostWanted.WidescreenFix.ini", "warning",
                  "EN-US環境で起動時に停止する場合は、[MAIN] の SkipIntro を 0 にして再起動してください。")
    texwizard = file_at(root, "scripts/TexWizard.json")
    if texwizard.is_file() and file_at(root, "scripts/TexWizard.asi").is_file():
        try:
            packs = json.loads(texwizard.read_text(encoding="utf-8-sig"))["packs"]
            if not isinstance(packs, list) or any(not isinstance(pack, str) for pack in packs):
                raise ValueError("Unsupported TexWizard packs schema")
        except (KeyError, ValueError, TypeError):
            yield row("optional", "scripts/TexWizard.json", "warning",
                      "TexWizardの設定を解釈できません。導入先のTexWizard設定を確認してください。")
        else:
            missing = []
            for pack in packs:
                relative = Path(pack)
                if relative.anchor or ".." in relative.parts:
                    continue
                if not file_at(root, relative.as_posix() + "/meta.json").is_file():
                    missing.append(pack)
            if missing:
                yield row("optional", "scripts/TexWizard.json", "warning",
                          "存在しないパック参照を確認し、退役済みのパックだけを packs から外してください。",
                          missing=missing)


def carbon_banks(root):
    if root is not None:
        for role, bank in CATALOG["carbonTextureBanks"].items():
            path = file_at(root, bank["path"])
            if path.is_file() and path.stat().st_size:
                yield role, path, bank["textures"]


def carbon_row(root):
    if root is None:
        return row("assets", "Carbon", "info", "未指定。W2CとMWの原本で変換します。")
    banks = {role: CATALOG["carbonTextureBanks"][role]["path"] for role, _, _ in carbon_banks(root)}
    missing = [bank["path"] for role, bank in CATALOG["carbonTextureBanks"].items() if role not in banks]
    detail = ("Carbonの画像バンクを検出しました。読み取れるテクスチャを変換時に取り込みます。"
              if banks else "対応するCarbon資源を検出できませんでした。W2CとMWの原本で変換します。")
    if missing:
        detail += " 見つからないバンクは省略します。"
    return row("assets", "Carbon", "info", detail, detected=list(banks.values()), missing=missing)


def checked_folder(value):
    if value is None:
        return None
    root = Path(value).resolve()
    if not root.is_dir():
        raise ValueError(f"フォルダーが見つかりません: {value}")
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--w2c", required=True, help="展開したW2C 2.0.0のフォルダー")
    parser.add_argument("--mw", required=True, help="Most Wantedの導入先")
    parser.add_argument("--quality", required=True, choices=tuple(CATALOG["qualities"]))
    parser.add_argument("--carbon", help="任意のCarbonフォルダー")
    args = parser.parse_args()
    try:
        w2c, mw, carbon = (checked_folder(value) for value in (args.w2c, args.mw, args.carbon))
        windows = Path(os.environ.get("WINDIR", r"C:\Windows"))
        target = check_target(mw)
        checks = [*source_rows(w2c, args.quality), target, carbon_row(carbon), *runtime_rows(mw, windows, target=target)]
    except (OSError, ValueError) as error:
        parser.exit(2, f"{error}\n")
    source_ok = not any(r["scope"] in {"source", "target"} and r["status"] == "stop" for r in checks)
    runtime_ok = not any(r["scope"] == "deployment" and r["status"] in {"stop", "action"} for r in checks)
    print(json.dumps({"version": "2.0.0", "quality": args.quality, "sourceIdentityPassed": source_ok,
                      "runtimeDependenciesDetected": runtime_ok, "checks": checks}, ensure_ascii=False, indent=2))
    return 0 if source_ok and runtime_ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
