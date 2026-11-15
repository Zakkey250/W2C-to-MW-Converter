"""Locate the original Carbon camera groups referenced by W2C breaker engines."""

import hashlib
import struct

from audit_breaker_sequences import chunks, commands, sequences
from cameras_compile import bhash

MARKER_NAMES = {"WMConsScaffold": "ScaffoldingMoment", "WMTorusRoofShop": "TorusRoofMoment"}


def plan_breaker_cameras(sequence_data, engine_names, camera_sources, track):
    original = sequences(sequence_data)
    names = set()
    for engine in engine_names:
        record = original[engine]
        for kind, size, data in commands(record):
            if kind != 0x66666DC8:
                continue
            if size != 32:
                raise ValueError("Unsupported EPlayNIS record")
            key = struct.unpack_from("<I", data, 8)[0]
            name = record["entries"][key]["data"].split(b"\0", 1)[0].decode("ascii")
            if not name.startswith("WM") or len(name) > 64:
                raise ValueError("Unexpected original W2C camera name")
            names.add(name)
    if not names:
        raise ValueError("No Carbon breaker cameras referenced")
    wanted = {bhash(name) for name in names}
    if len(wanted) != len(names):
        raise ValueError("Original camera hash collision")

    selected = {}
    for encoding, payload in camera_sources:
        current = {}
        for tag, _, body in chunks(payload):
            if tag != 0x8003B200:
                continue
            for kind, _, group in chunks(body):
                if kind != 0x3B210 or len(group) < 8:
                    raise ValueError("Unknown original camera group")
                key = struct.unpack_from("<I", group)[0]
                if key not in wanted:
                    continue
                if key in current and current[key] != group:
                    raise ValueError("Conflicting camera definitions within a Carbon bank")
                current[key] = group
        # InGameB supplies the base definitions. InGameC supplies additional
        # scenes and can also contain a different revision of a base scene.
        for key, group in current.items():
            selected.setdefault(key, (encoding, group))
    missing = [name for name in sorted(names) if bhash(name) not in selected]

    marker_banks = [body for tag, _, body in chunks(track) if tag == 0x34146]
    if len(marker_banks) != 1 or marker_banks[0][:8] != b"\x11" * 8 or (len(marker_banks[0]) - 8) % 48:
        raise ValueError("Unsupported W2C camera marker bank")
    markers = {struct.unpack_from("<I", marker_banks[0], at + 8)[0]
               for at in range(8, len(marker_banks[0]), 48)}
    plan = []
    for name in sorted(names):
        if bhash(name) not in selected:
            continue
        encoding, group = selected[bhash(name)]
        marker = MARKER_NAMES.get(name, name[2:] + "Moment")
        plan.append(dict(source=name, target="w2v2_cam_" + name, marker=marker,
                         unplaced=bhash(marker) not in markers, sourceEncoding=encoding,
                         sourceGroupSha256=hashlib.sha256(group).hexdigest()))
    body = b"".join(struct.pack("<II", 0x3B210, len(group)) + group
                    for _, group in (selected[key] for key in sorted(selected)))
    return plan, struct.pack("<II", 0x8003B200, len(body)) + body, missing
