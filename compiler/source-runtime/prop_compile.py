"""Typed original smackable, collision and hierarchy adaptation."""
from collections import Counter,defaultdict
import hashlib,json,struct,sys
from pathlib import Path
import numpy as np
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'source-world'))
from source_ir import Container,chunks,scene
from source_systems import target_spawners
ASSET_XOR=0x27c5f6a1
MODEL_XOR=0x6af72c91

def collision_collections(bank,allow_native_variants=False):
    result={}
    for key,row in bank.entries.items():
        origin=row['metadata']['origin']
        if origin['chunk']!='8003b900':continue
        data=bank.get(key);base=origin['offset']+8
        for kind,off,a,b in chunks(data):
            if kind==0:continue
            if kind!=0x3b901:raise ValueError('Unexpected original convex collection child')
            at=(base+a+15)//16*16-base
            asset,count,resolved,pad=struct.unpack_from('<4I',data,at)
            if not 0<count<4096 or resolved:raise ValueError('Original collection header')
            cloud=at+16+count*48
            if cloud+16>b:raise ValueError('Original compressed convex bounds')
            clouds=struct.unpack_from('<I',data,cloud)[0];end=cloud+16
            if clouds>4096:raise ValueError('Original cloud count')
            for i in range(clouds):
                n=struct.unpack_from('<I',data,end)[0]
                if n>65536:raise ValueError('Original convex cloud points')
                end+=16+n*16
                if end>b:raise ValueError('Original convex cloud range')
            if end!=b:raise ValueError('Original collection exact closure')
            payload=bytearray(data[at:b])
            for i in range(count):struct.pack_into('<I',payload,16+i*48+44,0)
            if asset in result and result[asset]['payload']!=bytes(payload):
                if not allow_native_variants:raise ValueError(f'Ambiguous original collision asset {asset:08x}: {result[asset]["source"]} / {key}')
                previous=result[asset];before=previous['payload'];root1=bytearray(before[16:64]);root2=bytearray(payload[16:64])
                # MW stores detailed compound and root-only definitions under
                # one asset. Hierarchy child links differ, not the root volume.
                # Select the retained original with the most authored bounds.
                for at in (22,30,31):root1[at]=root2[at]=0
                compatible=root1==root2 and before[16+previous['bounds']*48:]==payload[16+count*48:]
                variants=previous.get('variants',[])+[dict(source=key,sha256=hashlib.sha256(payload).hexdigest(),bounds=count,rootAndCloudsEquivalent=compatible)]
                valid=previous.get('variantCompatible',True) and compatible
                if count>previous['bounds']:result[asset]=dict(payload=bytes(payload),bounds=count,clouds=clouds,source=key,variants=variants,variantCompatible=valid,variantSelection='most original compound bounds; identical root volume/cloud data')
                else:previous.update(variants=variants,variantCompatible=valid,variantSelection='most original compound bounds; identical root volume/cloud data')
                continue
            result[asset]=dict(payload=bytes(payload),bounds=count,clouds=clouds,source=key)
    return result

def hierarchies(bank):
    result={}
    for key,row in bank.entries.items():
        if row['metadata']['origin']['chunk']!='8003410b':continue
        for kind,off,a,b in chunks(bank.get(key)):
            if kind==0:continue
            if kind!=0x3410c:raise ValueError('Original hierarchy child kind')
            p=bytes(bank.get(key)[a:b]);h,n=struct.unpack_from('<II',p)
            if not 0<n<=32 or len(p)!=8+n*16:raise ValueError('Original hierarchy dimensions')
            for i in range(n):
                tag,solid,pointer,flags=struct.unpack_from('<4I',p,8+16*i);parent,other,children,first=struct.unpack_from('<4B',p,20+16*i)
                if pointer or first+children>n or not(parent<n or parent==255):raise ValueError('Original hierarchy pointer/topology')
            if h in result and p!=result[h]:raise ValueError('Ambiguous original hierarchy')
            result[h]=p
    return result

def compile_props(source,target,work,output):
    work=Path(work)
    read=lambda name:json.loads((work/name).read_text(encoding='utf-8'))['rows']
    with (work/'systems-360/prop-source-join.jsonl').open(encoding='utf-8') as stream:
        records=list(map(json.loads,stream))
    return compile_prop_assets(
        source,target,work/'native-pristine',read('attributes-360/source-adapted.json'),
        read('attributes-360/native-clean-audit.json'),read('attributes-360/compiled-main-audit.json'),
        records,output)


def compile_prop_assets(source,target,pristine_target,source_rows,native_rows,compiled_rows,records,output,*,namespace='w2v2_'):
    """Compile native payloads from explicitly provided, audited inputs."""
    source=Path(source);target=Path(target);output=Path(output);output.mkdir(parents=True,exist_ok=True)
    if (output/'catalog.bin').exists():raise ValueError('Immutable native prop trial exists')
    sb=Container(source,'systems');tb=Container(target,'systems');sc=Container(source,'scenes');models=Container(source,'models');native_models=Container(target,'models')
    coll=collision_collections(sb);native_coll=collision_collections(tb,True);trees=hierarchies(sb);native_trees=hierarchies(tb);_,native_assets,native_params=target_spawners(tb)
    pristine=Container(pristine_target,'systems');pristine_coll=collision_collections(pristine,True);native_coll.update(pristine_coll);native_trees.update(hierarchies(pristine))
    src_rows={(r['class'],r['name']):r for r in source_rows};native_rows={(r['class'],r['name']):r for r in native_rows};by_hash={r['hash'].lower():r for r in source_rows if r['class']=='smackable'};dest_rows={(r['class'],r['name']):r for r in compiled_rows}
    active=[r for r in records if r['active']];assets={};unresolved=[];rows=[];catalog=[];cache={}
    fallback_prefixes=[(('XO_CARBODYB_','XOS_CARBODYB_'),0x15bbceb9,0xae71255c),(('XOS_MINIVAN_',),0xdcfe6004,0x11839388),(('XOS_NEWSBOXA_',),0x23cb1786,0x12e60973)]
    for r in active:
        sid=r['section'];ordinal=r['instance'];asset=int(r['asset'],16);solid=int(r['solid'],16);name=r['name'];source_parameter=by_hash[r['parameter']];target_name=namespace+source_parameter['name'];converted_parameter=dest_rows['smackable',target_name]
        if sid not in cache:cache[sid]=scene(sc.get(str(sid)))
        s=cache[sid];ii=struct.unpack_from('<h',s['instances'],ordinal*96+84)[0];info=s['infos'][ii*72:(ii+1)*72];tree_key=struct.unpack_from('<I',info,64)[0]
        model_source='w2c';collision_source='w2c';fallback=None
        if r['solid'] not in models.entries:
            if r['solid'] in native_models.entries:model_source='native-exact-hash';collision_source='native' if asset in native_coll else 'w2c'
            else:
                fallback=next(((a,m) for prefixes,a,m in fallback_prefixes if name.startswith(prefixes)),None)
                if fallback:asset,solid=fallback;model_source='native-explicit-missing-original';collision_source='native'
                else:unresolved.append(dict(**r,reason='Original display model absent; no verified exact native fallback'));continue
        collision=coll.get(asset) if collision_source=='w2c' else native_coll.get(asset)
        if collision is None:
            collision=native_coll.get(asset);collision_source='native'
        if collision is None:
            exact_native=native_rows.get(('smackable',source_parameter['name']))
            exact_template=native_params.get(int(exact_native['hash'],16)) if exact_native else None
            adapted_asset=exact_template['asset'] if exact_template and exact_template['asset'] in native_coll else 0xd11bf68f if name.startswith('XOS_STREETLIGHTD_') else None
            if adapted_asset in native_coll:asset=adapted_asset;collision=native_coll[asset];collision_source='native-explicit-missing-original'
        if collision is None:unresolved.append(dict(**r,reason='Original and native convex collection absent'));continue
        if collision.get('variants') and not collision.get('variantCompatible'):unresolved.append(dict(**r,reason='Native convex fallback has ambiguous original definitions',variants=collision['variants']));continue
        tree=(trees.get(tree_key) or trees.get(solid)) if model_source=='w2c' else native_trees.get(solid)
        synthesized_root=tree is None
        if tree is None:tree=struct.pack('<6I',solid,1,0x9756df79,solid,0,0xff00)
        # A single BASE node retains an intact original; it fabricates no
        # breakup pieces. Actual source destruction trees remain unchanged.
        wanted={struct.unpack_from('<I',tree,12+16*i)[0] for i in range(struct.unpack_from('<I',tree,4)[0])};wanted.add(solid)
        missing=[f'{h:08x}' for h in wanted if f'{h:08x}' not in models.entries and f'{h:08x}' not in native_models.entries]
        if missing:unresolved.append(dict(**r,reason='Original destruction-tree model closure missing',missing=missing));continue
        template=native_params.get(int(source_parameter['hash'],16));ancestor=source_parameter['name'];visited=set()
        while template is None and ancestor:
            if ancestor in visited:raise ValueError('Prop schema ancestry cycle')
            visited.add(ancestor);nr=native_rows.get(('smackable',ancestor))
            if nr:template=native_params.get(int(nr['hash'],16))
            ancestor=src_rows['smackable',ancestor]['parent'] if ('smackable',ancestor) in src_rows else None
        if template is None:template=next(iter(native_params.values()))
        semantic=b'W2V2.NativeProp.Asset.r1\0'+struct.pack('<3I',int(r['asset'],16),solid,int(r['parameter'],16))+tree
        private_asset=int.from_bytes(hashlib.sha256(semantic).digest()[:4],'little')
        if not private_asset:raise ValueError('Reserved native asset identity')
        record=bytearray(template['raw']);original_record=bytes.fromhex(r['sourceRecordHex']);record[:16]=original_record[:16];struct.pack_into('<3f',record,16,*r['mwPosition']);secondary=int(r['secondary'],16)
        secondary_alias=private_asset if secondary==int(r['asset'],16) else secondary^ASSET_XOR if secondary else 0
        if secondary and secondary!=int(r['asset'],16) and secondary not in coll and secondary not in native_coll:unresolved.append(dict(**r,reason='Original secondary convex collection absent'));continue
        struct.pack_into('<3I',record,32,private_asset,secondary_alias,int(converted_parameter['hash'],16))
        normalized_tree=bytearray(tree);struct.pack_into('<I',normalized_tree,0,struct.unpack_from('<I',tree)[0]^MODEL_XOR)
        for i in range(struct.unpack_from('<I',tree,4)[0]):struct.pack_into('<I',normalized_tree,12+i*16,struct.unpack_from('<I',tree,12+i*16)[0]^MODEL_XOR)
        desc=dict(sourceAsset=r['asset'],asset=f'{private_asset:08x}',displaySolid=f'{solid:08x}',displayModelAlias=f'{solid^MODEL_XOR:08x}',modelSource=model_source,collisionSource=collision_source,collisionAsset=f'{asset:08x}',sourceTreeKey=f'{tree_key:08x}',singleIntactRoot=synthesized_root,treeHex=normalized_tree.hex(),models=[f'{h:08x}' for h in sorted(wanted)],parameter=target_name,parameterHash=converted_parameter['hash'].lower(),collisionPayloadSha256=hashlib.sha256(collision['payload']).hexdigest(),nativeTemplateParameter=f'{template["parameter"]:08x}')
        if private_asset in assets:
            functional=lambda value:{k:v for k,v in value.items() if k!='sourceTreeKey'}
            if functional(assets[private_asset])!=functional(desc):raise ValueError(f'Native variant identity collision: {private_asset:08x}')
        assets[private_asset]=desc;catalog.append(bytes(record));rows.append(dict(id=len(rows),sourcePlacementId=r['sourcePlacementId'],section=sid,instance=ordinal,name=name,sourceAsset=r['asset'],asset=f'{private_asset:08x}',parameter=target_name,recordHex=record.hex(),nativeFallback=fallback is not None))
    (output/'catalog.bin').write_bytes(struct.pack('<II',0x31504d57,len(catalog))+b''.join(catalog))
    body=bytearray();hs=bytearray(struct.pack('<II',0x31485057,len(assets)));params=bytearray(struct.pack('<II',0x32505057,len(assets)))
    secondary_collections={}
    for r in rows:
        secondary=struct.unpack_from('<I',bytes.fromhex(r['recordHex']),36)[0]
        if secondary and secondary!=int(r['asset'],16):
            original_secondary=secondary^ASSET_XOR;secondary_collections[secondary]=coll.get(original_secondary) or native_coll[original_secondary]
    for alias,collision in sorted(secondary_collections.items()):
        if collision.get('variants') and not collision.get('variantCompatible'):raise ValueError('Secondary convex definition is ambiguous')
        payload=bytearray(collision['payload']);struct.pack_into('<I',payload,0,alias);pad=(-(8+len(body)+8))%16;body.extend(struct.pack('<II',0x3b901,pad+len(payload))+b'\x11'*pad+payload)
    for alias,a in sorted(assets.items()):
        if alias in secondary_collections:raise ValueError('Variant/secondary identity collision')
        collision=(coll if a['collisionSource']=='w2c' else native_coll)[int(a['collisionAsset'],16)];payload=bytearray(collision['payload']);struct.pack_into('<I',payload,0,alias);pad=(-(8+len(body)+8))%16;body.extend(struct.pack('<II',0x3b901,pad+len(payload))+b'\x11'*pad+payload)
        tree=bytes.fromhex(a['treeHex']);hs.extend(struct.pack('<II',alias,len(tree)));hs.extend(tree);params.extend(struct.pack('<I',alias)+a['parameter'].encode().ljust(64,b'\0'))
    (output/'collision.bun').write_bytes(struct.pack('<II',0x8003b900,len(body))+body);(output/'hierarchies.bin').write_bytes(hs);(output/'parameters.bin').write_bytes(params)
    for name,value in [('assets.json',{f'{h:08x}':a for h,a in assets.items()}),('records.jsonl',rows),('unresolved.json',unresolved)]:
        (output/name).write_text(''.join(json.dumps(r,separators=(',',':'))+'\n' for r in value) if name.endswith('.jsonl') else json.dumps(value,indent=2),encoding='utf-8')
    report=dict(sourceActivePlacements=len(active),convertedPlacements=len(rows),assets=len(assets),unresolvedPlacements=len(unresolved),originalConvexCollections=len(coll),nativeConvexCollections=len(native_coll),originalTrees=len(trees),nativeTrees=len(native_trees),modelSources=dict(Counter(a['modelSource'] for a in assets.values())),singleIntactRoots=sum(a['singleIntactRoot'] for a in assets.values()),allQuaternionComponentsPreserved=True,namespace=namespace,runtimeReady=not unresolved)
    (output/'prop-summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report));return report
if __name__=='__main__':compile_props(*sys.argv[1:])
