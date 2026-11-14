# Private V2 port; source algorithms read-only, namespace w2v2_.
"""Preserve effective W2C breaker settings in isolated MW collections."""
import argparse, json, os, re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def compile_attributes(out, mw_rows, carbon_rows, names, *, all_source=True,
                       world_rows=None, environment_groups=None, environment_textures=None,
                       sequence_global=None, native_sequence_track=None, missing_only=False, namespace='w2v2_'):
 """Write a deterministic MW attribute recipe from explicit, audited rows."""
 out=Path(out)
 out.mkdir(parents=True,exist_ok=True)
 if (out/'attributes.nfsms').exists():raise FileExistsError('Preserve earlier attribute conversion')
 names=sorted(set(names)) if all_source else list(names)
 prefix=namespace+'env_' if environment_groups else namespace
 world_props=world_rows is not None
 environment_textures=environment_textures or {}
 def rows_index(rows):return {(r['class'],r['name']):{**r,'fields':dict(r['fields'])} for r in rows}
 carbon=rows_index(carbon_rows)
 datasets={'mw':rows_index(mw_rows),'carbon':carbon}
 if world_props:
  world=rows_index(world_rows)
  datasets['carbon']=world|carbon
 if sequence_global:
  import hashlib
  from audit_breaker_sequences import sequences
  from carbon_breaker_dependencies import collect_breaker_dependencies
  native_engines=sequences(native_sequence_track.read_bytes()) if native_sequence_track else ()
  source_global=sequence_global.read_bytes()
  closure=collect_breaker_dependencies(list(datasets['carbon'].values()),sequences(source_global),names,native_engines)
  names=closure['smackables']
  closure['sourceGlobal']=str(sequence_global)
  closure['sourceSha256']=hashlib.sha256(source_global).hexdigest()
  (out/'sequence-dependency-closure.json').write_text(json.dumps(closure,indent=2)+'\n')
 if missing_only:names=[n for n in names if ('smackable',prefix+n) not in datasets['mw']]

 def native_template(cls,name):
  candidate=name;seen=set()
  while (cls,candidate) not in datasets['mw']:
   assert candidate and candidate not in seen,('No MW schema ancestor',cls,name)
   seen.add(candidate);candidate=datasets['carbon'][cls,candidate]['parent']
  return candidate
 def effective(rows,cls,name):
  r=rows[cls,name]; result=effective(rows,cls,r['parent']) if r['parent'] else {};result.update(r['fields']);return result
 lines=['game MW'];report=[];refs={};pending={};compatibility=[]
 if world_props and 'gettymonumentstrut' in names:
  row=datasets['carbon']['smackable','gettymonumentstrut']
  if row['fields'].get('rigidbodyspecs')=='rigidbodyspecs -> 0x9C0A7DEE' and ('rigidbodyspecs','0x9C0A7DEE') not in datasets['carbon']:
   inherited=effective(datasets['carbon'],'smackable',row['parent'])['rigidbodyspecs']
   compatibility.append(dict(source='smackable/gettymonumentstrut/rigidbodyspecs',value=row['fields']['rigidbodyspecs'],relocated_value=inherited,reason='Source reference absent in W2C original Carbon and World; retain source parent rigid-body specification'))
   row['fields']['rigidbodyspecs']=inherited
 if world_props:
  for (cls,name),row in world.items():
   if cls!='smackable' or (cls,name) in carbon:continue
   for field,value in list(row['fields'].items()):
    if not field.startswith('On') or 'Surface: ' not in value:continue
    # World routes audio on contact entries (including OnDetached and inherited
    # entries). MW keeps that routing inside its existing effects collections.
    # Normalize every source ancestor, not only the selected leaf collections.
    body=value[1:-1] if value.startswith('[') and value.endswith(']') else value
    groups=re.split(r',(?=Surface: )| \| (?=Surface: )',body);normalized=[]
    for group in groups:
     members=[]
     for member in group.split(' | '):
      if member.split(': ',1)[0] in ('Audio','SFXMinSpeed','SFXMaxSpeed'):
       compatibility.append(dict(source=f'{cls}/{name}/{field}',member=member,reason='World collision audio member absent from MW ContactEffect schema'));continue
      members.append(member)
     normalized.append(' | '.join(members))
    row['fields'][field]='['+','.join(normalized)+']'
 audio_aliases={'lamp_med_2_car':'lamp_lrg_2_car','lamp_med_2_world':'lamp_lrg_2_world','plastic_garb_2_car':'trashcan_2_car','plastic_garb_2_world':'trashcan_2_world','parked_truck_2_car':'huge_object_2_car','parked_truck_2_world':'huge_object_2_world'}
 if world_props:audio_aliases.update({'plastic_garb_2_car':'trashcan_2_car','plastic_garb_2_world':'trashcan_2_world','parked_truck_2_car':'huge_object_2_car','parked_truck_2_world':'huge_object_2_world'})
 def reference(value):
  cls,name=value.split(' -> ',1)
  if not cls:return '0x00000000','0x00000000'
  if not name:return cls,'0x00000000'
  if cls=='audioimpact' and name in audio_aliases:
   compatibility.append(dict(source=value,target=f'{cls} -> {audio_aliases[name]}',reason='MW audio sample bank IDs'))
   return cls,audio_aliases[name]
  if ((cls,name) not in datasets['mw'] or environment_groups and cls in ('emittergroup','emitterdata','effects','fuelcell_effect','fuelcell_emitter','emitteruv')) and (cls,name) not in refs:
   assert cls in ('effects','emittergroup','emitterdata','rigidbodyspecs','fuelcell_effect','fuelcell_emitter','emitteruv'), ('Unresolved source reference',value)
   row=datasets['carbon'][cls,name];template=name if (cls,name) in datasets['mw'] else row['parent']
   while (cls,template) not in datasets['mw']:
    assert template,('No MW schema ancestor',value)
    template=datasets['carbon'][cls,template]['parent']
   refs[cls,name]=prefix+cls+'_'+name
   if not (missing_only and (cls,refs[cls,name]) in datasets['mw']):pending[cls,name]=template
  return cls,name
 def update(cls,target,selector,value):
  if value.startswith('['):
   entries=re.split(r',(?=[A-Za-z]+:)',value[1:-1]) if ' | ' in value else value[1:-1].split(',') if value!='[]' else []
   lines.append(f'resize_field {cls} {target} {selector} {len(entries)}')
   for i,v in enumerate(entries):update(cls,target,f'{selector}[{i}]',v)
  elif value.startswith('('):
   for axis,number in zip('XYZW',value.strip('()').split(', ')):lines.append(f'update_field {cls} {target} {selector} {axis} {number}')
  elif ' | ' in value:
   for component in value.split(' | '):
    member,number=component.split(': ',1)
    if ' -> ' in number:
     refcls,refname=reference(number)
     lines.extend([f'update_field {cls} {target} {selector} {member} ClassKey {refcls}',f'update_field {cls} {target} {selector} {member} CollectionKey {refs.get((refcls,refname),refname)}'])
    else:lines.append(f'update_field {cls} {target} {selector} {member} {number}')
  elif ' -> ' in value:
   refcls,refname=reference(value)
   lines.extend([f'update_field {cls} {target} {selector} ClassKey {refcls}',f'update_field {cls} {target} {selector} CollectionKey {refs.get((refcls,refname),refname)}'])
  else:lines.append(f'update_field {cls} {target} {selector} {value if value else chr(34)*2}')
 def clone(cls,name,target,source,source_name=None):
  if (cls,name) not in datasets['mw']:
   source_name=source_name or name;name=native_template(cls,name)
  native=datasets['mw'][cls,name];before=effective(datasets['mw'],cls,name)
  lines.append(f'copy_node {cls} {name} {native["parent"] or ""} {target}')
  changes={}
  for field,value in source.items():
   if (world_props or environment_groups) and cls=='emitterdata' and field=='Texture':
    match=re.fullmatch(r'mEnum: (\w+) \| mIndex: (\d+)',value)
    if match:
     indices={re.fullmatch(r'mEnum: (\w+) \| mIndex: (\d+)',row['fields'].get('Texture','')).group(2) for (c,n),row in datasets['mw'].items() if c=='emitterdata' and re.fullmatch(r'mEnum: (\w+) \| mIndex: (\d+)',row['fields'].get('Texture','')) and re.fullmatch(r'mEnum: (\w+) \| mIndex: (\d+)',row['fields']['Texture']).group(1)==match.group(1)}
     if environment_groups:
      key=match.group(1)
      if not key.isdigit():
       h=0xffffffff
       for c in key:h=(h*33+ord(c))&0xffffffff
       key=str(h)
      indices={str(environment_textures[key]['index'])}
     assert len(indices)==1,('Missing MW particle texture identity',match.group(1))
     adapted=f'mEnum: {match.group(1)} | mIndex: {next(iter(indices))}'
     if adapted!=value:compatibility.append(dict(source=f'{cls}/{source_name or name}/{field}',value=value,relocated_value=adapted,reason='Texture enum index relocated for MW sprite table'))
     value=adapted
   if field=='CollectionName' or (before.get(field)==value and not (' -> ' in value and tuple(value.split(' -> ',1)) in refs)):continue
   if field not in native['fields']:
    schema=set().union(*(r['fields'].keys() for (c,n),r in datasets['mw'].items() if c==cls))
    if field not in schema:
     compatibility.append(dict(source=f'{cls}/{source_name or name}/{field}',value=value,reason='Carbon particle extension absent from MW schema'));continue
    lines.append(f'add_field {cls} {target} {field}')
   update(cls,target,field,value);changes[field]={'mw':before.get(field),'carbon':value}
  report.append(dict(cls=cls,source=source_name or name,template=name,target=target,source_hash=native['hash'],changes=changes))
 if all_source:
  dependencies={tuple(effective(datasets['carbon'],'smackable',n)['rigidbodyspecs'].split(' -> ',1)) for n in names if effective(datasets['carbon'],'smackable',n)['rigidbodyspecs'].split(' -> ',1)[1]}
  for cls,name in sorted(dependencies):
   reference(f'{cls} -> {name}');target=prefix+'rigid_'+name.replace('0x','');refs[cls,name]=target
   if not (missing_only and (cls,target) in datasets['mw']):clone(cls,name,target,effective(datasets['carbon'],cls,name))
   pending.pop((cls,name),None)
 if environment_groups:
  for name in environment_groups:reference(f'emittergroup -> {name}')
 for name in names:clone('smackable',name,prefix+name,effective(datasets['carbon'],'smackable',name))
 completed=set()
 while pending.keys()-completed:
  for cls,name in sorted(pending.keys()-completed):
   clone(cls,pending[cls,name],refs[cls,name],effective(datasets['carbon'],cls,name),name);completed.add((cls,name))
 # References may point forward, so create every collection before field edits.
 lines=[lines[0]]+[l for l in lines[1:] if l.startswith('copy_node ')]+[l for l in lines[1:] if not l.startswith('copy_node ')]
 (out/'attributes.nfsms').write_text('\n'.join(lines)+'\n')
 (out/'attribute-adapter-plan.json').write_text(json.dumps(report,indent=2))
 (out/'attribute-compatibility.json').write_text(json.dumps(compatibility,indent=2))
 (out/'attribute-reference-aliases.json').write_text(json.dumps([dict(cls=c,source=n,target=t) for (c,n),t in sorted(refs.items())],indent=2))
 (out/'source-effective-adapted.json').write_text(json.dumps(dict(rows=list(datasets['carbon'].values())),indent=2))
 return dict(collections=len(report),commands=len(lines),source_names=names)


def main():
 parser=argparse.ArgumentParser()
 parser.add_argument('output',nargs='?',type=Path)
 for key in ('mw-audit','carbon-audit','world-audit'):parser.add_argument('--'+key,type=Path)
 parser.add_argument('--smackables',nargs='+',help='Explicit private conversion roots instead of geometry selection')
 parser.add_argument('--sequence-global',type=Path,help='Manually supplied W2C/Carbon GLOBALB: include sequencer-spawned child attributes')
 parser.add_argument('--native-sequence-track',type=Path,help='Retain shared MW engines already present in the supplied track bank')
 parser.add_argument('--missing-only',action='store_true',help='Preserve existing converted collections; emit only absent aliases')
 args=parser.parse_args()
 out=args.output or ROOT/'generated/PursuitPayload'
 if args.smackables and not args.output:parser.error('--smackables requires an explicit output directory')
 if args.missing_only and not args.output:parser.error('--missing-only requires an explicit output directory')
 names=args.smackables or (sorted({a['parameter_name'] for a in json.loads((out/'selection.json').read_text())['assets'].values()}) if args.output else ['woodroofsupport','woodroof','bigrollingtorus'])
 def audit(path):return json.loads(path.read_text())['rows']
 groups=os.environ.get('W2MW_ENVIRONMENT_GROUPS')
 options=dict(all_source=args.output is not None,missing_only=args.missing_only,
              sequence_global=args.sequence_global,native_sequence_track=args.native_sequence_track)
 if os.environ.get('W2MW_ALL_SOURCE_PROPS')=='1':
  options['world_rows']=audit(args.world_audit or ROOT/'evidence/props299-world-effective.json')
 if groups:
  options['environment_groups']=json.loads(Path(groups).read_text())
  options['environment_textures']=json.loads((out/'particle-texture-map.json').read_text())
 report=compile_attributes(out,audit(args.mw_audit or ROOT/'evidence/pb291-mw-effective.json'),
                           audit(args.carbon_audit or ROOT/'evidence/pb291-carbon-effective.json'),names,**options)
 print(json.dumps(report))


if __name__=='__main__':main()
