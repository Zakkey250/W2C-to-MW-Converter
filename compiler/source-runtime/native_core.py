"""Rebuild target native core from fingerprinted pristine MW and original W2C.

No installed converted world/core payload is read. MW base resources retain
their original role; the new world does not reinterpret UI/vehicle assets.
"""
import hashlib,json,math,struct,sys
from pathlib import Path
from audit_breaker_sequences import chunks
def sha(b):return hashlib.sha256(b).hexdigest()
def only(b,tag):
    found=[p for t,a,p in chunks(b) if t==tag]
    if len(found)!=1:raise ValueError(f'Native core chunk identity {tag:08x}')
    return found[0]
def pack(tag,b):return struct.pack('<II',tag,len(b))+b
def regions(b):
    data=only(only(b,0x80034147),0x3414a);at=0;out=[]
    while at<len(data):
        if at+68>len(data):raise ValueError('Native region header')
        n,size=struct.unpack_from('<HH',data,at+64)
        if size!=68+n*8 or at+size>len(data):raise ValueError('Native region extent')
        out.append(data[at:at+size]);at+=size
    return out
def markers(b):
    found=[(off,p) for t,off,p in chunks(b) if t==0x34146]
    if len(found)!=1:raise ValueError('Original marker chunk identity')
    off,p=found[0];skip=(-(off+8))%16
    if len(p)<skip or (len(p)-skip)%48:raise ValueError('Original marker extent')
    return [p[i:i+48] for i in range(skip,len(p),48)]
def main(work,original,output,traffic_generation='native-traffic-360'):
    work=Path(work);original=Path(original);output=Path(output)
    if output.exists() and any(output.iterdir()):raise ValueError('Immutable native core output exists')
    sources=json.loads((work/'offline-original-references/manifest.json').read_bytes())['files'];base=work/'offline-original-references/mw-pristine';files={}
    for r in sources:
        if r['role'].startswith('mw-') and r['role'].endswith('-pristine'):
            p=Path(r['path']);b=p.read_bytes()
            if sha(b)!=r['sha256']:raise ValueError('Pristine MW input fingerprint changed')
            files[p.relative_to(base).as_posix()]=b
    src=original.read_bytes()
    if sha(src)!='461135c8b52fd2db27fe27bd53bd44ec126785299baa9cae5fc3ecc26eb3852d':raise ValueError('Original highest-quality master identity')
    traffic=(work/traffic_generation/'L2RA.BUN').read_bytes();proof=json.loads((work/traffic_generation/'traffic-conversion.json').read_bytes())
    if sha(traffic).upper()!=proof['outputSHA256'] or sha(files['TRACKS/L2RA.BUN']).upper()!=proof['baseSHA256'] or sha(src).upper()!=proof['sourceSHA256']:raise ValueError('Native traffic provenance')
    master,marker_data,added_count,marker_count=adapt_master(files['TRACKS/L2RA.BUN'],src,traffic)
    files['TRACKS/L2RA.BUN']=master
    globalb=files['GLOBAL/GLOBALB.BUN'];files['GLOBAL/GlobalB.lzc']=struct.pack('<4sIII',b'RAWW',0x1001,len(globalb),len(globalb)+16)+globalb
    files['pursuit-markers.bin']=marker_data
    output.mkdir(parents=True,exist_ok=True);rows=[]
    for name,b in files.items():
        dest=output/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b);rows.append(dict(path=name,bytes=len(b),sha256=sha(b)))
    report=dict(passed=True,files=rows,originalMasterSha256=sha(src),nativeTrafficOutputSha256=sha(traffic),newHidingRegions=added_count,pursuitMarkers=marker_count,oldConvertedWorldRead=False,existingMWNativeAssetsUnchanged=['GLOBAL/GLOBALB.BUN','GLOBAL/InGameA.bun','GLOBAL/InGameB.bun'],nativeStreamRequiresPristineMW=True,gameAccepted=False,compatibilityGate='V7')
    (output/'build-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))

def adapt_master(base,src,traffic):
    stock=regions(base);positions=[struct.unpack_from('<2f',r,4) for r in stock if struct.unpack_from('<I',r)[0]==8];added=[]
    for r in regions(src):
        if struct.unpack_from('<I',r)[0]!=8:continue
        r=bytearray(r)
        for at in (4,32,40,*range(68,len(r),8)):
            x,y=struct.unpack_from('<2f',r,at);struct.pack_into('<2f',r,at,x-6016,y+546)
        r[25:28]=bytes(3);xy=struct.unpack_from('<2f',r,4)
        if any(math.dist(xy,p)<3 for p in positions):continue
        if xy[0]>=200:raise ValueError('Ambiguous new hiding region')
        positions.append(xy);added.append(bytes(r))
    if len(added)!=26:raise ValueError('Original additional hiding-region count changed')
    combined=sorted(stock+added,key=lambda r:struct.unpack_from('<I',r)[0]);outer=only(traffic,0x80034147);inner=b''.join(pack(t,b''.join(combined) if t==0x3414a else p) for t,a,p in chunks(outer));padding=(-(len(inner)-len(outer)))%128
    if padding and padding<8:padding+=128
    if padding:inner+=pack(0,bytes(padding-8))
    master=b''.join(pack(t,inner if t==0x80034147 else p) for t,a,p in chunks(traffic))
    if regions(master)!=combined:raise ValueError('Native region readback mismatch')
    old=[r for r in markers(base) if struct.unpack_from('<I',r,8)[0]==0xec90a22a];added_markers=[]
    for r in markers(src):
        if struct.unpack_from('<I',r,8)[0]!=0xec90a22a:continue
        r=bytearray(r);x,y,z=struct.unpack_from('<3f',r,16);position=(x-6016,y+546,z)
        if any(math.dist(position[:2],struct.unpack_from('<2f',s,16))<5 for s in old):continue
        r[:8]=bytes(8);struct.pack_into('<3f',r,16,*position);struct.pack_into('<II',r,32,2600,0);added_markers.append(bytes(r))
    if len(old)!=37 or len(added_markers)!=36:raise ValueError('Original pursuit marker closure changed')
    marker_data=pack(0x34146,bytes(8)+b''.join(added_markers))
    return master,marker_data,len(added),len(added_markers)
if __name__=='__main__':main(*sys.argv[1:])
