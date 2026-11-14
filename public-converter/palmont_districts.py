"""Compile neighborhood boundaries from the user's local Carbon territory data."""

import json
import math
import re
import struct


AREAS = {"area_muscle": (1, 5), "area_tuner": (6, 10),
         "area_exotic": (11, 15), "area_casino": (16, 22)}
NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)?"
POINT = re.compile(rf"X: ({NUMBER}) \| Y: ({NUMBER})")
TRIANGLE = re.compile(r"mPoint1: (\d+) \| mPoint2: (\d+) \| mPoint3: (\d+)")
REGION = re.compile(r"Name: ([A-Za-z_]+) \| mCurveStart: \d+ \| mCurveCount: \d+"
                    r" \| mTriangleStart: (\d+) \| mTriangleCount: (\d+)")


def entries(text, pattern):
    if not text.startswith("[") or not text.endswith("]"):
        raise ValueError("Invalid Carbon neighborhood list")
    for item in text[1:-1].split(","):
        match = pattern.fullmatch(item.strip())
        if not match:
            raise ValueError("Invalid Carbon neighborhood record")
        yield match.groups()


def compile_districts(audit_path):
    rows = [r for r in json.loads(audit_path.read_bytes())["rows"] if r["class"] == "gameplay"]
    areas = {r["name"]: r["fields"] for r in rows if r["name"] in AREAS}
    tags = {}
    for row in rows:
        if row.get("parent") != "territory_zone":
            continue
        area, _, _ = row["name"].partition("/")
        if area not in AREAS:
            continue
        fields = row["fields"]
        tag = int(fields["LocalizationTag"])
        key = area, fields["RegionName"]
        if key in tags or not AREAS[area][0] <= tag <= AREAS[area][1] or fields["MapArea"] != f"gameplay -> {area}":
            raise ValueError("Invalid Carbon neighborhood identity")
        tags[key] = tag
    districts = {}
    for area, fields in areas.items():
        points = [(float(x) - 6016, float(y) + 546) for x, y in entries(fields["MapCurvePoint"], POINT)]
        triangles = [tuple(map(int, row)) for row in entries(fields["MapTriangle"], TRIANGLE)]
        if any(not math.isfinite(v) or abs(v) > 10_000 for point in points for v in point):
            raise ValueError("Invalid Carbon neighborhood coordinates")
        if any(max(t) >= len(points) for t in triangles):
            raise ValueError("Invalid Carbon neighborhood vertex reference")
        cursor = 0
        for name, start, count in entries(fields["MapRegion"], REGION):
            start, count = int(start), int(count)
            tag = tags.pop((area, name), None)
            if tag is None or tag in districts or start != cursor or not 1 <= count <= 128 or start + count > len(triangles):
                raise ValueError("Invalid Carbon neighborhood triangle partition")
            cursor = start + count
            districts[tag] = [tuple(v for i in t for v in points[i]) for t in triangles[start:cursor]]
        if cursor != len(triangles):
            raise ValueError("Incomplete Carbon neighborhood triangle partition")
    if tags or sorted(districts) != list(range(1, 23)) or sum(map(len, districts.values())) != 447:
        raise ValueError("Incomplete Carbon neighborhood coverage")
    return (struct.pack("<4sII", b"W2PD", 1, len(districts))
            + b"".join(struct.pack("<II", tag, len(tris)) + b"".join(struct.pack("<6f", *t) for t in tris)
                       for tag, tris in sorted(districts.items())))
