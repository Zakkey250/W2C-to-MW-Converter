"""Compare all source-space mesh fields with an independent original reader.

This code never calls the Common/C# decoder. It records full original material
slots and auxiliary source ranges so the runtime compiler cannot silently treat
the decoded common-attribute subset as the entire Carbon material/vertex schema.
"""
from collections import Counter
from pathlib import Path
import hashlib,json,mmap,struct,sys
import numpy as np
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'compiler/source-world'))
from source_ir import chunks,Container,model
from inspect_raw_models import leaves
E=P/'evidence/source-world-20261110';W=Path(r'E:\NFSMW-Labs\W2CMW-v2\SourceWorld\complete-trial')

def main():
    cfg=json.loads((E/'source-complete-config.json').read_text(encoding='utf-8-sig'));path=Path(next(r['path'] for r in cfg['files'] if r['role']=='x360'))
    index=E/'independent-model-source-index.jsonl'
    result=verify(path,W,index,role='x360')
    assert result['statistics']['models']==44909 and result['statistics']['vertices']==15665266
    (E/'independent-model-verification.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))

def verify(path,source,index,*,role):
    bank=Container(source,'models');statistics=Counter();effects=Counter();strides=Counter();unknown=Counter();bad=[]
    with index.open('w',encoding='utf-8') as output,path.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
        for kind,top,a,b in chunks(data):
            if kind!=0x80134000:continue
            for kind,at,s,e in chunks(data,a,b):
                if kind!=0x80134010:continue
                leaf=list(leaves(data,s,e));header=next(x for x in leaf if x[0]==0x134011);hp=(header[2]+15)&~15;h=struct.unpack_from('<I',data,hp+16)[0];key=f'{h:08x}'
                m=model(bank.get(key));statistics['models']+=1
                assert h==m['hash'];assert np.array_equal(np.frombuffer(data[hp+32:hp+44]+data[hp+48:hp+60],'<u4'),m['bounds'].view('<u4'));assert np.array_equal(np.frombuffer(data[hp+64:hp+128],'<u4'),m['pivot'].view('<u4').ravel())
                textures=next((np.frombuffer(data[x:y],'<u4').reshape(-1,2)[:,0] for k,o,x,y in leaf if k==0x134012),np.array([],dtype='<u4'))
                raw_mats=[];vb=[];ib=None;marker=[];morph=[];source_meta=[]
                for k,o,x,y in leaf:
                    if k==0:continue
                    p=(x+15)&~15
                    if k==0x134b02:
                        assert (y-p)%144==0
                        for pos in range(p,y,144):raw_mats.append((pos,data[pos:pos+144]))
                    elif k==0x134b01:vb.append((p,y))
                    elif k==0x134b03:ib=(p,y)
                    elif k==0x13401a:
                        assert (y-p)%80==0
                        marker.extend(data[i:i+80] for i in range(p,y,80))
                    elif k==0x13401d:
                        assert (y-x)%12==0
                        morph.extend(struct.unpack_from('<IIf',data,i) for i in range(x,y,12))
                    elif k not in (0x134011,0x134012,0x134900,0x134c02):
                        source_meta.append(dict(kind=f'{k:08x}',offset=x,bytes=y-x,sha256=hashlib.sha256(data[x:y]).hexdigest()));unknown[f'{k:08x}']+=1
                assert len(raw_mats)==len(m['materials']) and [bytes(v) for v in m['markers']]==marker and tuple(v[0] for v in morph)==m['morphs']
                groups=[];full_mats=[]
                for ordinal,((pos,r),im) in enumerate(zip(raw_mats,m['materials'])):
                    effect=struct.unpack_from('<H',r,48)[0];flags=struct.unpack_from('<I',r,56)[0];nv=struct.unpack_from('<I',r,64)[0];tri,first=struct.unpack_from('<II',r,96)
                    if not groups or groups[-1]['effect']!=effect:groups.append(dict(effect=effect,vertices=0))
                    groups[-1]['vertices']+=nv;vi=len(groups)-1
                    tex=[int(textures[i]) for i in r[24:29]]
                    assert im['effect']==effect and im['flags']==flags and im['set']==vi and im['numVerts']==nv and len(im['indices'])==tri*3 and im['sourceIndexStart']==first
                    assert im['diffuse']==tex[0] and im['normal']==(tex[1] if r[25]!=r[24] else None) and im['specular']==(tex[3] if r[27]!=r[24] else None)
                    assert ib and ib[0]+(first+tri*3)*2<=ib[1]
                    assert np.array_equal(im['indices'],np.frombuffer(data[ib[0]+first*2:ib[0]+(first+tri*3)*2],'<u2'))
                    full_mats.append(dict(ordinal=ordinal,sourceOffset=pos,sourceHeaderHex=r.hex(),effect=effect,flags=flags,textures=dict(zip(['diffuse','normal','height','specular','opacity'],[f'{v:08x}' for v in tex])),lightMaterial=r[29]))
                    effects[effect]+=1;statistics['indices']+=tri*3
                assert len(vb)==len(groups)==len(m['vertices'])
                auxiliary=[];bad_count=0
                for vi,((begin,end),g,decoded) in enumerate(zip(vb,groups,m['vertices'])):
                    n=g['vertices'];assert n>0 and (end-begin)%n==0;stride=(end-begin)//n;effect=g['effect'];strides[f'{effect}:{stride}']+=1
                    raw=np.ndarray((n,stride//4),'<u4',buffer=data,offset=begin,strides=(stride,4));expected=np.zeros((n,20),'<u4');expected[:,:3]=raw[:,:3]
                    if effect in (0,1,3,4,15):expected[:,3:5]=raw[:,7:9];expected[:,5:8]=raw[:,3:6];expected[:,17]=raw[:,6];consumed=36
                    elif effect in (5,25):expected[:,3:5]=raw[:,3:5];expected[:,5:8]=raw[:,6:9];expected[:,17]=raw[:,5];consumed=36
                    elif effect==26:expected[:,3:5]=raw[:,4:6];expected[:,5:8]=raw[:,6:9];expected[:,17]=raw[:,3];consumed=36
                    else:raise ValueError(f'Independent shader layout {effect}')
                    expected[:,19]=17
                    if effect in (1,3,4,5):expected[:,8:11]=raw[:,9:12];expected[:,19]|=2;consumed=48
                    assert np.array_equal(expected,decoded),f'Source attribute bits differ {key} stream {vi}'
                    # Tangent W / secondary UV channels remain explicit source
                    # attributes, never assumed to be padding or discarded.
                    if stride>consumed:
                        auxiliary.append(dict(stream=vi,effect=effect,sourceOffset=begin,vertices=n,stride=stride,auxiliaryOffset=consumed,auxiliaryBytesPerVertex=stride-consumed,sha256=hashlib.sha256(raw[:,consumed//4:].tobytes()).hexdigest()));statistics['auxiliaryAttributeBytes']+=n*(stride-consumed)
                    bad_count+=int((~np.isfinite(raw[:,:3].view('<f4')).all(1)).sum());statistics['vertices']+=n
                    del raw,expected
                if bad_count:bad.append(dict(key=key,originalNonfiniteVertices=bad_count))
                output.write(json.dumps(dict(key=key,sourceRole=role,sourceObjectOffset=at,sourceObjectBytes=e-at,sourceObjectSha256=hashlib.sha256(data[at:e]).hexdigest(),fullMaterials=full_mats,vertexAuxiliary=auxiliary,otherSourceMetadata=source_meta,morphRaw=[dict(hash=f'{v[0]:08x}',runtimePointer=v[1],blend=v[2]) for v in morph]),separators=(',',':'))+'\n')
                m=im=decoded=None
    bank.close()
    result=dict(stage='Independent original mesh common-attribute verification',passed=True,statistics=dict(statistics),sourceEffects=dict(effects),streamStrides=dict(strides),allPositionNormalTangentColorUvIndexBitsEqual=True,originalInvalidVertices=bad,metadataRequiringRuntimeSemantics=dict(unknown),fullMaterialAndAuxiliarySourceRangesRetained=str(index),completeRuntimeConversion=False)
    return result
if __name__=='__main__':main()
