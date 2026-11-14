"""Independent bounded reader for the C# source-space IR, not writer imports."""
from pathlib import Path
import hashlib
import json
import mmap
import struct
import numpy as np

def rows(root,name):
    with (Path(root)/(name+'.jsonl')).open(encoding='utf-8') as f:
        for line in f:
            yield json.loads(line)

class Container:
    def __init__(self,root,name,index_name=None):
        self.path=Path(root)/(name+'.sourceir');self.file=self.path.open('rb');self.data=mmap.mmap(self.file.fileno(),0,access=mmap.ACCESS_READ) if self.path.stat().st_size else b'';self.entries={}
        end=0
        for r in rows(root,index_name or name):
            at,n=r['offset'],r['bytes']
            if not isinstance(at,int) or not isinstance(n,int) or at<end or at%64 or n<0 or at+n>len(self.data) or n>64*1024**2:raise ValueError('Source IR bounded range')
            if self.data[end:at]!=bytes(at-end):raise ValueError('Source IR padding')
            if hashlib.sha256(self.data[at:at+n]).hexdigest()!=r['sha256']:raise ValueError('Source IR payload SHA')
            if r['key'] in self.entries:raise ValueError('Duplicate source identity')
            self.entries[r['key']]=r;end=at+n
        if end!=len(self.data):raise ValueError('Source IR unowned extent')
    def get(self,key):
        r=self.entries[key];return memoryview(self.data)[r['offset']:r['offset']+r['bytes']]
    def close(self):
        if isinstance(self.data,mmap.mmap):self.data.close()
        self.file.close()

class Cursor:
    def __init__(self,data):self.data=data;self.at=0
    def take(self,n):
        if n<0 or self.at+n>len(self.data):raise ValueError('Source IR truncation')
        result=self.data[self.at:self.at+n];self.at+=n;return result
    def values(self,fmt):return struct.unpack(fmt,self.take(struct.calcsize(fmt)))
    def align(self,n=4):
        pad=(-self.at)%n
        if bytes(self.take(pad))!=bytes(pad):raise ValueError('Source IR nonzero padding')
    def end(self):
        if self.at!=len(self.data):raise ValueError('Source IR trailing bytes')

def model(data):
    c=Cursor(data);head=c.take(128)
    if bytes(head[:8])!=b'W2SMOD1\0' or struct.unpack_from('<II',head,8)!=(1,128):raise ValueError('Model IR header')
    h,sets,mats,markers,morphs,reserved=struct.unpack_from('<6I',head,16)
    if reserved or not 0<sets<=128 or mats>4096 or markers>65536 or morphs>65536:raise ValueError('Model IR dimensions')
    vertices=[]
    for _ in range(sets):
        count,stride=c.values('<II')
        if count>10000000 or stride!=80:raise ValueError('Source vertex stride/count')
        v=np.frombuffer(c.take(count*80),dtype='<u4').reshape(count,20)
        if np.any(v[:,19]&0xffffffc0):raise ValueError('Unknown vertex presence bits')
        vertices.append(v)
    materials=[]
    for _ in range(mats):
        at=c.at;raw=c.take(128);hash_,flags,nv,vs,ni,diffuse,normal,spec,presence,effect,first=struct.unpack_from('<11I',raw)
        name_bytes=struct.unpack_from('<I',raw,68)[0]
        if vs>=sets or ni>30000000 or ni%3 or presence&~3 or name_bytes>65536 or bytes(raw[72:])!=bytes(56):raise ValueError('Source material fields')
        name=bytes(c.take(name_bytes)).decode('utf-8');c.align();indices=np.frombuffer(c.take(ni*2),dtype='<u2');c.align()
        if len(indices) and int(indices.max())>=len(vertices[vs]):raise ValueError('Source model index bounds')
        materials.append(dict(hash=hash_,flags=flags,numVerts=nv,set=vs,indices=indices,diffuse=diffuse,normal=normal if presence&1 else None,specular=spec if presence&2 else None,effect=effect,sourceIndexStart=first,name=name))
    marker_data=[]
    for _ in range(markers):
        n,=c.values('<I')
        if n>1048576:raise ValueError('Source marker bound')
        marker_data.append(c.take(n));c.align()
    morph_ids=c.values('<'+str(morphs)+'I') if morphs else ();c.end()
    return dict(hash=h,bounds=np.frombuffer(head[40:64],dtype='<f4'),pivot=np.frombuffer(head[64:128],dtype='<f4').reshape(4,4),vertices=vertices,materials=materials,markers=marker_data,morphs=morph_ids)

def scene(data):
    if len(data)<32 or bytes(data[:8])!=b'W2SSCNE1' or struct.unpack_from('<II',data,8)!=(1,32):raise ValueError('Scene IR header')
    section,infos,instances,reserved=struct.unpack_from('<4I',data,16)
    if reserved or infos>100000 or instances>1000000 or len(data)!=32+infos*72+instances*96:raise ValueError('Scene IR exact dimensions')
    raw_infos=data[32:32+infos*72];raw_instances=data[32+infos*72:]
    ix=np.frombuffer(raw_instances,dtype=np.dtype({'names':['index'],'formats':['<i2'],'offsets':[84],'itemsize':96}))['index']
    if len(ix) and (int(ix.min())<0 or int(ix.max())>=infos):raise ValueError('Scene info reference')
    return dict(section=section,infos=raw_infos,instances=raw_instances,infoCount=infos,instanceCount=instances)

def chunks(data,start=0,end=None):
    end=len(data) if end is None else end
    while start<end:
        if end-start<8:raise ValueError('Original chunk truncated')
        kind,n=struct.unpack_from('<II',data,start);stop=start+8+n
        if stop>end:raise ValueError('Original chunk exceeds container')
        yield kind,start,start+8,stop
        start=stop
