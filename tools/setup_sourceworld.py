"""Build SourceWorld from pinned upstream source on the user's PC."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.request import urlopen
from zipfile import ZipFile

SDK_VERSION = "11.0.100"
SDK_ARCHIVE = f"dotnet-sdk-{SDK_VERSION}-win-x64.zip"
SDK_URL = f"https://builds.dotnet.microsoft.com/dotnet/Sdk/{SDK_VERSION}/{SDK_ARCHIVE}"
SDK_SHA512 = "5cdd9122c84873f090038548ebe8a4786e5c50a6b1b9792ca2c6dab82fd74a0d7f1d6cdf59e7dcd25e4903dd44f37af5d7ed73ecd3bca4d0cce948b5572452c3"
SOURCE_ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = "https://codeload.github.com/NFSTools/NFS-ModTools/zip/"
FLAGS = getattr(subprocess, "CREATE_NO_WINDOW", 0)

def importer_command():
    """Use one locally built importer for both W2C and reference-game reads."""
    if getattr(sys, "frozen", False):
        built = Path(sys._MEIPASS) / "bin/SourceWorld.exe"
        if built.is_file():
            return [str(built)]
    local = SOURCE_ROOT / "compiler/source-world/bin/Release/net11.0/SourceWorld.dll"
    if not getattr(sys, "frozen", False) and local.is_file():
        return ["dotnet", str(local)]
    cache = Path(os.environ.get("LOCALAPPDATA", Path.home())) / "W2C-to-MW/SourceWorld/RC6/SourceWorld.exe"
    bundle = Path(sys._MEIPASS) / "importer-source" if getattr(sys, "frozen", False) else None
    return [str(cache if cache.is_file() else build_importer(cache, bundle=bundle))]

def prepare(workspace, lock_file, archives=None):
    lock = json.loads(lock_file.read_text(encoding="utf-8"))
    vendor = workspace / "compiler/source-world/vendor"
    for spec in lock["archives"]:
        if archives is None:
            with urlopen(UPSTREAM + spec["commit"], timeout=60) as response:
                data = response.read(24 * 1024 * 1024 + 1)
        else:
            data = (archives / spec["name"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != spec["sha256"]:
            raise ValueError(f"SourceWorld: SHA-256 mismatch for {spec['group']} upstream archive.")
        folder = spec["group"] + "/"
        with ZipFile(io.BytesIO(data)) as archive:
            for name, (digest, edits) in lock["files"].items():
                if not name.startswith(folder):
                    continue
                a = archive.read(f"NFS-ModTools-{spec['commit']}/{name}").replace(b"\r\n", b"\n").decode("utf-8")
                for start, end, replacement in reversed(edits):
                    a = a[:start] + replacement + a[end:]
                b = a.encode("utf-8")
                if hashlib.sha256(b).hexdigest() != digest:
                    raise ValueError(f"SourceWorld: generated hash mismatch for {name}.")
                f = vendor / name
                f.parent.mkdir(parents=True, exist_ok=True)
                f.write_bytes(b)

def has_sdk(dotnet):
    result = subprocess.run([dotnet, "--list-sdks"], capture_output=True, text=True,
                            creationflags=FLAGS, timeout=15)
    return result.returncode == 0 and any(
        line.startswith(SDK_VERSION + " [") for line in result.stdout.splitlines())

def find_sdk(workdir, progress, dotnet_override=None):
    if dotnet_override is not None:
        return dotnet_override
    for path in (Path(os.environ.get("ProgramW6432", r"C:\Program Files")) / "dotnet/dotnet.exe",
                 Path(shutil.which("dotnet") or "dotnet.exe")):
        if path.is_file() and has_sdk(path):
            return path
    progress("Downloading and verifying the Microsoft .NET 11 SDK", 5)
    archive_path = workdir / SDK_ARCHIVE
    digest = hashlib.sha512()
    received = 0
    last = 5
    with urlopen(SDK_URL, timeout=60) as response, archive_path.open("wb") as output:
        size = int(response.headers.get("Content-Length", "0"))
        while chunk := response.read(1024 * 1024):
            received += len(chunk)
            if received > 512 * 1024 * 1024:
                raise ValueError("SourceWorld: .NET SDK download exceeds the expected size.")
            digest.update(chunk)
            output.write(chunk)
            percent = 5 + min(20, received * 20 // max(size, received, 1))
            if percent > last:
                progress("Downloading and verifying the Microsoft .NET 11 SDK", percent)
                last = percent
    if digest.hexdigest() != SDK_SHA512:
        raise ValueError("SourceWorld: Microsoft .NET SDK SHA-512 mismatch.")
    sdk = workdir / "sdk"
    progress("Extracting the verified .NET SDK", 26)
    with ZipFile(archive_path) as archive:
        archive.extractall(sdk)
    archive_path.unlink()
    return sdk / "dotnet.exe"

def build_importer(destination, progress=lambda *_: None, *, bundle=None, archives=None, dotnet_override=None):
    source = bundle if bundle is not None else SOURCE_ROOT
    if destination.exists():
        raise FileExistsError(f"SourceWorld already exists: {destination}")
    with tempfile.TemporaryDirectory(prefix="W2CMW-importer-") as tmp:
        root = Path(tmp)
        workspace = root / "src"
        wanted = (
            "compiler/source-world/Program.cs", "compiler/source-world/SourceExport.cs",
            "compiler/source-world/SourceWorld.csproj", "compiler/target-mw/Program.cs",
            "compiler/source-world/vendor/Common/Common.csproj",
            "compiler/source-world/vendor/CompLib/CompLib.csproj",
            "tools/sourceworld.lock.json",
        )
        for relative in wanted:
            target = workspace / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / relative, target)
        dotnet = find_sdk(root, progress, dotnet_override)
        progress("Fetching and verifying the upstream importer source", 32)
        prepare(workspace, workspace / "tools/sourceworld.lock.json", archives)
        if dotnet_override is None:
            (workspace / "global.json").write_text(
                json.dumps({"sdk":{"version":SDK_VERSION,"rollForward":"disable","allowPrerelease":False}}),
                encoding="utf-8")
        (workspace / "NuGet.Config").write_text(
            '<configuration><packageSources><clear /><add key="nuget.org" '
            'value="https://api.nuget.org/v3/index.json" /></packageSources></configuration>',
            encoding="utf-8")
        progress("Building the source importer on this PC", 65)
        process = subprocess.run(
            [dotnet, "publish", workspace / "compiler/source-world/SourceWorld.csproj",
             "-c", "Release", "-r", "win-x64", "--self-contained", "true",
             "-o", root / "out", "-p:PublishSingleFile=true", "-p:DebugType=None",
             "-p:DebugSymbols=false", "-p:UseSharedCompilation=false", "--disable-build-servers"],
            cwd=workspace, capture_output=True, text=True, errors="replace", timeout=360,
            creationflags=FLAGS, env=dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT="1",
                DOTNET_NOLOGO="1", DOTNET_CLI_HOME=str(root / "cli-home"), MSBUILDDISABLENODEREUSE="1"))
        if process.returncode:
            raise RuntimeError("SourceWorld build failed:\n" + (process.stdout + process.stderr)[-2500:])
        exe = root / "out/SourceWorld.exe"
        if not exe.is_file() or exe.stat().st_size == 0:
            raise RuntimeError("SourceWorld build did not produce the importer.")
        progress("Installing the locally built source importer", 98)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=".sourceworld-", delete=False) as output:
            pending = Path(output.name)
            try:
                with exe.open("rb") as stream:
                    shutil.copyfileobj(stream, output)
                output.close()
                # On Windows rename fails if another invocation already installed the helper.
                pending.rename(destination)
            finally:
                pending.unlink(missing_ok=True)
        progress("Source importer ready", 100)
    return destination

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--archives", type=Path, help="directory containing the two pinned upstream ZIPs")
    parser.add_argument("--dotnet", type=Path, help="explicit SDK executable for developer builds")
    opts = parser.parse_args()
    build_importer(opts.output.resolve(), lambda message, percent: print(f"{percent}% {message}", flush=True),
                   archives=opts.archives and opts.archives.resolve(),
                   dotnet_override=opts.dotnet and opts.dotnet.resolve())

