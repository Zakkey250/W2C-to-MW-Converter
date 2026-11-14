"""Demand closure of original W2C destruction engines and child parameters."""
import hashlib,json,struct,sys
from pathlib import Path
from audit_breaker_sequences import sequences,commands
from carbon_breaker_dependencies import collect_breaker_dependencies
from build_carbon_breaker_prototype import adapt_sequence
from mw_attribute_hash import attribute_hash
def demand_closure(rows,engines,roots,native,effective):
    by_hash={int(r['hash'],16):r['name'] for r in rows if r['class']=='smackable'};pending=list(roots);selected=set();used=set();preserved=set();edges=[];missing=[]
    while pending:
        name=pending.pop()
        if name in selected:continue
        selected.add(name);engine=effective(name).get('EventSequencer','')
        if not engine:continue
        if engine in native and name not in roots:preserved.add(engine);continue
        if engine not in engines:missing.append(dict(owner=name,engine=engine,kind='original engine absent'));continue
        used.add(engine)
        for kind,size,event in commands(engines[engine]):
            if kind!=0x2554c48a:continue
            if size!=48:raise ValueError('Original fragment record ABI')
            child=struct.unpack_from('<I',event,12)[0]
            if not child:continue
            if child not in by_hash:missing.append(dict(owner=name,engine=engine,kind='original child collection absent',hash=f'{child:08x}'));continue
            target=by_hash[child];edge=dict(owner=name,engine=engine,child=target,sourceHash=f'{child:08x}')
            if edge not in edges:edges.append(edge)
            pending.append(target)
    return dict(roots=sorted(roots),smackables=sorted(selected),engines=sorted(used),preservedNativeEngines=sorted(preserved),fragmentReferences=edges,missingOriginalReferences=missing)
def plan_sequence_assets(data, native_track, rows, target_rows, root_names, *, namespace='w2v2_'):
    source=sequences(data);native=sequences(native_track);byname={r['name']:r for r in rows if r['class']=='smackable'}
    def effective(name,seen=()):
        if name in seen:raise ValueError('Original smackable inheritance cycle')
        row=byname[name];v=effective(row['parent'],seen+(name,)) if row['parent'] else {};v.update(row['fields']);return v
    camera_kinds={0xf07a31ac,0x66666dc8}
    shared_native=[dict(parameter=n,engine=effective(n).get('EventSequencer','')) for n in root_names if effective(n).get('EventSequencer','') in native and not ({k for k,z,e in commands(source[effective(n)['EventSequencer']])}&camera_kinds)]
    roots_to_port=sorted(set(root_names)-{r['parameter'] for r in shared_native});closure=demand_closure(rows,source,roots_to_port,native,effective);names=set(closure['engines']);area_dependencies=set()
    while True:
        targets=set()
        for name in names:
            for kind,size,event in commands(source[name]):
                if kind!=0x762442f3:continue
                system,stimulus=struct.unpack_from('<II',event,4)
                for candidate,r in source.items():
                    for tag,entry in r['entries'].items():
                        if tag>>16!=0x6553 or entry['key']!=system:continue
                        n=struct.unpack_from('<I',entry['data'],8)[0]
                        for i in range(n):
                            state=struct.unpack_from('<I',entry['data'],12+4*n+4*i)[0];p=r['entries'][state]['data'];count=struct.unpack_from('<I',p)[0]
                            if stimulus in struct.unpack_from('<'+'I'*count,p,16):targets.add(candidate)
        missing=targets-names-set(native)
        if not missing:break
        names.update(missing);area_dependencies.update(missing)
    names=sorted({alias for name in names for alias in source[name]['names']})
    renames={name:('w2v2_seq_'+name,attribute_hash(name),attribute_hash('w2v2_seq_'+name)) for name in names}
    converted={r['name']:int(r['hash'],16) for r in target_rows if r['class']=='smackable'};attributes={int(r['hash'],16):converted.get(namespace+r['name'],int(r['hash'],16)) for r in rows if r['class']=='smackable'};missing_attributes=[n for n in closure['smackables'] if namespace+n not in converted];results=[];scenes=set();kinds={}
    for name in names:
        record=source[name];scenes.update(e['data'].split(b'\0')[0].decode('ascii') for tag,e in record['entries'].items() if tag>>16==0x6353)
        for kind,size,event in commands(record):kinds[f'{kind:08x}']=kinds.get(f'{kind:08x}',0)+1
        try:
            result,changes=adapt_sequence(record,attributes,renames[name],True,None,{n:renames[n] for n in record['names'] if n in renames});results.append(dict(source=name,convertible=True,bytes=len(result),changes=changes))
        except ValueError as error:results.append(dict(source=name,convertible=False,reason=str(error)))
    return dict(originalSourceGlobalSha256=hashlib.sha256(data).hexdigest(),closure=closure,sharedNativeGenericEngines=shared_native,sharedNativeReason='Retain the existing MW generic engine; source-specific engines and cameras are privately namespaced. Original Carbon generic bytes remain preserved in source pack.',areaDependencyEngines=sorted(area_dependencies),engineAliases=len(names),engineRenames={n:dict(target=r[0],sourceHash=f'{r[1]:08x}',targetHash=f'{r[2]:08x}') for n,r in renames.items()},missingConvertedChildParameters=missing_attributes,cameraScenes=sorted(scenes),eventCommandKinds=kinds,preflight=results,gameConverted=False)


def main(source_global,source_audit,target_audit,native_track,roots,executable,output):
    from native_string_hash import NativeStringHash

    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    if (output/'sequence-plan.json').exists():raise ValueError('Immutable original sequence plan exists')
    root_names=json.loads(Path(roots).read_bytes())['sourceRoots']
    result=plan_sequence_assets(Path(source_global).read_bytes(),Path(native_track).read_bytes(),
                                json.loads(Path(source_audit).read_bytes())['rows'],
                                json.loads(Path(target_audit).read_bytes())['rows'],root_names)
    oracle=NativeStringHash(executable)
    for name,row in result['engineRenames'].items():
        if oracle(name)!=int(row['sourceHash'],16) or oracle(row['target'])!=int(row['targetHash'],16):
            raise ValueError('Independent engine hash differs from the native oracle')
    result['originalSourceGlobal']=str(source_global)
    (output/'sequence-plan.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(dict(sourceRoots=len(root_names),sourceChildClosure=len(result['closure']['smackables']),
                          engineAliases=result['engineAliases'],missingConvertedChildParameters=result['missingConvertedChildParameters'],
                          cameraScenes=result['cameraScenes'],blockers=[r for r in result['preflight'] if not r['convertible']])))
if __name__=='__main__':main(*sys.argv[1:])
