"""Rebuild target-authored barriers from pristine MW models and current authoring.

The existing event/group placements are target authoring, not W2C geometry.
No converted barrier meshes, DDS files, or old converter outputs are inputs.
"""
import hashlib,json,struct,sys
from collections import defaultdict
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'source-world'))
from source_ir import Container,Cursor,model,chunks
ALIAS=0x6e15a923

def main(authoring,pristine,master,output,*,master_role='mw-master'):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    if (output/'barrier-meshes.bin').exists():raise ValueError('Immutable barrier generation exists')
    authoring=Path(authoring);summary=json.loads((authoring/'import-summary.json').read_bytes());expected=next(r for r in summary['sources'] if r['role']==master_role);raw=Path(master).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=expected['sha256']:raise ValueError('Authored target master fingerprint')
    def leaves(a=0,b=None):
        for k,o,s,e in chunks(raw,a,b):
            if k&0x80000000:yield from leaves(s,e)
            else:yield k,raw[s:e]
    data=dict(leaves());overrides=list(struct.iter_unpack('<3H',data[0x34108]));groups=data[0x34109];at=0;refs=defaultdict(list)
    while at<len(groups):
        key,num,n,barrier,drive,race=struct.unpack_from('<IHHBBH',groups,at+8)
        if num>=4096:raise ValueError('Authored group cap')
        for i in struct.unpack_from('<'+str(n)+'H',groups,at+20):
            section,index,flags=overrides[i];refs[section,index].append((num,key,flags))
        at=(at+20+2*n+3)&~3
    if at!=len(groups):raise ValueError('Authored group extent')
    bank=Container(pristine,'models');texture=Container(pristine,'textures');scene=Container(authoring,'scenes');wanted={'54a4fc3f','54b22557'};models={k:model(bank.get(k)) for k in wanted};rows=[]
    for key in scene.entries:
        c=Cursor(scene.get(key));head=c.take(32)
        if bytes(head[:8])!=b'W2TSCN1\0':raise ValueError('Target scene identity')
        section,ni,nv,reserved=struct.unpack_from('<4I',head,16);infos=[]
        for _ in range(ni):
            solid,n=c.values('<II');c.take(n);c.align();infos.append(f'{solid:08x}')
        for index in range(nv):
            flags,info=c.values('<Ii');matrix=np.array(c.values('<16f'),dtype='<f4').reshape(4,4);c.take(24);solid=infos[info]
            if solid not in wanted:continue
            binding=refs[section,index]
            if len(binding)!=1:raise ValueError('Barrier exact group binding')
            rows.append((section,index,solid,flags,matrix,binding[0]))
        c.end()
    if not rows or len(rows)>50000:raise ValueError('Barrier placement bounds')
    meshes=bytearray(struct.pack('<II',0x31424d57,len(rows)));bindings=bytearray(struct.pack('<II',0x31474d57,len(rows)));reverse=bytearray(struct.pack('<II',0x32524d57,len(rows)));used=set()
    for section,index,key,flags,matrix,binding in rows:
        groups_out=[];vertices=[];reversed_positions=[];indices=[]
        for mat in models[key]['materials']:
            v=models[key]['vertices'][mat['set']];p=v[:,:3].view('<f4');h=np.c_[p,np.ones(len(p),dtype='<f4')];world=(h@matrix)[:,:3];h[:,0]*=-1;reversed_positions.extend((h@matrix)[:,:3]);low=world.min(0);high=world.max(0);fv=len(vertices);fi=len(indices);color=v[:,17]
            # Preserve the original native barrier's position/color/UV data.
            for i in range(len(v)):vertices.append(struct.pack('<3fI2f',*world[i],int(color[i]),*v[i,3:5].view('<f4')))
            indices.extend(int(i) for i in mat['indices']);alias=mat['diffuse']^ALIAS;used.add(mat['diffuse']);groups_out.append(struct.pack('<5I6f',alias,fv,len(v),fi,len(indices)-fi,*low,*high))
        all_positions=np.array([struct.unpack_from('<3f',v) for v in vertices],dtype='<f4');lo=all_positions.min(0);hi=all_positions.max(0)
        meshes.extend(struct.pack('<7I6f',section,index,int(key,16),flags,len(groups_out),len(vertices),len(indices),*lo,*hi));meshes.extend(b''.join(groups_out));meshes.extend(b''.join(vertices));meshes.extend(struct.pack('<'+str(len(indices))+'I',*indices));bindings.extend(struct.pack('<5I',section,index,*binding));reverse.extend(struct.pack('<3I',section,index,len(vertices)));reverse.extend(np.asarray(reversed_positions,'<f4').tobytes())
    outputs={'barrier-meshes.bin':meshes,'barrier-groups.bin':bindings,'barrier-reverse-vertices.bin':reverse};materials=[];scrolls=[];textures=[]
    for h in sorted(used):
        key=f'{h:08x}';r=texture.entries[key];meta=r['metadata']['texture'];alias=h^ALIAS;blob=bytes(texture.get(key));name=f'{alias:08x}.dds';outputs[name]=blob;materials.append([alias,*[meta[k] for k in ('AlphaUsageType','AlphaBlendType','RenderFlags','TilableUV')]]);scrolls.append([alias,*[meta[k] for k in ('ScrollType','ScrollTimeStep','ScrollSpeedS','ScrollSpeedT')]]);textures.append(dict(alias=f'{alias:08x}',sourceKey=key,bytes=len(blob),sha256=r['sha256'],source=meta))
    outputs['materials.json']=json.dumps(dict(materials=materials,scrolls=scrolls),separators=(',',':')).encode()
    for name,blob in outputs.items():(output/name).write_bytes(blob)
    result=dict(placements=len(rows),pristineModels=sorted(wanted),textures=textures,authoredMasterSha256=expected['sha256'],oldConvertedAssetsUsed=False,files={n:dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest()) for n,b in outputs.items()})
    (output/'barrier-summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(dict(placements=len(rows),textures=len(textures),bytes=sum(len(b) for b in outputs.values()),oldConvertedAssetsUsed=False)))
if __name__=='__main__':main(*sys.argv[1:])
