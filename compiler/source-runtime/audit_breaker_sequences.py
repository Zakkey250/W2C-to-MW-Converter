"""Read-only CARP sequencer audit for the Carbon pursuit-breaker prototype."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def chunks(data):
    offset = 0
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError('Truncated chunk header')
        tag, size = struct.unpack_from('<II', data, offset)
        end = offset + 8 + size
        if end > len(data):
            raise ValueError('Truncated chunk payload')
        yield tag, offset, data[offset + 8:end]
        offset = end


def entries(data):
    marker = data.find(b'PRAC', 0, 128)
    if marker < 0:
        raise ValueError('Missing CARP header')
    _, version, count, flags = struct.unpack_from('<4I', data, marker)
    if version != 26 or not 1 <= count <= 4096:
        raise ValueError('Unsupported CARP directory')
    result = {}
    for index in range(count):
        offset = marker + 16 + 16 * index
        tag, packed, key, relative = struct.unpack_from('<4I', data, offset)
        start, size = offset + relative, packed >> 8
        if start < marker + 16 + count * 16 or start + size > len(data):
            raise ValueError('CARP extent')
        if tag in result:
            raise ValueError('Duplicate CARP tag')
        result[tag] = dict(kind=packed & 255, key=key, data=data[start:start+size])
    return result


def sequences(data):
    result = {}
    for tag, offset, raw in chunks(data):
        if tag == 0x8003B810:
            for name, record in sequences(raw).items():
                if name in result:
                    raise ValueError('Duplicate sequence name')
                result[name] = record
        elif tag == 0x3B811:
            table = entries(raw)
            names = [v['data'].split(b'\0')[0].decode('ascii')
                     for k, v in table.items() if k >> 16 == 0x694E]
            if not names:
                raise ValueError('Sequence identity')
            for name in names:
                if name in result:
                    raise ValueError('Duplicate sequence name')
                result[name] = dict(raw=raw, entries=table, offset=offset, names=names)
    return result


def summary(record):
    table = record['entries']
    return dict(
        bytes=len(record['raw']),
        sha256=hashlib.sha256(record['raw']).hexdigest(),
        strings=[s.decode('ascii') for s in table[0x53747273]['data'].split(b'\0') if s],
        entries=[dict(tag=f'{k:08X}', kind=v['kind'], key=f"{v['key']:08X}",
                      size=len(v['data']), hex=v['data'].hex())
                 for k, v in table.items() if k != 0x53747273])


def commands(record):
    result = []
    for tag, entry in record['entries'].items():
        if tag >> 16 != 0x656C:
            continue
        raw = entry['data']
        count = struct.unpack_from('<I', raw)[0]
        if 16 + count * 16 > len(raw):
            raise ValueError('Command directory')
        for i in range(count):
            offset = 16 + 16 * i
            kind, size, relative, unused = struct.unpack_from('<4I', raw, offset)
            start = offset + relative
            if start < 16 + count * 16 or start + size > len(raw):
                raise ValueError('Command extent')
            if struct.unpack_from('<I', raw, start)[0] != kind:
                raise ValueError('Command type mismatch')
            result.append((kind, size, raw[start:start+size]))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--carbon', type=Path, required=True)
    parser.add_argument('--mw', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, target = sequences(args.carbon.read_bytes()), sequences(args.mw.read_bytes())
    report = dict(carbon=str(args.carbon), mw=str(args.mw),
                  carbonCount=len(source), mwCount=len(target),
                  absentFromMW=sorted(source.keys()-target.keys()),
                  sharedChanged=sorted(k for k in source.keys() & target.keys()
                                       if source[k]['raw'] != target[k]['raw']),
                  carbonSequences={k:summary(v) for k,v in source.items()},
                  mwSequences={k:summary(v) for k,v in target.items()})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if not k.endswith('Sequences')},indent=2))


if __name__ == '__main__':
    main()
