"""Identify local Carbon inputs for rebuilding functional W2C breakers and cameras.

This is a development input check/extractor. It does not install anything or
make an incomplete public world into a full one. Extracted game data stays local.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct


CHUNKS = (
    ("sequences", "GLOBAL/GLOBALB.BUN", 0x8003B810),
    ("cameras-ingameb", "GLOBAL/InGameB.bun", 0x8003B200),
    ("cameras-ingamec", "GLOBAL/InGameC.bun", 0x8003B200),
)


def input_file(root, name):
    path = (root / name).resolve(strict=True)
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f"Expected a file inside the selected Carbon folder: {name}")
    return path


def bank_chunks(path, kind):
    size = path.stat().st_size
    if not 0 < size <= 64 * 1024**2:
        raise ValueError("Unexpected Carbon bank extent.")
    with path.open("rb") as stream:
        while stream.tell() < size:
            offset = stream.tell()
            header = stream.read(8)
            if len(header) != 8:
                raise ValueError("Truncated Carbon bank header.")
            tag, length = struct.unpack("<II", header)
            if length > size - stream.tell():
                raise ValueError("Carbon chunk exceeds its bank.")
            if kind == tag and length:
                if length > 8 * 1024**2:
                    raise ValueError("Unexpected Carbon chunk extent.")
                data = stream.read(length)
                if len(data) != length:
                    raise ValueError("Truncated Carbon chunk.")
                yield offset, data
            else:
                stream.seek(length, 1)


def checked_inputs(root):
    extracted = {}
    records = []
    for role, name, kind in CHUNKS:
        try:
            banks = list(bank_chunks(input_file(root, name), kind))
        except (FileNotFoundError, ValueError):
            if role == "sequences":
                raise
            continue
        if not banks and role == "sequences":
            raise ValueError(f"Could not extract Carbon pursuit sequences from {name}.")
        output = bytearray()
        for offset, payload in banks:
            output.extend(struct.pack("<II", kind, len(payload)))
            output.extend(payload)
            records.append({"role": role, "source": name, "offset": offset, "chunk": f"{kind:08x}",
                            "bytes": len(payload), "payloadSha256": hashlib.sha256(payload).hexdigest()})
        if output:
            extracted[role + ".bun"] = bytes(output)
    decoders = [name for name in ("speed.exe", "nfs.exe", "nfsc.exe") if (root / name).is_file()]
    report = {"decoderCandidates": decoders, "verifiedChunks": records, "publicRuntimeInstalled": False}
    return extracted, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--carbon", required=True, type=Path)
    parser.add_argument("--out", type=Path, help="Optional new local folder for verified chunk extraction")
    args = parser.parse_args()
    try:
        root = args.carbon.resolve(strict=True)
        out = args.out.resolve() if args.out else None
        if out is not None and (out.is_relative_to(root) or out.exists()):
            raise ValueError("Choose a new work folder outside the Carbon installation.")
        extracted, report = checked_inputs(root)
        if out is not None:
            out.mkdir()
            for name, data in extracted.items():
                with (out / name).open("xb") as f:
                    f.write(data)
        print(json.dumps(report, indent=2))
    except (OSError, ValueError) as e:
        parser.exit(2, f"{e}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
