"""Package the portable converter directory after building it."""
import argparse
import ast
import hashlib
from pathlib import Path
import stat
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/desktop"
REQUIRED = {"W2C-to-MW-Converter.exe", "_internal/bin/W2CMWPublic.asi",
            "_internal/bin/W2CMWv2Repack.exe", "_internal/bin/PublicVault.exe",
            "_internal/bin/W2CMW-Rollback.exe", "_internal/base_library.zip",
            "_internal/camera-decoder/python312.zip", "_internal/importer-source/tools/sourceworld.lock.json"}
BIN = {s for s in REQUIRED if s.startswith("_internal/bin/")}
TOP = {"base_library.zip", "catalog.json", "rules.json", "translations.json", "compiler-identity.json"}
TREES = {"assets", "camera-decoder", "importer-source", "presets", "_tcl_data", "_tk_data", "tcl8",
         "numpy", "numpy.libs", "numpy-2.4.3.dist-info", "scipy", "scipy.libs", "yaml", "PIL", "lz4"}
BLOCKED = {".log", ".bun", ".moo", ".sav", ".lzc", ".ini", ".pdb"}

def allowed(name):
    p = Path(name)
    if p.suffix.lower() in BLOCKED or "sourceworld.exe" in name.lower():
        return False
    if name == "W2C-to-MW-Converter.exe" or name in BIN:
        return True
    parts = p.parts
    if parts[0] != "_internal":
        return False
    if len(parts) == 2:
        return p.suffix in {".dll", ".pyd"} or parts[1] in TOP
    return parts[1] in TREES and not (p.suffix in {".asi", ".exe"} and
                name != "_internal/camera-decoder/python.exe")

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build", type=Path, default=BUILD, help="directory used by the desktop builder")
    ap.add_argument("--notices", required=True, type=Path, help="third-party notice files to store under notices/")
    ap.add_argument("--output", required=True, type=Path)
    n = ap.parse_args()
    if n.output.exists():
        raise FileExistsError(n.output)
    app = n.build / "portable/W2C-to-MW-Converter"
    _, entries = ast.literal_eval((n.build / "pyinstaller-clean/W2C-to-MW-Converter/PYZ-00.toc").read_text(encoding="utf-8"))
    assert any(row[0] == "setup_sourceworld" for row in entries)
    assert not any(row[0].split(".")[0] == "unicorn" for row in entries)
    files = {}
    for f in app.rglob("*"):
        if f.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Unexpected link: {f}")
        if f.is_file():
            name = f.relative_to(app).as_posix()
            if not allowed(name):
                raise ValueError(f"Unexpected component: {name}")
            files[name] = f
    if REQUIRED - files.keys():
        raise ValueError(f"Missing files: {REQUIRED - files.keys()}")
    files.update({s: ROOT / "public-converter" / s for s in ("QUICKSTART-JA.md", "QUICKSTART-EN.md")})
    files["THIRD_PARTY_NOTICES.md"] = ROOT / "THIRD_PARTY_NOTICES.md"
    files.update({f"notices/{f.name}": f for f in n.notices.iterdir() if f.is_file() and f.suffix == ".txt"})
    if not all("notices/" + x in files for x in ("NFSPluginSDK-LICENSE.txt", "LZ4-LICENSE.txt",
               "MinHook-LICENSE.txt", "Python-LICENSE.txt", "PyInstaller-COPYING.txt")):
        raise ValueError("Missing third-party notices.")
    pending = n.output.with_suffix(".pending")
    try:
        with ZipFile(pending, "x", ZIP_DEFLATED, compresslevel=5) as z:
            for name, source in sorted(files.items()):
                z.write(source, name)
        with ZipFile(pending) as z:
            if z.testzip() is not None or set(z.namelist()) != files.keys():
                raise ValueError("ZIP content integrity check failed.")
        pending.rename(n.output)
    finally:
        pending.unlink(missing_ok=True)
    with n.output.open("rb") as src:
        digest = hashlib.file_digest(src, "sha256").hexdigest()
    n.output.with_suffix(".zip.sha256").write_text(f"{digest}  {n.output.name}\n", encoding="ascii")
    print(n.output.name, n.output.stat().st_size, len(files), digest)

if __name__ == "__main__":
    main()
