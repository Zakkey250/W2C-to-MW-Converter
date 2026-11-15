"""Preserve the native models and textures requested by MW's roadblock spawner."""

import hashlib
import json
import mmap
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "source-world"))

from source_ir import Container, chunks
from toll_compile import native_models, native_textures, write_native_models, write_native_textures


MODELS = {
    0x2A908C13: ("XO_SPIKEBELT_1B_DW_00", {0x06C142FC, 0xD5FAA405}),
    0x3393B8E2: ("XO_SAWHORSE_1B_00", {0x3B1AD9AC, 0x4B1A66F9, 0x8C3E07E6, 0x9ED8F353, 0xBD712F3E}),
}
TEXTURES = set.union(*(keys for _, keys in MODELS.values()))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main(work, stream, output, *, imported=None):
    work, stream, output = Path(work), Path(stream), Path(output)
    imported = Path(imported) if imported is not None else work / "native-full-pristine-360"
    if output.exists():
        raise ValueError("Immutable utility generation exists")
    with stream.open("rb") as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
        source_sha = sha(data)
        rows = list(chunks(data))
        models, model_template = native_models(data, rows, MODELS.keys())
        textures, texture_template = native_textures(data, rows, TEXTURES)
    if models.keys() != MODELS.keys() or textures.keys() != TEXTURES:
        raise ValueError("Incomplete MW roadblock model or texture bank")
    model_index = Container(imported, "models")
    texture_index = Container(imported, "textures")
    try:
        for key, (name, texture_keys) in MODELS.items():
            entry = model_index.entries[f"{key:08x}"]["metadata"]
            if (entry["Name"].upper() != name or entry["origin"]["role"] != "mw-stream-pristine"
                    or models[key][2] != texture_keys):
                raise ValueError("MW roadblock model identity or texture closure")
        for key, (_, _, pixels) in textures.items():
            if bytes(texture_index.get(f"{key:08x}"))[128:] != pixels:
                raise ValueError("Independent normalized roadblock DDS differs")
    finally:
        texture_index.close()
        model_index.close()
    geometry = write_native_models(models, model_template, b"W2CMW_V2_NATIVE_UTIL")
    tpk = write_native_textures(textures, texture_template, b"W2CMW_V2_NATIVE_UTIL")
    output.mkdir()
    (output / "models.bun").write_bytes(geometry)
    (output / "textures.bin").write_bytes(tpk)
    report = {
        "originalStreamSha256": source_sha,
        "models": [{"key": f"{key:08x}", "name": MODELS[key][0], "originalOffset": offset,
                    "objectSha256": sha(payload)} for key, (offset, payload, _) in sorted(models.items())],
        "textures": [{"key": f"{key:08x}", "pixelSha256": sha(value[2])} for key, value in sorted(textures.items())],
        "modelsSha256": sha(geometry), "texturesSha256": sha(tpk),
    }
    (output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    main(*sys.argv[1:])
