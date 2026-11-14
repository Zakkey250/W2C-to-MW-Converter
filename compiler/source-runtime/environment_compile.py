"""Typed environmental records read only from fingerprinted original inputs."""
from collections import Counter,defaultdict
import hashlib,json,mmap,struct,sys
from pathlib import Path
import numpy as np
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'source-world'))
from source_ir import Container,chunks
from master import unique

def weather(master):
    parent=unique(master,0x80034147);data=unique(parent,0x3414a);at=0;result=[]
    while at<len(data):
        if at+68>len(data):raise ValueError('Source region header')
        count,size=struct.unpack_from('<HH',data,at+64)
        if size!=68+8*count or at+size>len(data) or count>128:raise ValueError('Source region dimensions')
        kind=struct.unpack_from('<I',data,at)[0]
        if kind in (3,4,5):
            elevation=struct.unpack_from('<f',data,at+20)[0];poly=np.frombuffer(data[at+68:at+size],'<f4').reshape(-1,2).copy();poly+=np.array([-6016,546],'<f4')
            if count<3 or not np.isfinite(poly).all() or not np.isfinite(elevation):raise ValueError('Source shelter polygon')
            result.append(dict(type=kind,elevation=elevation,polygon=poly.tolist()))
        at+=size
    return result

class Sun:
    def __init__(self,zones):
        self.zones=zones;self.grid=defaultdict(list)
        for i,z in enumerate(zones):
            p=np.asarray(z['polygon']);lo=np.floor(p.min(0)/64).astype(int);hi=np.floor(p.max(0)/64).astype(int)
            if np.prod(hi-lo+1)>1000000:raise ValueError('Sun grid bound')
            for x in range(lo[0],hi[0]+1):
                for y in range(lo[1],hi[1]+1):self.grid[x,y].append(i)
    def exposure(self,positions):
        result=np.ones(len(positions),'<f4');cells=np.floor(positions[:,:2]/64).astype('i4');_,first,inverse=np.unique(cells,axis=0,return_index=True,return_inverse=True)
        for group,at in enumerate(first):
            ids=np.flatnonzero(inverse==group);p=positions[ids];cover=np.zeros(len(ids),bool)
            for zi in self.grid.get(tuple(cells[at]),()):
                z=self.zones[zi];inside=np.zeros(len(ids),bool);a=z['polygon'][-1]
                for b in z['polygon']:
                    if a[1]!=b[1]:inside^=((a[1]>p[:,1])!=(b[1]>p[:,1]))&(p[:,0]<(b[0]-a[0])*(p[:,1]-a[1])/(b[1]-a[1])+a[0])
                    a=b
                cover|=inside&(p[:,2]<=z['elevation'])
            result[ids[cover]]=0
        return result

def compile_environment(root,master_path,output):
    root=Path(root);output=Path(output);output.mkdir(parents=True,exist_ok=True);master=Path(master_path).read_bytes();zones=weather(master)
    wb=bytearray(struct.pack('<3I',0x315a4d57,1,len(zones)))
    for z in zones:wb.extend(struct.pack('<IIf',z['type'],len(z['polygon']),z['elevation']));wb.extend(np.asarray(z['polygon'],'<f4').tobytes())
    if (output/'weather-zones.bin').exists():raise ValueError('Immutable environment outputs exist')
    (output/'weather-zones.bin').write_bytes(wb);(output/'weather-zones.json').write_text(json.dumps(dict(zones=zones),indent=2),encoding='utf-8')
    summary=json.loads((root/'import-summary.json').read_text());sources={r['role']:r for r in summary['sourceRows']};light_bank=Container(root,'lights');lights=[];dedup=set();raw_count=0
    for role,src in sources.items():
        records=[r for r in light_bank.entries.values() if r['metadata']['origin']['role']==role]
        if not records:continue
        path=Path(src['path'])
        if not path.exists():raise ValueError('Original light source unavailable')
        # Sources were SHA-verified at import. Repeat the file hash before
        # using records omitted by the old third-party LightPack projection.
        with path.open('rb') as f:
            h=hashlib.file_digest(f,'sha256').hexdigest()
        if h!=src['sha256']:raise ValueError('Original light source changed')
        with path.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
            for row in records:
                at=row['metadata']['origin']['offset'];kind,n=struct.unpack_from('<II',data,at)
                if kind!=0x80135000 or at+8+n>len(data):raise ValueError('Original light parent')
                found=0
                for k,off,a,b in chunks(data,at+8,at+8+n):
                    if k!=0x135003:continue
                    first=(a+15)&~15
                    if (b-first)%96:raise ValueError('Original full light stride')
                    for i in range(first,b,96):
                        r=bytes(data[i:i+96]);v=struct.unpack_from('<11f',r,16);color=struct.unpack_from('<I',r,12)[0]
                        if not np.isfinite(v).all():raise ValueError('Original full light finite values')
                        raw_count+=1;found+=1
                        # Same source light can appear in adjacent streamed
                        # packs. Identity includes every functional field.
                        key=r[:60]+r[62:]
                        if key in dedup:continue
                        dedup.add(key);pos=[v[0]-6016,v[1]+546,v[2]];rgb=[(color>>shift&255)/255 for shift in (0,8,16)]
                        if not 0<v[9]<10000 or not 0<=v[7]<10000:raise ValueError('Original light intensity/range')
                        lights.append(dict(position=pos,rgb=rgb,intensity=v[7],range=v[9],direction=list(v[4:7]),flags=1 if r[7]==1 else 0,inner=1.,outer=-1.,blink=0,sourceRecordHex=r.hex(),origin=dict(role=role,offset=i)))
                if found!=row['metadata']['count']:raise ValueError('Original light count disagrees with independent import')
    lb=bytearray(struct.pack('<II',0x324c4d57,len(lights)))
    for l in lights:lb.extend(struct.pack('<11fI2fI',*l['position'],*l['rgb'],l['intensity'],l['range'],*l['direction'],l['flags'],l['inner'],l['outer'],l['blink']))
    (output/'lights.bin').write_bytes(lb);(output/'lights.jsonl').write_text(''.join(json.dumps(l,separators=(',',':'))+'\n' for l in lights),encoding='utf-8')
    report=dict(source='fingerprinted original X360 and Carbon base',weatherZones=len(zones),weatherTypes=dict(Counter(z['type'] for z in zones)),originalLights=raw_count,uniqueLights=len(lights),lightPacks=len(light_bank.entries),sunsetFixturePresetsAdded=False,oldConvertedEnvironmentRead=False)
    (output/'environment-summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report));return report
if __name__=='__main__':compile_environment(*sys.argv[1:])
