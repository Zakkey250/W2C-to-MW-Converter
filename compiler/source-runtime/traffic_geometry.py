"""Road-table and polygon helpers shared by the public traffic converter."""
from collections import Counter, defaultdict
import math
import struct

import numpy as np

from calculate_port import chunk, walk

ORDER = ("CGcn", "CGrd", "RNhd", "RNnd", "RNpf", "RNrd", "RNsg")
REPLACED = ORDER[2:]
ROCKPORT_LEFT = -1404.353515625
BUCKET = 256.0


def registry(blob: bytes) -> dict:
    out = {}
    marker = blob.find(b"PRAC", 0, 128)
    for pos in range(marker + 16, marker + 16 + 16 * 128, 16):
        if blob[pos:pos + 4] == b"\xaa" * 4:
            break
        tag = blob[pos:pos + 4][::-1].decode("latin1")
        packed, count, rel = struct.unpack_from("<III", blob, pos + 4)
        if tag in ORDER:
            if packed & 255 != 2:
                raise ValueError(f"unexpected type for {tag}")
            start = pos + rel
            size = packed >> 8
            if start + size > len(blob):
                raise ValueError(f"{tag} exceeds blob")
            out[tag] = dict(pos=pos, count=count, data=blob[start:start + size])
    if set(out) != set(ORDER):
        raise ValueError(f"missing tags {set(ORDER) - set(out)}")
    return out


def align8(buf: bytearray) -> None:
    buf.extend(b"\0" * ((8 - len(buf)) % 16))


def replace_top(data: bytes, kind: int, body: bytes) -> bytes:
    matches = [(s, e) for k, s, e in walk(data) if k == kind]
    if len(matches) != 1:
        raise ValueError(f"expected one top chunk {kind:08x}")
    start, end = matches[0]
    return data[:start-8] + struct.pack("<II", kind, len(body)) + body + data[end:]


def replace_children(parent: bytes, changes: dict[int, bytes]) -> bytes:
    result = bytearray()
    found = set()
    for kind, start, end in walk(parent):
        body = changes.get(kind, parent[start:end])
        result += struct.pack("<II", kind, len(body)) + body
        if kind in changes:
            found.add(kind)
    if found != set(changes):
        raise ValueError(f"missing nested chunks {set(changes)-found}")
    return bytes(result)


def world_polygons(master: bytes):
    data = chunk(chunk(master, 0x80034150), 0x34152)
    result = {}
    at = 0
    while at + 48 <= len(data) and struct.unpack_from("<II", data, at) == (0, 0):
        pid, count = struct.unpack_from("<HB", data, at + 8)
        end = at + 48 + count * 8
        if count < 3 or end > len(data) or pid in result:
            raise ValueError("invalid World polygon")
        left, bottom, right, top = struct.unpack_from("<4f", data, at + 24)
        left -= 6016.0
        right -= 6016.0
        if left < ROCKPORT_LEFT:
            row = bytearray(data[at:at + 12] + data[at + 24:end])
            struct.pack_into('<II',row,0,11,11) # MW serialized list links; original W2C stores null runtime pointers
            for pos in range(12, len(row), 8):
                x, y = struct.unpack_from("<ff", row, pos)
                struct.pack_into("<ff", row, pos, x - 6016.0, y + 546.0)
            points = [struct.unpack_from("<ff", row, pos) for pos in range(36, len(row), 8)]
            result[pid] = (row, (left, bottom + 546.0, right, top + 546.0), points)
        at = end
    if at != len(data):
        raise ValueError("unparsed World polygon bytes")
    return result


def inside(points, x, y):
    hit = False
    for (ax, ay), (bx, by) in zip(points, points[1:] + points[:1]):
        if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
            hit = not hit
    return hit


def cost_rows(data: bytes):
    rows = []
    at = 0
    while at < len(data):
        if at + 20 > len(data):
            raise ValueError("short cost cell")
        cell = struct.unpack_from("<I", data, at + 4)[0]
        nr, ns = data[at + 8], data[at + 11]
        end = at + 20 + 4 * (nr + ns)
        if end > len(data):
            raise ValueError("invalid cost cell size")
        pairs = {struct.unpack_from("<HH", data, at + 20 + 4 * i) for i in range(nr)}
        rows.append((cell, pairs, data[at:end]))
        at = end
    return rows


def polygons(master: bytes):
    blob = chunk(chunk(master, 0x80034150), 0x34152)
    at = 0
    result = {}
    buckets = defaultdict(list)
    while at + 36 <= len(blob) and struct.unpack_from("<II", blob, at) == (11, 11):
        pid, n = struct.unpack_from("<HB", blob, at + 8)
        end = at + 36 + 8 * n
        if n < 3 or end > len(blob) or pid in result:
            raise ValueError("invalid or repeated path polygon")
        bbox = struct.unpack_from("<4f", blob, at + 12)
        points = [struct.unpack_from("<ff", blob, at + 36 + i * 8) for i in range(n)]
        result[pid] = (bbox, points)
        left, bottom, right, top = bbox
        for bx in range(math.floor(left / BUCKET), math.floor(right / BUCKET) + 1):
            for by in range(math.floor(bottom / BUCKET), math.floor(top / BUCKET) + 1):
                buckets[bx, by].append(pid)
        at = end
    if at != len(blob) or len(result) < 900:
        raise ValueError("incomplete combined path polygons")
    return result, buckets


def graph_components(node_count, edges):
    parents = list(range(node_count))

    def root(node):
        while parents[node] != node:
            parents[node] = parents[parents[node]]
            node = parents[node]
        return node

    for a, b in edges:
        parents[root(int(a))] = root(int(b))
    main = Counter(root(i) for i in range(node_count)).most_common(1)[0][0]
    return np.array([root(int(a)) == main for a, _ in edges], dtype=bool)
