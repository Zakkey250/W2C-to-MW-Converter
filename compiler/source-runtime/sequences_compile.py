"""Privately namespace original W2C engines; never read converted engines."""
import copy, hashlib, json, struct, sys
from pathlib import Path
from audit_breaker_sequences import sequences, commands
from build_carbon_breaker_prototype import adapt_sequence

def sha(data): return hashlib.sha256(data).hexdigest()

EXPLOSION = 0x31BBBAE2
# Carbon-only pursuit installations and the command that represents the
# main impact. Supporting sparks and fragment impulses retain Carbon values.
PALMONT_BLASTS = {
    'Petro_OilPipe_Start_Seq': (0x656C000C, 1),
    'Petro_Transformer_Seq': (None, None),
    'Tireshoproof_seq': (None, None),
    'boutiquesign_seq': (0x656C0004, None),
    'buildingfacade_seq': (0x656C0000, 0),
    'chickensignpost_seq': (0x656C0000, 0),
    'corporateglobe_seq': (None, None),
    'droppedfromcrane_seq': (0x656C0003, 1),
    'gaspumpexplosion_seq': (0x656C0003, 1),
    'gettymonumentstrut_seq': (None, None),
    'gettystation_seq': (0x656C0005, None),
    'icecreamroof_seq': (None, None),
    'ironpole_seq': (None, None),
    'luckystrike_seq': (None, None),
    'montezuma_seq': (None, None),
    'motel_seq': (None, None),
    'outletsign_seq': (None, None),
    'parkedsemitruck_seq': (None, None),
    'thickscaffoldPetro_seq': (None, None),
    'thickscaffoldsupport_seq': (None, None),
}


def native_blast(record):
    rows = [data for kind, size, data in commands(record) if kind == EXPLOSION and size == 48]
    if len(rows) != 1:
        raise ValueError('Expected one MW pursuit blast in ' + record['names'][0])
    return rows[0]


def normalize_breaker_blast(record, native):
    name = next((name for name in record['names'] if name in PALMONT_BLASTS), None)
    if name is None:
        return []
    command_tag, command_index = PALMONT_BLASTS[name]
    candidates = []
    for tag, entry in record['entries'].items():
        if tag >> 16 != 0x656C or (command_tag is not None and tag != command_tag):
            continue
        data = entry['data']
        for i in range(struct.unpack_from('<I', data)[0]):
            if command_index is not None and command_index != i:
                continue
            at = 16 + i * 16
            kind, size, relative, _ = struct.unpack_from('<4I', data, at)
            if kind == EXPLOSION and size == 32:
                start = at + relative
                strength, radius, _ = struct.unpack_from('<3f', data, start + 8)
                candidates.append((strength, radius, tag, i, start))
    if not candidates:
        raise ValueError('Missing primary Carbon pursuit blast in ' + name)
    strength, radius, tag, i, start = max(candidates)
    entry = record['entries'][tag]
    data = bytearray(entry['data'])
    reference = 'gaspumpexplosion_seq' if name == 'gaspumpexplosion_seq' else 'explosion_seq'
    stock = native_blast(native[reference])
    new_strength, new_radius, new_inner = struct.unpack_from('<3f', stock, 8)
    if not (0 < radius <= new_radius <= 16 and strength > 0):
        raise ValueError('Unexpected pursuit blast extent in ' + name)
    # Preserve the Carbon impact marker. Replace its main blast profile with
    # the native MW profile so its simulator uses the same area and flags.
    struct.pack_into('<3f', data, start + 8, new_strength, new_radius, new_inner)
    data[start + 20:start + 25] = bytes(struct.unpack_from('<5I', stock, 20))
    entry['data'] = bytes(data)
    return [f'{name} {tag:08x}/{i}: MW {reference} blast radius {radius:g} -> {new_radius:g} m']


def quarantine_original_gap(record, gaps, cameras):
    result=copy.deepcopy(record)
    for tag, entry in result['entries'].items():
        if tag>>16!=0x656c: continue
        raw=entry['data']; rows=[]
        for i in range(struct.unpack_from('<I',raw)[0]):
            at=16+16*i; kind,size,relative,unused=struct.unpack_from('<4I',raw,at)
            data=raw[at+relative:at+relative+size]
            if kind==0x66666dc8:
                scene_key=struct.unpack_from('<I',data,8)[0]
                scene=result['entries'][scene_key]['data'].split(b'\0',1)[0].decode('ascii')
                if scene not in cameras:
                    continue
            if kind==0x2554c48a and struct.unpack_from('<I',data,12)[0]==0x0a8471ec:
                if set(record['names'])!={'metalgate_seq'} or size!=48:
                    raise ValueError('Known original gap appeared in a new owner/ABI')
                gaps.append(dict(engine='metalgate_seq',listTag=f'{tag:08x}',command=i,
                    sourceCommandSha256=sha(data),originalParameter='0a8471ec',
                    disposition='Quarantine this fragment spawn only; absent from original Carbon and pristine MW parameters; source command retained in original systems asset'))
            else: rows.append((kind,data,unused))
        if len(rows)==struct.unpack_from('<I',raw)[0]: continue
        out=bytearray(raw[:16]+bytes(16*len(rows)));struct.pack_into('<I',out,0,len(rows))
        for i,(kind,data,unused) in enumerate(rows):
            out.extend(bytes((-len(out))%16));at=16+16*i
            struct.pack_into('<4I',out,at,kind,len(data),len(out)-at,unused);out.extend(data)
        entry['data']=bytes(out)
    return result

def compile_sequence_assets(raw, native_track, source_rows, target_rows, plan, camera_plan, *, namespace='w2v2_'):
    """Compile exact, typed destruction engines from supplied local inputs."""
    if sha(raw)!=plan['originalSourceGlobalSha256']: raise ValueError('Original sequence input changed')
    if plan['missingConvertedChildParameters']: raise ValueError('Converted child closure incomplete')
    cameras={r['source']:r['target'] for r in camera_plan}
    source=sequences(raw); native=sequences(native_track)
    target={r['name']:int(r['hash'],16) for r in target_rows if r['class']=='smackable'}
    attrs={int(r['hash'],16):target.get(namespace+r['name'],int(r['hash'],16)) for r in source_rows if r['class']=='smackable'}
    renames={n:(r['target'],int(r['sourceHash'],16),int(r['targetHash'],16)) for n,r in plan['engineRenames'].items()}
    body=bytearray();seen=set();reports=[];gaps=[]
    for name,rename in renames.items():
        r=source[name]; identity=sha(r['raw'])
        if identity in seen: continue
        seen.add(identity)
        if any(renames[n][0] in native for n in r['names']): raise ValueError('Native engine collision')
        record=quarantine_original_gap(r,gaps,cameras)
        blast_changes=normalize_breaker_blast(record,native)
        data,changes=adapt_sequence(record,attrs,rename,True,cameras,{n:renames[n] for n in r['names']})
        body.extend(struct.pack('<II',0x3b811,len(data))+data)
        reports.append(dict(sourceNames=r['names'],targetNames=[renames[n][0] for n in r['names']],sourceSha256=identity,sha256=sha(data),changes=blast_changes+changes))
    if len(gaps)!=1: raise ValueError('Known original missing-fragment occurrence count changed')
    data=struct.pack('<II',0x8003b810,len(body))+body
    if set(sequences(data))!={r[0] for r in renames.values()}: raise ValueError('Sequence readback identity')
    def bh(s):
        h=0xffffffff
        for c in s.encode('ascii'): h=(h*33+c)&0xffffffff
        return h
    routes=dict(schema=1,engines=[dict(source=n,target=r[0],sourceHash=r[1],targetHash=r[2]) for n,r in renames.items()],
                cameraScenes=[dict(name=r['target'],hash=bh(r['target'])) for r in camera_plan],
                originalGaps=gaps,sharedNativeGenericEngines=plan['sharedNativeGenericEngines'])
    return data, routes, reports, gaps


def main(work,output):
    work=Path(work);output=Path(output)
    if output.exists() and any(output.iterdir()): raise ValueError('Immutable output exists')
    plan_path=work/'sequence-r2-360/sequence-plan.json'; plan=json.loads(plan_path.read_bytes())
    raw=Path(plan['originalSourceGlobal']).read_bytes()
    camera_plan=json.loads((work/'camera-plan-r2-360/scene-plan.json').read_bytes())
    source_rows=json.loads((work/'attributes-360/source-adapted.json').read_bytes())['rows']
    target_rows=json.loads((work/'attributes-closure-360/compiled-main-audit.json').read_bytes())['rows']
    data,routes,reports,gaps=compile_sequence_assets(
        raw,(work/'offline-original-references/mw-pristine/TRACKS/L2RA.BUN').read_bytes(),
        source_rows,target_rows,plan,camera_plan)
    output.mkdir(parents=True,exist_ok=True);(output/'sequences.bun').write_bytes(data)
    (output/'routes.json').write_text(json.dumps(routes,indent=2),encoding='utf-8')
    report=dict(originalSourceSha256=sha(raw),planSha256=sha(plan_path.read_bytes()),bytes=len(data),sha256=sha(data),engines=len(routes['engines']),groups=len(reports),sequences=reports,originalGaps=gaps,existingMWEnginesReplaced=0,gameAccepted=False)
    (output/'build-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('sequences',)}))
if __name__=='__main__': main(*sys.argv[1:])
