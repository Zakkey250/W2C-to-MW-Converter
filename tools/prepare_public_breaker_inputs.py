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
    ("sequences", "GLOBAL/GLOBALB.BUN", 11218440, 0x8003B810, 197120,
     "dd123336c5cf0ae68e56c0c883603158263c0399b2c0cc35e25ec1488854edd1"),
    ("cameras-ingameb", "GLOBAL/InGameB.bun", 637280, 0x8003B200, 269616,
     "ff1892bbb04a3301e9b12141c02643befa1fe769b1a2e9128f7f04ad250ef89e"),
    ("cameras-ingamec", "GLOBAL/InGameC.bun", 708928, 0x8003B200, 99452,
     "ecfc4918020ae682dccdd9b2fcd72757edd5d17e543a15828052cb2e58393e61"),
)
DECODER_BYTES = 7217152
DECODER_SHA256 = "590239474994375c56c4675839ed8ed23bae76e3c0aad2d5ccdb8ec1883b2ac7"


def input_file(root, name):
    path = (root / name).resolve(strict=True)
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f"Expected a file inside the selected Carbon folder: {name}")
    return path


def checked_inputs(root):
    decoder = input_file(root, "speed.exe")
    if decoder.stat().st_size != DECODER_BYTES:
        raise ValueError("Unsupported Carbon decoder executable size.")
    with decoder.open("rb") as f:
        if hashlib.file_digest(f, "sha256").hexdigest() != DECODER_SHA256:
            raise ValueError("Unsupported Carbon decoder executable fingerprint.")

    extracted = {}
    records = []
    for role, name, offset, kind, size, digest in CHUNKS:
        path = input_file(root, name)
        with path.open("rb") as f:
            f.seek(offset)
            header = f.read(8)
            if header != struct.pack("<II", kind, size):
                raise ValueError(f"Unsupported Carbon chunk location or size: {name} ({role})")
            payload = f.read(size)
        if len(payload) != size or hashlib.sha256(payload).hexdigest() != digest:
            raise ValueError(f"Carbon chunk fingerprint differs: {name} ({role})")
        extracted[role + ".bun"] = header + payload
        records.append({"role": role, "source": name, "offset": offset, "chunk": f"{kind:08x}",
                        "bytes": size, "payloadSha256": digest})
    report = {"decoder": {"path": "speed.exe", "bytes": DECODER_BYTES, "sha256": DECODER_SHA256},
              "verifiedChunks": records, "publicRuntimeInstalled": False}
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
