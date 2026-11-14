"""Independent typed source systems, identity joins, and native target records."""
from collections import Counter,defaultdict
import hashlib,json,struct,sys
from pathlib import Path
import numpy as np
COMPILER=Path(__file__).resolve().parents[1];sys.path.insert(0,str(COMPILER/'source-world'))
from source_ir import Container,scene,chunks
from master import groups,graph,sections,unique

def source_spawners(bank):
    output={}
    for key,row in bank.entries.items():
        if row['metadata']['origin']['chunk']!='00034027':continue
        p=bank.get(key)
        if len(p)<24 or bytes(p[:16])!=b'\x11'*8+bytes(8):raise ValueError('Source smackable header')
        sid,n,reserved=struct.unpack_from('<HHI',p,16)
        if len(p)!=24+n*64 or sid in output or reserved:raise ValueError('Source smackable exact section dimensions')
        records=[]
        for i in range(n):
            r=bytes(p[24+i*64:24+(i+1)*64]);pos=struct.unpack_from('<3f',r,16);asset,secondary,param=struct.unpack_from('<3I',r,28)
            if not np.isfinite(pos).all():raise ValueError('Source smackable position')
            records.append(dict(ordinal=i,raw=r,position=pos,asset=asset,secondary=secondary,parameter=param,sceneryGuid=struct.unpack_from('<I',r,44)[0],typeFlags=struct.unpack_from('<I',r,48)[0]))
        output[sid]=records
    return output

def target_spawners(bank):
    output={};by_asset=defaultdict(dict);by_parameter={}
    for key,row in bank.entries.items():
        if row['metadata']['origin']['chunk']!='00034027':continue
        p=bank.get(key)
        if len(p)<24 or bytes(p[:16])!=b'\x11'*8+struct.pack('<II',11,11):raise ValueError('MW base smackable header')
        sid,first,n=struct.unpack_from('<HHI',p,16)
        if len(p)!=24+n*64 or sid in output:raise ValueError('MW base smackable exact dimensions')
        records=[]
        for i in range(n):
            r=bytes(p[24+i*64:24+(i+1)*64]);asset,secondary,param=struct.unpack_from('<3I',r,32);pos=struct.unpack_from('<3f',r,16)
            if not np.isfinite(pos).all():raise ValueError('MW base position')
            item=dict(ordinal=i,raw=r,position=pos,asset=asset,secondary=secondary,parameter=param);records.append(item);by_asset[asset].setdefault(param,item);by_parameter.setdefault(param,item)
        output[sid]=records
    return output,by_asset,by_parameter

def target_scene(data):
    if bytes(data[:8])!=b'W2TSCN1\0' or struct.unpack_from('<II',data,8)!=(1,32):raise ValueError('Target scene contract')
    sid,ni,nn,reserved=struct.unpack_from('<4I',data,16)
    if reserved or ni>100000 or nn>1000000:raise ValueError('Target scene dimensions')
    infos=[];at=32
    for i in range(ni):
        key,n=struct.unpack_from('<II',data,at);at+=8
        if n>4096 or at+n>len(data):raise ValueError('Target name bounds')
        name=bytes(data[at:at+n]).decode('utf-8');at=(at+n+3)&~3;infos.append(dict(hash=key,name=name))
    if at+nn*96!=len(data):raise ValueError('Target placement stride')
    return dict(section=sid,infos=infos,count=nn,instances=data[at:])

def source_prop_rows(spawners, scenes, master):
    definitions, _ = groups(master)
    memberships = defaultdict(list)
    for group,d in definitions.items():
        for sid,ordinal,flags in d['overrides']:memberships[sid,ordinal].append(group)
    for sid,records in sorted(spawners.items()):
        s=scene(scenes.get(str(sid)));dynamic=[]
        for i in range(s['instanceCount']):
            p=s['instances'][96*i:96*(i+1)];flags=struct.unpack_from('<I',p,12)[0]
            if flags&0x40000000:dynamic.append((i,p,flags))
        if len(dynamic)!=len(records):raise ValueError(f'Source spawner/scenery join count {sid}: {len(records)}/{len(dynamic)}')
        for r,(ordinal,p,flags) in zip(records,dynamic):
            ii=struct.unpack_from('<h',p,84)[0];info=s['infos'][ii*72:(ii+1)*72];solid=struct.unpack_from('<I',info,24)[0];name=bytes(info[:24]).split(b'\0')[0].decode('ascii');bounds=np.frombuffer(p[:12],'<f4')*.5+np.frombuffer(p[16:28],'<f4')*.5;raw_center=np.array([-bounds[1],bounds[2],bounds[0]])
            delta=np.array(r['position'])-raw_center;distance=float(np.linalg.norm(delta[[0,2]]))
            if distance>10:raise ValueError(f'Source placement identity join differs {sid}:{ordinal}: {distance}')
            member_groups=set(memberships[sid,ordinal])
            active=(not(flags&0x3a)) or (2 in member_groups and not member_groups.intersection((1,7)))
            yield dict(section=sid, spawner=r['ordinal'], instance=ordinal, name=name, solid=solid,
                       record=r, flags=flags, groups=memberships[sid,ordinal], active=active,
                       mwPosition=(np.array(r['position'])+[-546,0,-6016]).tolist(), distance=distance)


def pair_props(source_root,target_root,master_path,output):
    source=Path(source_root);target=Path(target_root);output=Path(output);output.mkdir(parents=True,exist_ok=True)
    sb=Container(source,'systems');sc=Container(source,'scenes');mb=Container(source,'models');tb=Container(target,'systems');tm=Container(target,'models')
    spawners=source_spawners(sb);native,by_asset,by_param=target_spawners(tb)
    rows=[];max_distance=0;unresolved=[];replacement_candidates=[];source_solid_hashes={int(k,16) for k in mb.entries if '/' not in k};target_solid_hashes={int(k,16) for k in tm.entries if '/' not in k}
    for row in source_prop_rows(spawners, sc, Path(master_path).read_bytes()):
        sid, ordinal, r = row['section'], row['instance'], row['record']
        solid, active = row['solid'], row['active']
        max_distance = max(max_distance, row['distance'])
        item=dict(section=sid,spawner=r['ordinal'],instance=ordinal,sourcePlacementId=f'{sid}:{ordinal}',sourceSpawnerId=f'{sid}:{r["ordinal"]}',name=row['name'],solid=f'{solid:08x}',asset=f'{r["asset"]:08x}',secondary=f'{r["secondary"]:08x}',parameter=f'{r["parameter"]:08x}',sourceFlags=f'{row["flags"]:08x}',groups=row['groups'],active=active,sourcePosition=list(r['position']),mwPosition=row['mwPosition'],sourceRecordHex=r['raw'].hex(),sourceMeshAvailable=solid in source_solid_hashes,targetMeshSameHashAvailable=solid in target_solid_hashes,nativeAssetAvailable=r['asset'] in by_asset,nativeParameterAvailable=r['parameter'] in by_param,joinHorizontalDistance=row['distance'])
        rows.append(item)
        if active and solid not in source_solid_hashes:replacement_candidates.append(item)
        if active and r['asset'] not in by_asset and r['parameter'] not in by_param:unresolved.append(item)
    report=dict(sourceSpawnerSections=len(spawners),sourceSpawners=len(rows),active=sum(r['active'] for r in rows),nativeSections=len(native),nativeAssets=len(by_asset),nativeParameters=len(by_param),maximumJoinHorizontalDistance=max_distance,activeMissingSourceMesh=len(replacement_candidates),activeNativeTemplateClosurePending=len(unresolved))
    (output/'prop-source-join.jsonl').write_text(''.join(json.dumps(r,separators=(',',':'))+'\n' for r in rows),encoding='utf-8');(output/'prop-source-join-summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    (output/'prop-native-templates.json').write_text(json.dumps(dict(assets={f'{a:08x}':{f'{p:08x}':r['raw'].hex() for p,r in v.items()} for a,v in by_asset.items()},parameters={f'{p:08x}':r['raw'].hex() for p,r in by_param.items()}),separators=(',',':')),encoding='utf-8')
    return report

if __name__=='__main__':print(json.dumps(pair_props(*sys.argv[1:])))
