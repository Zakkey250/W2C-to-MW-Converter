"""Bounded independent W2C master tables; source IDs remain unchanged."""
from collections import Counter
import struct
import numpy as np
from source_ir import chunks

def unique(data,kind):
    found=[(a,b) for k,_,a,b in chunks(data) if k==kind]
    if len(found)!=1:raise ValueError(f'Expected one source chunk {kind:08x}')
    a,b=found[0];return memoryview(data)[a:b]

def graph(data):
    p=unique(data,0x3b800);marker=bytes(p[:128]).find(b'PRAC')
    if marker<0:raise ValueError('Road CARP signature')
    tables={};at=marker+16
    while at+16<=min(marker+4096,len(p)):
        tag,packed,count,rel=struct.unpack_from('<4sIII',p,at)
        if tag==b'\xaa'*4:break
        size=packed>>8;start=at+rel;kind=packed&255
        name=tag[::-1].decode('ascii')
        if name in tables:raise ValueError('Road directory duplicate')
        if kind==18:
            tables[name]=dict(count=count,inlineDescriptor=bytes(p[at:at+16]),kind=kind)
        else:
            if kind not in (2,3) or (size and start<at+16) or start+size>len(p):raise ValueError('Road directory range/type')
            tables[name]=dict(count=count,raw=p[start:start+size],offset=start,kind=kind)
        at+=16
    else:raise ValueError('Road directory terminator')
    for tag,stride in [('RNnd',32),('RNsg',22),('RNrd',8),('RNpf',64)]:
        t=tables[tag]
        if len(t['raw'])!=t['count']*stride:raise ValueError('Road table exact dimensions')
    node=tables['RNnd'];seg=tables['RNsg'];n=node['count'];m=seg['count']
    positions=np.ndarray((n,3),'<f4',buffer=node['raw'],strides=(32,4)).copy()
    if not np.isfinite(positions).all():raise ValueError('Road nonfinite node')
    refs=[]
    for i in range(n):
        ordinal,road,degree,pad=struct.unpack_from('<HHBB',node['raw'],32*i+12)
        links=struct.unpack_from('<7H',node['raw'],32*i+18)[:degree]
        if ordinal!=i or degree>7 or len(set(links))!=len(links) or any(x>=m for x in links):raise ValueError('Road adjacency bounds')
        refs.append(set(links))
    edges=np.empty((m,2),dtype='<u4');parent=list(range(n))
    def find(x):
        while parent[x]!=x:parent[x]=parent[parent[x]];x=parent[x]
        return x
    for i in range(m):
        a,b,length,optional,ordinal=struct.unpack_from('<5H',seg['raw'],22*i)
        if ordinal!=i or a>=n or b>=n or i not in refs[a] or i not in refs[b]:raise ValueError('Road segment reciprocal adjacency')
        edges[i]=a,b;parent[find(a)]=find(b)
    if sum(map(len,refs))!=2*m:raise ValueError('Road extraneous links')
    components=np.array([find(i) for i in range(n)],dtype='<u4');counts=Counter(map(int,components));largest=counts.most_common(1)[0][0]
    return dict(tables=tables,nodes=positions,edges=edges,components=components,main=np.flatnonzero(components==largest),componentSizes=sorted(counts.values(),reverse=True))

def sections(data,stream_bytes):
    p=unique(data,0x34110)
    if len(p)%92:raise ValueError('Section table exact stride')
    result={}
    for at in range(0,len(p),92):
        sid,rendered,visible=struct.unpack_from('<HBB',p,at+8)
        status,kind,offset,size,compressed,permanent=struct.unpack_from('<6I',p,at+12)
        x,y,radius=struct.unpack_from('<3f',p,at+40)
        if sid in result or status or rendered or visible or kind!=1 or offset+compressed>stream_bytes:raise ValueError('Section source identity/range')
        result[sid]=dict(name=bytes(p[at:at+8]).split(b'\0')[0].decode('ascii'),offset=offset,bytes=size,compressedBytes=compressed,permanentBytes=permanent,center=[x,y],radius=radius)
    return result

def groups(data):
    refs=unique(data,0x34108);p=unique(data,0x34109)
    if len(refs)%6:raise ValueError('Group override stride')
    overrides=list(struct.iter_unpack('<3H',refs));result={};at=0
    while at<len(p):
        if at+20>len(p):raise ValueError('Group header truncation')
        key,num,count,barrier,drive,race=struct.unpack_from('<IHHBBH',p,at+8)
        end=at+20+count*4
        if end>len(p) or num in result or num>=4096:raise ValueError('Group range/identity')
        indices=struct.unpack_from('<'+str(count)+'I',p,at+20)
        if any(i>=len(overrides) for i in indices):raise ValueError('Group override reference bounds')
        # The source adds one through four bytes, including when already aligned.
        stop=end+4-(end&3)
        if stop>len(p):raise ValueError('Group padding range')
        result[num]=dict(key=f'{key:08x}',barrier=barrier,driveThrough=drive,raceSection=race,indices=indices,overrides=[overrides[i] for i in indices],sourceOffset=at,sourceBytes=stop-at)
        at=stop
    return result,overrides
