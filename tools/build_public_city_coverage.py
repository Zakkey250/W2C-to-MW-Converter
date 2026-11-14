"""Derive a small HUD coverage grid from the local World and MW road coordinates."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import numpy as np
from scipy.spatial import cKDTree

PROJECT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(PROJECT / "compiler/source-world"), str(PROJECT / "compiler/source-runtime"),
                str(PROJECT / "public-converter")]
from bundle import Reader
from master import unique


def roads(data, stride):
    road = unique(data, 0x3B800)
    offset = bytes(road[:128]).index(b"PRAC") + 16
    tables = {}
    for at in range(offset, min(offset + 4096, len(road) - 15), 16):
        tag, packed, count, relative = struct.unpack_from("<4sIII", road, at)
        if tag == b"\xaa" * 4:
            break
        if tag in (b"dnNR", b"gsNR"):
            row_size = 32 if tag == b"dnNR" else stride
            size = packed >> 8
            start = at + relative
            if packed & 255 != 2 or size != count * row_size or not at + 16 <= start <= len(road) - size:
                raise ValueError("Invalid road directory")
            tables[tag] = road[start:start + size], count
    nodes, n = tables[b"dnNR"]
    segments, m = tables[b"gsNR"]
    points = np.ndarray((n, 3), "<f4", buffer=nodes, strides=(32, 4)).astype("f8")
    edges = np.ndarray((m, 2), "<u2", buffer=segments, strides=(stride, 2))
    if not np.isfinite(points).all() or edges.max() >= n:
        raise ValueError("Invalid road geometry")
    return points, edges


def track_plane(positions):
    return positions[:, [2, 0]] * [1, -1]


def district_triangles(bundle):
    reader = Reader(bundle)
    try:
        blob = reader.read(51, "environment/palmont-districts")
    finally:
        reader.close()
    magic, version, count = struct.unpack_from("<4sII", blob)
    if (magic, version, count) != (b"W2PD", 1, 22):
        raise ValueError("Wrong district source")
    at = 12
    all_triangles = []
    for i in range(count):
        tag, n = struct.unpack_from("<II", blob, at)
        if tag != i + 1:
            raise ValueError("Unexpected district order")
        at += 8
        all_triangles.extend(np.frombuffer(blob, "<f4", n * 6, at).reshape(n, 3, 2))
        at += n * 24
    if at != len(blob) or len(all_triangles) != 447:
        raise ValueError("Wrong district dimensions")
    return np.array(all_triangles, "f8")


def inside(points, triangles):
    result = np.zeros(len(points), dtype=bool)
    for tri in triangles:
        edge = np.roll(tri, -1, axis=0) - tri
        area = edge[0, 0] * (tri[2, 1] - tri[0, 1]) - edge[0, 1] * (tri[2, 0] - tri[0, 0])
        if abs(area) < 0.001:
            continue
        candidates = np.flatnonzero(np.all((points >= tri.min(0)) & (points <= tri.max(0)), axis=1))
        delta = points[candidates, None, :] - tri
        cross = edge[:, 0] * delta[:, :, 1] - edge[:, 1] * delta[:, :, 0]
        result[candidates] |= (cross.min(1) >= -0.001) | (cross.max(1) <= 0.001)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("world", "mw", "w2c", "district_pack", "output"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    data = {k: getattr(args, k).read_bytes() for k in ("world", "mw", "w2c")}
    world, edges = roads(data["world"], 24)
    stock, _ = roads(data["mw"], 22)
    w2c, _ = roads(data["w2c"], 22)
    if np.max(cKDTree(w2c).query(world)[0]) >= 1:
        raise ValueError("World roads do not align with this W2C source")
    world += [-546, 0, -6016]
    stock_distance = cKDTree(stock).query(world)[0]
    near_stock = (stock_distance[edges].max(1) < 15) & (stock_distance[edges].min(1) < 3)
    triangles = district_triangles(args.district_pack)
    segments = track_plane(world)[edges]
    sampled = []
    for a, b in segments:
        steps = max(1, int(np.ceil(np.linalg.norm(b - a) / 8)))
        sampled.append(np.linspace(a, b, steps + 1))
    rockport = np.concatenate([s for s, stock_segment in zip(sampled, near_stock) if stock_segment])
    other = np.concatenate([s for s, stock_segment in zip(sampled, near_stock) if not stock_segment])
    palmont = other[inside(other, triangles)]
    step = 16
    origin = np.floor((triangles.min((0, 1)) - 96) / step).astype(int) * step
    end = np.ceil((triangles.max((0, 1)) + 96) / step).astype(int) * step
    width, height = (end - origin) // step
    yy, xx = np.indices((height, width))
    points = origin + (np.column_stack((xx.ravel(), yy.ravel())) + 0.5) * step
    rockport_distance = cKDTree(rockport).query(points)[0]
    palmont_distance = cKDTree(palmont).query(points)[0]
    cells = np.zeros(len(points), dtype="u1")
    cells[(palmont_distance <= 80) & (palmont_distance + 16 < rockport_distance)] = 2
    cells[(rockport_distance <= 50) & (rockport_distance <= palmont_distance + 16)] = 1
    if not np.any(cells == 1) or not np.any(cells == 2):
        raise ValueError("Missing city coverage")
    encoded = struct.pack("<4sIiiIII", b"W2HG", 1, *origin, step, width, height) + cells.tobytes()
    args.output.write_bytes(encoded)
    print(json.dumps({"inputs": {k: hashlib.sha256(b).hexdigest() for k, b in data.items()},
                      "worldNodes": len(world), "stockNodesWithin1m": int(np.sum(stock_distance < 1)),
                      "rockportSegments": int(np.sum(near_stock)), "origin": origin.tolist(), "step": step,
                      "size": [int(width), int(height)], "cells": np.bincount(cells).tolist(),
                      "sha256": hashlib.sha256(encoded).hexdigest()}, indent=2))


if __name__ == "__main__":
    main()
