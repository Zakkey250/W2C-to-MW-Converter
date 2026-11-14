"""Direct original-source placement baking to accepted bounded 40B pages.

No FBX, Assimp, old converted terrain or old loader is an input. The frozen
V2 repacker is reused as a lossless backend for newly generated CPU streams.
"""
from collections import Counter,defaultdict,OrderedDict
import hashlib,json,mmap,struct,subprocess,sys,time
from pathlib import Path
import numpy as np
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'source-world'))
from source_ir import Container,scene
from master import groups
from environment_compile import Sun

class GPU:
    def __init__(self,root):
        self.file=(Path(root)/'models.gpu').open('rb');self.data=mmap.mmap(self.file.fileno(),0,access=mmap.ACCESS_READ);self.rows={r['key']:r for r in map(json.loads,(Path(root)/'models.gpu.jsonl').open(encoding='utf-8'))};self.cache=OrderedDict()
    def get(self,key):
        if key in self.cache:self.cache.move_to_end(key);return self.cache[key]
        r=self.rows[key];b=memoryview(self.data)[r['offset']:r['offset']+r['bytes']]
        if hashlib.sha256(b).hexdigest()!=r['sha256'] or bytes(b[:8])!=b'W2SGPU1\0':raise ValueError('Verified GPU input changed')
        nm,nc=struct.unpack_from('<II',b,20);ma,ca=struct.unpack_from('<QQ',b,64);mats=[struct.unpack_from('<12I',b,ma+96*i) for i in range(nm)];result=[]
        for i in range(nc):
            ordinal,mi,vc,ic,vo,io=struct.unpack_from('<4I2Q',b,ca+96*i)
            result.append((mats[mi],np.frombuffer(b[vo:vo+vc*40],'<u4').reshape(-1,10),np.frombuffer(b[io:io+ic*2],'<u2')))
        value=(r['metadata']['Name'],result);self.cache[key]=value
        if len(self.cache)>2048:self.cache.popitem(last=False)
        return value

class Lights:
    def __init__(self,root):
        self.rows=[json.loads(s) for s in (Path(root)/'lights.jsonl').open(encoding='utf-8')];self.grid=defaultdict(list)
        for i,r in enumerate(self.rows):self.grid[tuple(np.floor(np.array(r['position'][:2])/64).astype(int))].append(i)
    def indices(self,lo,hi):
        mid=(lo+hi)/2;gx,gy=np.floor(mid[:2]/64).astype(int);eligible=[]
        for x in range(gx-2,gx+3):
            for y in range(gy-2,gy+3):
                for i in self.grid.get((x,y),()):
                    l=self.rows[i];p=np.array(l['position']);delta=np.maximum(np.maximum(lo-p,p-hi),0)
                    if delta@delta<=l['range']**2:eligible.append((float((p-mid)@(p-mid)),i))
        eligible.sort();night=[i for d,i in eligible[:8]];day=[i for d,i in eligible if not self.rows[i]['flags']&1][:8]
        return tuple(day+[0xffffffff]*(8-len(day))+night+[0xffffffff]*(8-len(night)))

def material_bank(root,destination):
    textures=Container(root,'textures','textures-verified');states={};payload=bytearray(128);payload[:8]=b'W2MATB1\0';default=(0,0,1,3)
    canonical={int(key,16):row['metadata']['texture'] for key,row in textures.entries.items() if '/' not in key}
    struct.pack_into('<4I',payload,8,1,len(canonical),0,88);struct.pack_into('<4I',payload,24,*default);struct.pack_into('<3f',payload,76,1,1,1)
    for h,t in sorted(canonical.items()):
        state=(t['AlphaUsageType'],t['AlphaBlendType'],t['RenderFlags'],t['TilableUV']);states[h]=state
        env=struct.pack('<8iI4f3i',t['ScrollType'],t['ScrollTimeStep'],t['ScrollSpeedS'],t['ScrollSpeedT'],t['OffsetS'],t['OffsetT'],t['ScaleS'],t['ScaleT'],0,1,1,1,0,0,0,0)
        payload.extend(struct.pack('<I4I',h,*state));payload.extend(env);payload.extend(bytes(4))
    Path(destination).write_bytes(payload);return states

def write_cpu(destination,sid,pieces,states,lights):
    # A source placement cluster is already bounded. Spatial tiling retains
    # alpha-over submission order while opaque material tiles may batch.
    vb=[];ib=[];meshes=[];mesh_lights=[];vcount=icount=0;batches=OrderedDict()
    for sequence,(texture,v,indices) in enumerate(pieces):
        triangles=indices.reshape(-1,3);center=v[:,:3].view('<f4')[triangles].mean(1);tile=np.floor(center[:,:2]/32).astype('i4')
        blend=states.get(texture,(0,0,1,3))[1]
        if blend and blend!=2:tile[:]=tile[0]  # Preserve all alpha-over triangle order.
        _,first,inverse=np.unique(tile,axis=0,return_index=True,return_inverse=True)
        for group in np.argsort(first):
            selected=triangles[inverse==group].ravel();lo=v[:,:3].view('<f4')[selected].min(0);hi=v[:,:3].view('<f4')[selected].max(0);ls=lights.indices(lo,hi);blend=states.get(texture,(0,0,1,3))[1]
            key=(texture,*tile[first[group]],*ls,sequence if blend and blend!=2 else -1)
            if key not in batches:batches[key]=[]
            batches[key].append((v,selected,lo,hi))
    shadow=[];shadow_count=0;passes=[[],[]]
    for key,parts in batches.items():
        texture=key[0];verts=[];indices=[];count=0
        for v,ix,lo,hi in parts:
            # Only referenced source vertices need world-space storage. No
            # quantization; the repacker welds exact final 40-byte values.
            remap,back=np.unique(ix,return_inverse=True);verts.append(v[remap]);indices.append(np.asarray(back+count,'<u4'));count+=len(remap)
        v=np.concatenate(verts);ix=np.concatenate(indices);lo=v[:,:3].view('<f4').min(0);hi=v[:,:3].view('<f4').max(0);mi=len(meshes)
        meshes.append(struct.pack('<5I6f',texture,vcount,len(v),icount,len(ix),*lo,*hi));mesh_lights.append(struct.pack('<16I',*key[3:19]));passes[bool(states.get(texture,(0,0,1,3))[1])].append(mi)
        state=states.get(texture,(0,0,1,3))
        if not state[1] and state[0]!=1 and texture not in (0x57fc91b7,0xd2e09151,0x29d9478c):shadow.append(ix+vcount);shadow_count+=len(ix)
        vb.append(v);ib.append(ix);vcount+=len(v);icount+=len(ix)
    if not meshes:return None
    vertices=np.concatenate(vb);indices=np.concatenate(ib);lo=vertices[:,:3].view('<f4').min(0);hi=vertices[:,:3].view('<f4').max(0);header=bytearray(128);header[:8]=b'W2ORCL2\0'
    struct.pack_into('<12I6f',header,8,1,128,sid,vcount,icount,len(meshes),len(meshes),len(meshes),shadow_count,1 if shadow_count else 0,len(passes[0]),len(passes[1]),*lo,*hi)
    with Path(destination).open('xb') as f:
        f.write(header);f.write(vertices.tobytes());f.write(indices.tobytes());f.write(b''.join(meshes));f.write(b''.join(mesh_lights));f.write(bytes(len(meshes)*4))
        if shadow:f.write(np.concatenate(shadow).astype('<u4').tobytes());f.write(struct.pack('<3I',0,0,shadow_count))
        for p in passes:f.write(np.asarray(p,'<u4').tobytes())
    return dict(sourceSection=sid,sourceVertexCount=vcount,sourceIndexCount=icount,sourceMeshCount=len(meshes),bounds=np.r_[lo,hi].tolist(),textures=sorted({p[0] for p in pieces}),originalSourceBakedDirectly=True)

# Exact W2C 2.0.0 smackable collection keys for small scenery. Objects that
# trigger events, block routes or support a collapse remain in the native
# prop/sequence pipeline; never turn those into inert terrain.
PUBLIC_DECORATION_PARAMETERS = frozenset((
    0x97324DAD, # news box
    0xD96C718E, # garbage bag
    0xD7115DF4, # bench
    0x390CA828, # cone
    0x47CC52B6, # garbage can
    0x084D25C6, # shrub
    0x786EF95D, # crate
    0xE01B8839, # parking meter
    0xBE35A64A, # chair
    0xE431296C, # cardboard box
    0x2CE93CEA, # payphone
    0xEC8FF7FC, # hydrant
    0x89B6D9FA, # tire
    0xB45DD668, # trash bin
    0x13750F5F, # vending machine
    0xBEEC7818, # patio table
    0x163EF67F, # bicycle
    0x08758C56, # picnic table
    0xC6CECF83, # lawn chair
    0x650B598B, # recycling bin
))

# W2C includes these facility meshes even when a Carbon-free conversion has
# no compatible MW destruction tree for them. Keep the original roof span,
# post height and placement. A matched native placement always takes
# precedence so a working breaker never leaves a second intact roof behind.
PUBLIC_VISUAL_FACILITY_PARTS = frozenset((
    (0x7A52AED8, 0x8F9C801A), # W2C gas station support
    (0xF30B497B, 0xD3E99EE0), # W2C gas station canopy
    (0xCE09D880, 0x600B6636), # W2C tire shop post B
    (0xCE09D880, 0x786DDC67), # W2C tire shop post A
    (0xF1A2688A, 0x7D6FF632), # W2C tire shop awning B
    (0xF1A2688A, 0x8DA34D84), # W2C tire shop awning A
))


def decorative_bounds(raw):
    lower = np.frombuffer(raw[:12], "<f4")
    upper = np.frombuffer(raw[16:28], "<f4")
    size = upper - lower
    return bool(np.isfinite(size).all() and (size >= 0).all() and (size <= 3.5).all())


def compile_terrain(source,gpu_root,master_path,environment,output,repacker, *, public_decorations=False, source_quality=None,
                    native_prop_placements=()):
    summary=Path(source)/'import-summary.json'
    quality=source_quality or (str(json.loads(summary.read_bytes()).get('sourceVariant','X360')).upper() if summary.is_file() else 'X360')
    output=Path(output);output.mkdir(parents=True,exist_ok=True);pages=output/'pages';pages.mkdir(exist_ok=True);cpu=output/'current.cpu'
    if cpu.exists():raise ValueError('Unexamined interrupted CPU trial')
    scenes=Container(source,'scenes');models=GPU(gpu_root);definitions,refs=groups(Path(master_path).read_bytes());summer={(s,i) for s,i,f in definitions[2]['overrides']}-{(s,i) for g in (1,7) for s,i,f in definitions[g]['overrides']}
    spawners = {}
    if public_decorations:
        from source_systems import source_spawners
        systems = Container(source, 'systems')
        try:
            spawners = source_spawners(systems)
        finally:
            systems.close()
    states=material_bank(source,output/'materials.bank');sun=Sun(json.loads((Path(environment)/'weather-zones.json').read_text())['zones']);lights=Lights(environment);stats=Counter();missing=[];fallback_lods=[];seen=set();start=time.monotonic();accepted=[]
    index_path=output/'pages.jsonl'
    # Resume only complete pages whose recorded source and stored SHA agree.
    if index_path.exists():
        accepted=[json.loads(s) for s in index_path.open(encoding='utf-8')]
        for r in accepted:
            p=pages/(str(r['sourceSection'])+'.page')
            if hashlib.sha256(p.read_bytes()).hexdigest()!=r['sha256']:raise ValueError('Retained terrain page changed')
    done={r['sourceSection'] for r in accepted}
    with index_path.open('a',encoding='utf-8') as index:
        for sidstr,row in sorted(scenes.entries.items(),key=lambda x:int(x[0])):
            sid=int(sidstr);s=scene(scenes.get(sidstr));pieces=[]
            records=iter(spawners.get(sid, ()))
            for ordinal in range(s['instanceCount']):
                raw=s['instances'][ordinal*96:(ordinal+1)*96];flags=struct.unpack_from('<I',raw,12)[0]
                is_prop=bool(flags&0x40000000)
                prop=next(records,None) if public_decorations and is_prop else None
                if public_decorations and is_prop and prop is None:
                    raise ValueError(f'Original prop spawner missing at {sid}:{ordinal}')
                if flags&0x3a and (sid,ordinal) not in summer:stats['inactivePlacements']+=1;continue
                if is_prop:
                    stats['nativePropPlacements']+=1
                    if (sid, ordinal) in native_prop_placements:
                        stats['runtimePropPlacements']+=1
                        continue
                    facility_part = prop is not None and (prop['parameter'], prop['asset']) in PUBLIC_VISUAL_FACILITY_PARTS
                    if not facility_part and (prop is None or prop['parameter'] not in PUBLIC_DECORATION_PARAMETERS or not decorative_bounds(raw)):
                        continue
                    # Same horizontal source join check as the native prop compiler.
                    bounds=(np.frombuffer(raw[:12],'<f4')+np.frombuffer(raw[16:28],'<f4'))*.5
                    dx=prop['position'][0]+float(bounds[1]); dz=prop['position'][2]-float(bounds[0])
                    if dx*dx+dz*dz>100:raise ValueError(f'Original prop spawner/scenery mismatch at {sid}:{ordinal}')
                ii=struct.unpack_from('<h',raw,84)[0];info=s['infos'][ii*72:(ii+1)*72];lods=struct.unpack_from('<4I',info,24);selected=next((h for h in lods if f'{h:08x}' in models.rows),None);name=bytes(info[:24]).split(b'\0')[0].decode('ascii')
                if selected is None:missing.append(dict(section=sid,instance=ordinal,name=name,lods=[f'{h:08x}' for h in lods]));continue
                if selected!=lods[0]:fallback_lods.append(dict(section=sid,instance=ordinal,name=name,sourceLOD=lods.index(selected),model=f'{selected:08x}'))
                # Exact duplicate placements belong to the first source section.
                # Keep all original identities in source scenes, never round.
                identity=struct.pack('<I',selected)+bytes(raw[32:80])
                if identity in seen:stats['exactDuplicatePlacements']+=1;continue
                seen.add(identity);model_name,clusters=models.get(f'{selected:08x}')
                if model_name.startswith(('RFL_','SHD_','SHADOW')):stats['specialPassPlacements']+=1;continue
                stats['visualPropPlacements' if is_prop else 'staticPlacements']+=1
                if is_prop and facility_part:stats['visualFacilityParts']+=1
                if sid in done:continue
                matrix=np.frombuffer(raw[44:80],'<f4').reshape(3,3).astype('f8');position=np.frombuffer(raw[32:44],'<f4').astype('f8')+[-6016,546,0];det=np.linalg.det(matrix)
                if not np.isfinite(matrix).all() or abs(det)<1e-12:raise ValueError('Original scenery singular/nonfinite basis')
                normal_matrix=np.linalg.inv(matrix).T
                for mat,v,ix in clusters:
                    world=v.copy();positions=np.asarray(v[:,:3].view('<f4')@matrix+position,'<f4');normal=v[:,3:6].view('<f4')@normal_matrix;length=np.linalg.norm(normal,axis=1);normal[length>1e-12]/=length[length>1e-12,None];normal[length<=1e-12]=[0,0,1]
                    world[:,:3]=positions.view('<u4');world[:,3:6]=np.asarray(normal,'<f4').view('<u4');world[:,9]=sun.exposure(positions).view('<u4');indices=ix.astype('<u4')
                    if det<0:indices=indices.reshape(-1,3)[:,[0,2,1]].ravel()
                    pieces.append((mat[4],world,indices))
            if public_decorations and next(records,None) is not None:
                raise ValueError(f'Unjoined original prop spawner section: {sid}')
            if sid in done:continue
            meta=write_cpu(cpu,sid,pieces,states,lights)
            if meta is None:stats['emptySections']+=1;continue
            if cpu.stat().st_size>64*1024**2:raise ValueError(f'Section requires bounded subdivision: {sid}')
            dest=pages/(sidstr+'.page');p=subprocess.run([str(repacker),str(cpu),str(output/'materials.bank'),str(dest),'--lz4'],capture_output=True,text=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
            if p.returncode:raise RuntimeError(p.stderr+p.stdout)
            result=json.loads(p.stdout);meta.update(result);meta['sha256']=hashlib.sha256(dest.read_bytes()).hexdigest();meta['sourceSceneSha256']=row['sha256'];accepted.append(meta);index.write(json.dumps(meta,separators=(',',':'))+'\n');index.flush();cpu.unlink()
            if len(accepted)%100==0:print(json.dumps(dict(phase='original-terrain-pages',quality=quality,pages=len(accepted),seconds=time.monotonic()-start)),flush=True)
    report=dict(source=f'Original {quality} model/scenery',pages=len(accepted),statistics=dict(stats),vertices=sum(r['sourceVertexCount'] for r in accepted),indices=sum(r['sourceIndexCount'] for r in accepted),clusters=sum(r['clusters'] for r in accepted),storedBytes=sum((pages/(str(r['sourceSection'])+'.page')).stat().st_size for r in accepted),missingPlacements=missing,sourceLodFallbacks=fallback_lods,oldConvertedTerrainRead=False,runtimeClosureComplete=False,seconds=time.monotonic()-start)
    (output/'terrain-summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k not in ('missingPlacements','sourceLodFallbacks')}),flush=True)
if __name__=='__main__':compile_terrain(*sys.argv[1:])
