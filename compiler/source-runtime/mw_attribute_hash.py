"""Hash ASCII AttribSys names using MW's lookup2 algorithm and seed."""

from struct import unpack_from

MASK = 0xFFFFFFFF


def mix(a, b, c):
    a = ((a - b - c) & MASK) ^ (c >> 13)
    b = ((b - c - a) & MASK) ^ ((a << 8) & MASK)
    c = ((c - a - b) & MASK) ^ (b >> 13)
    a = ((a - b - c) & MASK) ^ (c >> 12)
    b = ((b - c - a) & MASK) ^ ((a << 16) & MASK)
    c = ((c - a - b) & MASK) ^ (b >> 5)
    a = ((a - b - c) & MASK) ^ (c >> 3)
    b = ((b - c - a) & MASK) ^ ((a << 10) & MASK)
    c = ((c - a - b) & MASK) ^ (b >> 15)
    return a, b, c


def attribute_hash(name):
    data = name.encode("ascii")
    if not data:
        return 0
    if b"\0" in data:
        raise ValueError("NUL in attribute name")
    a = b = 0x9E3779B9
    c = 0xABCDEF00
    block_bytes = len(data) // 12 * 12
    for at in range(0, block_bytes, 12):
        x, y, z = unpack_from("<III", data, at)
        a, b, c = mix((a + x) & MASK, (b + y) & MASK, (c + z) & MASK)
    tail = data[block_bytes:]
    a = (a + int.from_bytes(tail[:4], "little")) & MASK
    b = (b + int.from_bytes(tail[4:8], "little")) & MASK
    c = (c + len(data) + (int.from_bytes(tail[8:], "little") << 8)) & MASK
    return mix(a, b, c)[2]
