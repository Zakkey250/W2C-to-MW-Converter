"""Original visibility, model-marker flares, ambient motion and texture frames."""
from collections import Counter,defaultdict,OrderedDict
import hashlib,json,mmap,struct,sys,time
from pathlib import Path
import numpy as np
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'source-world'))
from source_ir import Container,scene,model,chunks
from master import unique,groups

def visibility(master):
    parent=unique(master,0x80034150);p=unique(parent,0x34152);at=0;bounds=[]
    while at<len(p):
        if at+48>len(p):raise ValueError('Visibility boundary header')
        sid,n,pan,d1,d2=struct.unpack_from('<HBBHH',p,at+8)
        if not 3<=n<64 or at+48+n*8>len(p):raise ValueError('Visibility polygon bounds')
        xy=np.frombuffer(p[at+48:at+48+8*n],'<f4').reshape(-1,2)
        if not np.isfinite(xy).all():raise ValueError('Visibility nonfinite boundary')
        bounds.append(dict(section=sid,panorama=pan,depth=[d1,d2],polygon=xy.tolist()));at+=48+8*n
    p=unique(parent,0x34153);at=0;visible={}
    while at<len(p):
        if at+20>len(p):raise ValueError('Visibility membership header')
        sid,most,maximum,n=struct.unpack_from('<HBBH',p,at+12)
        if not 0<n<=maximum or sid in visible or at+20+maximum*2>len(p):raise ValueError('Visibility exact dimensions')
        ids=struct.unpack_from('<'+str(n)+'H',p,at+18)
        if sid not in ids:raise ValueError('Visibility source section membership')
        visible[sid]=ids;at+=20+maximum*2
    result=[]
    for b in bounds:
        sid=b['section']
        if sid not in visible:continue
        refs=set(visible[sid])
        for d in b['depth']:refs.update(visible.get(d,()))
        result.append(dict(**b,visible=sorted(refs)))
    return result

def texture_animations(source_root, allow_shared=False, source_roles=None):
    bank=Container(source_root,'textures','textures-verified');summary=json.loads((Path(source_root)/'import-summary.json').read_text());sources={r['role']:r for r in summary['sourceRows']};packs=defaultdict(set)
    for r in bank.entries.values():o=r['metadata']['origin'];packs[o['role']].add(o['offset'])
    result=[];seen={};omissions=[]
    def animation_groups(raw,a,b,depth=0):
        if depth>8:raise ValueError('Texture animation parent depth')
        children=list(chunks(raw,a,b))
        if any(k==0x33312001 for k,off,s,e in children):yield children
        for k,off,s,e in children:
            if k&0x80000000:yield from animation_groups(raw,s,e,depth+1)
    for role,offsets in packs.items():
        if source_roles is not None and role not in source_roles:
            continue
        src=sources[role];path=Path(src['path'])
        if str(path).startswith(('\\\\','//')) and not allow_shared:
            raise ValueError('Texture animation input must be local unless shared originals are explicitly enabled: '+role)
        with path.open('rb') as f:
            if hashlib.file_digest(f,'sha256').hexdigest()!=src['sha256']:raise ValueError('Original animation source fingerprint changed')
        with path.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as raw:
            for offset in sorted(offsets):
                kind,n=struct.unpack_from('<II',raw,offset)
                if kind!=0xb3300000 or offset+8+n>len(raw):raise ValueError('Original texture-pack source bounds')
                for k,off,a,b in chunks(raw,offset+8,offset+8+n):
                    if k!=0xb3310000:continue
                    for children in animation_groups(raw,a,b):
                        for ordinal,(tag,where,start,end) in enumerate(children):
                            if tag!=0x33312001:continue
                            if end-start!=44 or ordinal+1>=len(children):raise ValueError('Texture animation original descriptor')
                            next_tag,_,fa,fb=children[ordinal+1];count,fps,base,reserved=struct.unpack_from('<4B',raw,start+28)
                            if next_tag!=0x33312002 or count==0 or fps==0 or base>1 or fb-fa!=count*12:raise ValueError('Texture animation exact frame dimensions')
                            hashes=[f'{struct.unpack_from("<I",raw,fa+i*12)[0]:08x}' for i in range(count)];name=bytes(raw[start+8:start+24]).split(b'\0')[0].decode('ascii');key=struct.unpack_from('<I',raw,start+24)[0];missing=[h for h in hashes if h not in bank.entries]
                            row=dict(key=f'{key:08x}',name=name,count=count,fps=fps,base=base,frames=hashes,sourceHeaderHex=bytes(raw[start:end]).hex(),sourceFramesHex=bytes(raw[fa:fb]).hex(),origin=dict(role=role,offset=where),missingOriginalFrames=missing)
                            signature=(tuple(hashes),fps,base)
                            if signature not in seen:seen[signature]=row;result.append(row)
    return result,omissions

def motions(bank,models):
    lookup={(r['metadata']['origin']['role'],r['metadata']['origin']['offset']):r for r in bank.entries.values()};result=[];missing=[]
    for key,row in bank.entries.items():
        o=row['metadata']['origin']
        if o['chunk']!='00037250':continue
        pad=(-(o['offset']+8))%16;p=bank.get(key)[pad:]
        if len(p)!=116:raise ValueError('Original ambient node exact layout')
        matrix=np.frombuffer(p[:64],'<f4').copy();uid=struct.unpack_from('<I',p,64)[0];parent,scale,flags=struct.unpack_from('<bBH',p,68);lods=struct.unpack_from('<3I',p,72);rotation=struct.unpack_from('<3h',p,92);section=struct.unpack_from('<H',p,98)[0];n,bits,initial=struct.unpack_from('<IIi',p,100)
        if parent!=-1 or n>100000 or not np.isfinite(matrix).all():raise ValueError('Original ambient parent/frame basis')
        frames=b'';frame_source=None
        if n:
            nxt=lookup.get((o['role'],o['offset']+8+row['bytes']))
            if not nxt or nxt['metadata']['origin']['chunk']!='00037240':raise ValueError('Ambient node frame ownership')
            no=nxt['metadata']['origin'];fp=(-(no['offset']+8))%16;frames=bytes(bank.get(nxt['key'])[fp:]);frame_source=nxt['key']
            if len(frames)!=n*64 or not np.isfinite(np.frombuffer(frames,'<f4')).all():raise ValueError('Original ambient exact finite matrices')
        selected=next((h for h in lods if f'{h:08x}' in models.entries),None)
        record=dict(sourceKey=f'{uid:08x}',section=section,sourceLods=[f'{h:08x}' for h in lods],model=f'{selected:08x}' if selected is not None else None,selectedSourceLod=lods.index(selected) if selected is not None else None,parent=parent,scale=scale,flags=flags,rotation=list(rotation),initial=initial,count=n,bits=bits,matrix=matrix.tolist(),sourceNodeSha256=row['sha256'],sourceFrameKey=frame_source,frameSha256=hashlib.sha256(frames).hexdigest(),frameBytes=frames)
        if selected is None:missing.append({k:v for k,v in record.items() if k!='frameBytes'})
        else:result.append(record)
    return result,missing

def compile_systems(source_root,master_path,output,marker_ledger_only=False,allow_shared=False,*,texture_animation_roles=None):
    started=time.monotonic();source_root=Path(source_root);output=Path(output);output.mkdir(parents=True,exist_ok=True)
    if (output/'scene-systems-summary.json').exists():raise ValueError('Immutable scene systems exist')
    master=Path(master_path).read_bytes();systems=Container(source_root,'systems');scenes=Container(source_root,'scenes');models=Container(source_root,'models');vis=visibility(master);nodes,missing_nodes=motions(systems,models);definitions,_=groups(master);summer={(s,i) for s,i,f in definitions[2]['overrides']}-{(s,i) for g in (1,7) for s,i,f in definitions[g]['overrides']};cache=OrderedDict();flares=[];flare_ids=set();unsupported=[];ghosts=[];node_poses=defaultdict(list)
    for n in nodes:node_poses[n['section'],n['model'],np.array(n['matrix'][12:15],'<f4').tobytes()].append(n['sourceKey'])
    def get_model(key):
        if key not in cache:
            m=model(models.get(key));cache[key]=(m['pivot'].copy(),[bytes(marker) for marker in m['markers']])
            if len(cache)>2048:cache.popitem(last=False)
        cache.move_to_end(key);return cache[key]
    for sid,row in scenes.entries.items():
        sid=int(sid);s=scene(scenes.get(str(sid)))
        for ordinal in range(s['instanceCount']):
            p=s['instances'][ordinal*96:(ordinal+1)*96];flags=struct.unpack_from('<I',p,12)[0]
            if flags&0x3a and (sid,ordinal) not in summer:continue
            ii=struct.unpack_from('<h',p,84)[0];lods=struct.unpack_from('<4I',s['infos'],ii*72+24);key=next((f'{h:08x}' for h in lods if f'{h:08x}' in models.entries),None)
            if key is None:continue
            pivot,markers=get_model(key);pose=node_poses.get((sid,key,bytes(p[32:44])))
            if pose:ghosts.append(dict(section=sid,instance=ordinal,model=key,originalMotionNodes=pose,identity='exact source section/model/position bits; packed scenery rotation remains independently retained'))
            if not markers:continue
            basis=np.frombuffer(p[44:80],'<f4').reshape(3,3).astype('f8');position=np.frombuffer(p[32:44],'<f4').astype('f8')+[-6016,546,0]
            for mi,marker in enumerate(markers):
                if len(marker)!=80:raise ValueError('Original environment marker stride')
                kind=struct.unpack_from('<i',marker,4)[0];custom=struct.unpack_from('<f',marker,8)[0]
                if custom>0 or not 0<=kind<=6:
                    unsupported.append(dict(section=sid,instance=ordinal,model=key,marker=mi,type=kind,
                                            sourceMarkerHex=marker.hex(),
                                            disposition='original-marker-retained-runtime-adapter-unverified'))
                    continue
                local=np.array(struct.unpack_from('<3f',marker,64))-pivot[3,:3];world=np.asarray(local@basis+position,'<f4');direction=np.array(struct.unpack_from('<3f',marker,32))@basis if 3<=kind<=5 else np.array([0.,0.,1.]);length=np.linalg.norm(direction)
                if not np.isfinite(world).all() or length<=1e-12:raise ValueError('Original flare transform')
                direction=np.asarray(direction/length,'<f4');tint=struct.unpack_from('<I',marker,12)[0];native_type=12 if kind==6 else 14+kind;raw=struct.pack('<3f3I3f',*world,tint,native_type,sid,*direction)
                if raw in flare_ids:continue
                flare_ids.add(raw);flares.append(dict(raw=raw,section=sid,instance=ordinal,model=key,marker=mi,type=kind,nativeType=native_type,sourceMarkerHex=marker.hex()))
    flare_bytes=struct.pack('<II',0x32464d57,len(flares))+b''.join(r['raw'] for r in flares)
    with (output/'unadapted-marker-source-index.jsonl').open('x',encoding='utf-8',newline='\n') as f:
        f.writelines(json.dumps(r,separators=(',',':'))+'\n' for r in unsupported)
    if marker_ledger_only:
        print(json.dumps(dict(unadaptedMarkers=len(unsupported),rebuiltFlareSha256=hashlib.sha256(flare_bytes).hexdigest(),
                              importedIRAndLocalMasterOnly=True,sharedInputsOpened=False)))
        return
    (output/'flares.bin').write_bytes(flare_bytes);(output/'flare-source-index.jsonl').write_text(''.join(json.dumps({k:v for k,v in r.items() if k!='raw'},separators=(',',':'))+'\n' for r in flares),encoding='utf-8')
    motion=bytearray(struct.pack('<II',0x31444d57,len(nodes)))
    for n in nodes:motion.extend(struct.pack('<5I4f16f',int(n['model'],16),n['section'],n['count'],n['bits'],n['scale'],*n['rotation'],n['initial'],*n['matrix']));motion.extend(n['frameBytes'])
    (output/'world-animations.bin').write_bytes(motion);(output/'motion-source-index.jsonl').write_text(''.join(json.dumps({k:v for k,v in n.items() if k!='frameBytes'},separators=(',',':'))+'\n' for n in nodes),encoding='utf-8');(output/'original-missing-motion-models.json').write_text(json.dumps(missing_nodes,indent=2),encoding='utf-8');(output/'animated-static-exclusions.json').write_text(json.dumps(ghosts,indent=2),encoding='utf-8')
    (output/'visibility.json').write_text(json.dumps(vis,indent=2),encoding='utf-8');(output/'visibility.txt').write_text(str(len(vis))+'\n'+'\n'.join(' '.join(map(str,[r['section'],len(r['polygon']),len(r['visible']),*[v for xy in r['polygon'] for v in xy],*r['visible']])) for r in vis)+'\n',encoding='ascii')
    animations,omissions=texture_animations(source_root,allow_shared,texture_animation_roles)
    (output/'texture-animations.json').write_text(json.dumps(animations,indent=2),encoding='utf-8')
    result=dict(visibilityZones=len(vis),flares=len(flares),unsupportedMarkerPlacements=len(unsupported),ambientMotions=len(nodes),motionFrames=sum(n['count'] for n in nodes),unavailableOriginalMotionNodes=len(missing_nodes),exactAnimatedStaticExclusions=len(ghosts),textureAnimations=len(animations),textureFrames=sum(r['count'] for r in animations),missingOriginalTextureFrames=sum(len(r['missingOriginalFrames']) for r in animations),textureDecoderOmissions=omissions,sourceTransformAndFrameBytesPreserved=True,oldConvertedEnvironmentRead=False,gameAccepted=False,seconds=time.monotonic()-started);(output/'scene-systems-summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser()
    p.add_argument('source_root');p.add_argument('master_path');p.add_argument('output')
    p.add_argument('--marker-ledger-only',action='store_true')
    p.add_argument('--allow-shared-originals',action='store_true')
    a=p.parse_args()
    compile_systems(a.source_root,a.master_path,a.output,a.marker_ledger_only,a.allow_shared_originals)
