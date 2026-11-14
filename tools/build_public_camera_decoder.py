"""Stage an isolated Windows interpreter for the Unicorn camera decoder."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
from urllib.request import urlopen
from zipfile import ZipFile


PROJECT = Path(__file__).resolve().parents[1]
EMBED_ARCHIVE = "python-3.12.9-embed-amd64.zip"
EMBED_SHA256 = "615861fb801e8b04c847598db4e1e46e4b046295017caa37cb5486dde72b5865"
EMBED_FILES = (
    "python.exe", "python312.dll", "python312.zip", "python312._pth",
    "vcruntime140.dll", "vcruntime140_1.dll", "_ctypes.pyd", "libffi-8.dll",
)
CAMERA_MODULES = ("cameras_compile.py", "curve_oracle.py", "audit_breaker_sequences.py", "native_camera.py")
PEFILE_FILES = ("pefile.py", "ordlookup/__init__.py", "ordlookup/oleaut32.py",
                "ordlookup/ws2_32.py", "ordlookup/wsock32.py")
UNICORN_FILES = (
    "__init__.py", "unicorn.py", "unicorn_const.py", "arm_const.py", "arm64_const.py",
    "m68k_const.py", "mips_const.py", "riscv_const.py", "s390x_const.py", "sparc_const.py",
    "tricore_const.py", "x86_const.py", "unicorn_py3/__init__.py", "unicorn_py3/unicorn.py",
    "unicorn_py3/arch/__init__.py", "unicorn_py3/arch/intel.py", "unicorn_py3/arch/types.py",
    "lib/unicorn.dll",
)
DECODER_DEPENDENCIES = (*PEFILE_FILES, *(f"unicorn/{name}" for name in UNICORN_FILES))
DECODER_FILES = {*EMBED_FILES, *CAMERA_MODULES, *DECODER_DEPENDENCIES}
X86_SMOKE = """
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX

machine = Uc(UC_ARCH_X86, UC_MODE_32)
machine.mem_map(0x1000, 0x1000)
machine.mem_write(0x1000, bytes.fromhex("b82a000000"))
machine.emu_start(0x1000, 0x1005)
assert machine.reg_read(UC_X86_REG_EAX) == 42
"""


def stage(archive, target):
    if not archive.exists():
        url = "https://www.python.org/ftp/python/3.12.9/" + EMBED_ARCHIVE
        with urlopen(url, timeout=60) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != EMBED_SHA256:
            raise ValueError("The downloaded camera interpreter does not match the pinned archive.")
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != EMBED_SHA256:
        raise ValueError("The cached camera interpreter does not match the pinned archive.")

    target.mkdir()
    with ZipFile(archive) as bundle:
        for name in EMBED_FILES:
            (target / name).write_bytes(bundle.read(name))
    modules = PROJECT / "compiler/source-runtime"
    for name in CAMERA_MODULES:
        shutil.copyfile(modules / name, target / name)
    site = Path(sys.prefix) / "Lib/site-packages"
    for name in DECODER_DEPENDENCIES:
        destination = target / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(site / name, destination)

    target = target.resolve()
    subprocess.run([target / "python.exe", "-I", "-B", "-c", X86_SMOKE], cwd=target,
                   env=dict(os.environ, LIBUNICORN_PATH=str(target / "unicorn/lib")),
                   check=True, timeout=20)


if __name__ == "__main__":
    stage(Path(sys.argv[1]), Path(sys.argv[2]))
