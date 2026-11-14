"""Fail-closed offline registration and topology checks, before world generation.

CARP directory offsets are relative to each directory entry, NOT to a shared
data base. The endpoint/ordinal invariants verify that interpretation on both
inputs. Coordinates below are explicitly distinguished from rendering axes.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from collections import Counter, defaultdict
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree


def walk(data, start=0, end=None):
    end = len(data) if end is None else end
    while start < end:
        if start + 8 > end:
            raise ValueError(f"truncated chunk at {start:x}")
        kind, size = struct.unpack_from('<II', data, start)
        stop = start + 8 + size
        if stop > end:
            raise ValueError(f"chunk {kind:08x} exceeds container at {start:x}")
        yield kind, start + 8, stop
        start = stop


def chunk(data, kind):
    hits = [(s, e) for k, s, e in walk(data) if k == kind]
    if len(hits) != 1:
        raise ValueError(f"expected one {kind:08x}, got {len(hits)}")
    s, e = hits[0]
    return data[s:e]


def read_sections(data, stream):
    p = chunk(data, 0x34110)
    assert len(p) % 92 == 0
    rows = []
    for o in range(0, len(p), 92):
        number, rendered, visible = struct.unpack_from('<HBB', p, o + 8)
        status, file_type, offset, size, compressed_size, perm_size = struct.unpack_from('<6I', p, o + 12)
        center = struct.unpack_from('<2f', p, o + 40)
        radius, = struct.unpack_from('<f', p, o + 48)
        checksum, = struct.unpack_from('<I', p, o + 52)
        assert status == rendered == visible == 0
        assert file_type == 1, f"unsupported file type {file_type}"
        assert offset + compressed_size <= stream.stat().st_size
        rows.append(dict(name=p[o:o+8].split(b'\0')[0].decode('ascii'),
                         number=number, offset=offset, size=size,
                         compressed_size=compressed_size, perm_size=perm_size,
                         center=list(center), radius=radius, checksum=checksum))
    assert len({r['number'] for r in rows}) == len(rows), 'duplicate section number'
    return rows


def read_graph(data, segment_stride=22):
    p = chunk(data, 0x3b800)
    marker = p.find(b'PRAC', 0, 128)
    assert marker >= 0
    directory_end = marker + 16
    while p[directory_end:directory_end+4] != b'\xaa'*4:
        directory_end += 16
        assert directory_end < min(len(p), marker + 4096)
    def table(tag, stride):
        o = p.find(tag[::-1].encode(), marker + 16, directory_end)
        assert o >= 0
        packed, count, rel = struct.unpack_from('<III', p, o + 4)
        assert packed & 255 == 2
        assert packed >> 8 == count * stride
        start = o + rel
        assert start + count * stride <= len(p)
        return start, count
    ns, nc = table('RNnd', 32)
    ss, sc = table('RNsg', segment_stride)
    rs, rc = table('RNrd', 8)
    fs, fc = table('RNpf', 64)
    positions = np.array([struct.unpack_from('<3f', p, ns+i*32) for i in range(nc)])
    assert np.isfinite(positions).all()
    refs = []
    for i in range(nc):
        o = ns + 32*i
        ordinal, road, degree, pad = struct.unpack_from('<HHBB', p, o+12)
        assert ordinal == i
        assert degree <= 7
        # This field can be a signed sentinel; retained, never renumbered here.
        links = struct.unpack_from('<7H', p, o+18)[:degree]
        assert len(set(links)) == len(links)
        assert all(x < sc for x in links)
        refs.append(set(links))
    segs = []
    for i in range(sc):
        a, b, length, optional, ordinal = struct.unpack_from('<5H', p, ss+i*segment_stride)
        assert ordinal == i
        assert a < nc and b < nc
        assert i in refs[a] and i in refs[b], f"segment {i} lacks reciprocal adjacency"
        segs.append((a,b))
    for i, links in enumerate(refs):
        assert all(i in segs[s] for s in links), f"node {i} has extraneous adjacency"
    assert sum(map(len,refs)) == 2*sc
    parent=list(range(nc))
    def find(x):
        while parent[x] != x:
            parent[x]=parent[parent[x]]
            x=parent[x]
        return x
    for a,b in segs:
        parent[find(a)] = find(b)
    components=Counter(find(i) for i in range(nc))
    return positions, np.array(segs), dict(nodes=nc, segments=sc, roads=rc,
        profiles=fc, reciprocal_adjacency_errors=0,
        component_sizes=sorted(components.values(), reverse=True),
        bounds_raw_x_height_y=np.column_stack((positions.min(0), positions.max(0))).tolist(),
        directory_base='each tag address', node_offset=ns, segment_offset=ss)


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(8*1024*1024), b''): h.update(b)
    return h.hexdigest().upper()


def main():
    a=argparse.ArgumentParser()
    a.add_argument('--mw', type=Path, required=True)
    a.add_argument('--w2c', type=Path, required=True)
    a.add_argument('--w2c-stream', type=Path, required=True)
    a.add_argument('--output', type=Path, required=True)
    args=a.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    mwb=args.mw.read_bytes(); wb=args.w2c.read_bytes()
    mw, me, mi=read_graph(mwb); w, we, wi=read_graph(wb)
    # Initial translation vote uses height correspondence; it does not assume
    # that node/road/section indices remain the same between games.
    height=defaultdict(list)
    for v in mw: height[round(float(v[1]),2)].append(v)
    votes=Counter()
    for v in w:
        for m in height.get(round(float(v[1]),2),[]):
            votes[(round(float(v[0]-m[0]),1), round(float(v[2]-m[2]),1))]+=1
    best, n=votes.most_common(1)[0]
    offset=np.array([best[0],0,best[1]])
    shifted=w-offset
    dist, idx=cKDTree(mw).query(shifted)
    accepted=np.flatnonzero(dist<0.01)
    assert len(accepted)>1000, 'insufficient anchors'
    assert len(set(idx[accepted]))==len(accepted), 'ambiguous anchor mapping'
    offsets=w[accepted]-mw[idx[accepted]]
    exact=np.median(offsets,axis=0)
    assert np.max(np.abs(exact-offset))<0.001
    residual=shifted[accepted]-mw[idx[accepted]]
    spans=np.ptp(mw[idx[accepted]],axis=0)
    assert spans[0]>3000 and spans[2]>3000, 'anchors not geographically distributed'
    pair_map={int(i):int(idx[i]) for i in accepted}
    mwedges={tuple(sorted(map(int,s))) for s in me}
    both=[(int(i),int(j)) for i,j in we if i in pair_map and j in pair_map]
    matching=[(i,j) for i,j in both if tuple(sorted((pair_map[i],pair_map[j]))) in mwedges]
    ms=read_sections(mwb,args.mw.parent/'STREAML2RA.BUN')
    ws=read_sections(wb,args.w2c_stream)
    registration=dict(method='height vote, unique 3D nearest neighbors, reciprocal graph correspondence',
        source_to_mw_raw_translation=(-offset).tolist(),
        raw_axis_order=['horizontal_0','height','horizontal_1'],
        candidate_votes=[dict(offset=list(k),votes=v) for k,v in votes.most_common(5)],
        anchors=len(accepted), anchor_span_raw=spans.tolist(),
        max_residual_euclidean=float(np.linalg.norm(residual,axis=1).max()),
        rms_residual=float(np.sqrt(np.mean(residual**2))),
        edges_with_both_anchors=len(both), matching_mw_edges=len(matching),
        notes=['Render/scenery axes must be independently registered before mesh placement.',
               'New/modified W2C roads are not required to match original MW.'])
    result=dict(schema=2, mw=dict(path=str(args.mw),sha256=sha(args.mw),graph=mi,sections=len(ms)),
        w2c=dict(path=str(args.w2c),sha256=sha(args.w2c),graph=wi,sections=len(ws)),
        registration=registration,
        sections=dict(mw_min=min(x['number'] for x in ms),mw_max=max(x['number'] for x in ms),
            w2c_min=min(x['number'] for x in ws),w2c_max=max(x['number'] for x in ws),
            common_numbers=len({r['number'] for r in ms}&{r['number'] for r in ws}),
            note='number is u16 at +8; name[0:4] is NOT the section identifier'),
        runtime_world_ready=False)
    for name,value in [('preflight.json',result),('mw-sections.json',ms),('w2c-sections.json',ws),
                      ('anchor-correspondence.json',[dict(w2c=int(i),mw=int(idx[i]),residual=float(dist[i])) for i in accepted])]:
        (args.output/name).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    np.savez_compressed(args.output/'road-graphs.npz',mw_nodes=mw,mw_edges=me,w2c_nodes=w,
                        w2c_edges=we,w2c_aligned=shifted,anchors_w2c=accepted,anchors_mw=idx[accepted])
    print(json.dumps(result,indent=2))


if __name__=='__main__': main()
