"""Independent inspection of original model buffers, not Common's decoder."""
import json,mmap,struct,sys
import numpy as np
from pathlib import Path
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'compiler/source-world'))
from source_ir import chunks
E=P/'evidence/source-world-20261110'
TARGETS={0x86cd38d7,0xb4714323,0x812e8d81,0x283dce73}
def leaves(data,s,e):
    for k,o,a,b in chunks(data,s,e):
        if k&0x80000000:yield from leaves(data,a,b)
        else:yield k,o,a,b
def main():
    cfg=json.loads((E/'source-audit-config.json').read_text(encoding='utf-8-sig'));path=Path(next(r['path'] for r in cfg['files'] if r['role']=='x360'));results=[];counts={}
    with path.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
        for k,o,a,b in chunks(data):
            if k!=0x80134000:continue
            for t,at,s,e in chunks(data,a,b):
                counts[f'{t:08x}']=counts.get(f'{t:08x}',0)+1
                if t!=0x80134010:continue
                leaf=list(leaves(data,s,e));h=next((x for x in leaf if x[0]==0x134011),None)
                if not h:continue
                start=(h[2]+15)&~15;model=struct.unpack_from('<I',data,start+16)[0]
                if model not in TARGETS:continue
                row=dict(hash=f'{model:08x}',sourceOffset=at,sourceBytes=e-at,name=data[start+160:h[3]].split(b'\0')[0].decode('ascii'),buffers=[],materials=[])
                for kind,lo,begin,end in leaf:
                    p=(begin+15)&~15
                    if kind==0x134b01:row['buffers'].append(dict(offset=p,bytes=end-p))
                    elif kind==0x134b02:
                        if (end-p)%144:raise ValueError('Raw source material exact stride')
                        for atmat in range(p,end,144):
                            effect=struct.unpack_from('<H',data,atmat+48)[0];nv=struct.unpack_from('<I',data,atmat+64)[0];tri,first=struct.unpack_from('<II',data,atmat+96)
                            row['materials'].append(dict(effect=effect,vertices=nv,triangles=tri,first=first))
                groups=[]
                for mat in row['materials']:
                    if not groups or groups[-1]['effect']!=mat['effect']:groups.append(dict(effect=mat['effect'],vertices=0))
                    groups[-1]['vertices']+=mat['vertices']
                if len(groups)!=len(row['buffers']):raise ValueError('Independent source stream grouping')
                for buf,group in zip(row['buffers'],groups):
                    if buf['bytes']%group['vertices']:raise ValueError('Independent source stride divisibility')
                    stride=buf['bytes']//group['vertices'];xyz=np.ndarray((group['vertices'],3),'<f4',buffer=data,offset=buf['offset'],strides=(stride,4));bad=np.flatnonzero(~np.isfinite(xyz).all(1));buf.update(stride=stride,vertices=group['vertices'],effect=group['effect'],originalNonfinitePositions=len(bad),firstBadIndex=int(bad[0]) if len(bad) else None)
                    del xyz
                results.append(row)
    (E/'raw-invalid-model-inspection.json').write_text(json.dumps(dict(topChildren=counts,models=results),indent=2),encoding='utf-8');print(json.dumps(dict(topChildren=counts,models=results),indent=2))
if __name__=='__main__':main()
