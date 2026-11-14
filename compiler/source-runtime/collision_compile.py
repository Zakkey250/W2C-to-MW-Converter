"""Original CARP -> MW physics triangles/BVH, without old converted collision."""
from collections import Counter
import hashlib,json,struct,sys,time
from pathlib import Path
import numpy as np
C=Path(__file__).resolve().parents[1];sys.path.insert(0,str(C/'source-world'))
from source_ir import Container,scene
from master import groups

# The Rockport bus-terminal approach has an extra, invisible asphalt ramp
# over the visible street. All three W2C 2.0.0 streams contain this exact
# instance and article. Both signatures are required: group IDs can be reused.
BUS_TERMINAL_RAMP_GROUP = 1007
BUS_TERMINAL_RAMP_INSTANCE = '66617c5b304e9f5823982bb27b350533ccca0ea615a3b93c049233faa60d7eaf'
BUS_TERMINAL_RAMP_ARTICLE = '42118fec2b08b0b2d2740d215d1610f60bf9280c37ab89491bde11af33c86a25'

def directory(p):
    marker=bytes(p[:128]).find(b'PRAC')
    if marker<0:raise ValueError('Collision CARP signature')
    result={}
    for at in range(marker+16,min(marker+65536,len(p)-15),16):
        tag,packed,count,relative=struct.unpack_from('<4I',p,at)
        if tag==0xaaaaaaaa:break
        name=bytes(p[at+2:at+4])
        if name not in (b'ac',b'ic'):continue
        size=packed>>8;start=at+relative
        if packed&255!=3 or start<at+16 or start+size>len(p):raise ValueError('Collision article bounds')
        identity=(name[::-1].decode(),tag&65535)
        if identity in result:raise ValueError('Collision duplicate article identity')
        result[identity]=(p[start:start+size],count)
    else:raise ValueError('Collision directory terminator')
    if not result:raise ValueError('Collision empty directory')
    return result

def geometry(p):
    if len(p)<16:raise ValueError('Collision geometry header')
    strips,sbytes,edges,ebytes,resolved,surfaces,surfbytes,inter,flags=struct.unpack_from('<4HBB3H',p)
    if resolved or surfbytes!=surfaces*4 or ebytes!=edges*32 or 16+sbytes+ebytes+surfbytes>len(p):raise ValueError('Collision geometry dimensions')
    surface=struct.unpack_from('<'+str(surfaces)+'I',p,16+sbytes+ebytes);tri=[];ids=[];vflags=[]
    for i in range(strips):
        at=16+i*16;sphere=np.array(struct.unpack_from('<3f',p,at));radius,offset=struct.unpack_from('<HH',p,at+12);vbase=16+offset
        if offset<strips*16 or offset+8>sbytes:raise ValueError('Collision strip range')
        count=p[vbase+6]
        if not 3<=count<=255 or offset+count*8>sbytes:raise ValueError('Collision strip vertex count')
        vertices=[];ss=[];ff=[]
        for n in range(count):
            x,y,z,s,f=struct.unpack_from('<3hBB',p,vbase+8*n);vertices.append(sphere+np.array([x,y,z])/128.);ss.append(s);ff.append(f)
        vertices=np.array(vertices)
        if np.max(np.linalg.norm(vertices-sphere,axis=1))>radius/16.+1.:raise ValueError('Original strip sphere mismatch')
        for n in range(2,count):
            if ss[n]>=len(surface):raise ValueError('Collision surface index')
            order=[n-2,n-1,n] if n%2==0 else [n-1,n-2,n];tri.append(vertices[order]);ids.append(surface[ss[n]]);vflags.append(ff[n])
    walls=[];wall_ids=[]
    for i in range(edges):
        at=16+sbytes+32*i;a=struct.unpack_from('<3f',p,at);b=struct.unpack_from('<3f',p,at+16);sid,flag=struct.unpack_from('<BB',p,at+12)
        if sid>=len(surface):raise ValueError('Collision wall surface')
        v=np.array([[a[0],a[1],a[2]],[b[0],a[1],b[2]],[b[0],b[1],b[2]],[a[0],b[1],a[2]]]);walls.extend([v[[0,1,2]],v[[0,2,3]]]);wall_ids.extend([surface[sid]]*2)
    return np.asarray(tri,dtype='<f8').reshape(-1,3,3),np.asarray(ids,'<u4'),np.asarray(vflags,'u1'),np.asarray(walls,dtype='<f8').reshape(-1,3,3),np.asarray(wall_ids,'<u4')

def inactive_barriers(master,scenes):
    definitions,_=groups(master);inactive=set();cache={}
    for key,g in definitions.items():
        if not g['overrides']:continue
        hidden=True
        for sid,ordinal,flags in g['overrides']:
            if sid not in cache:cache[sid]=scene(scenes.get(str(sid)))
            s=cache[sid]
            if ordinal>=s['instanceCount']:raise ValueError('Source group placement reference')
            ii=struct.unpack_from('<h',s['instances'],ordinal*96+84)[0];name=bytes(s['infos'][ii*72:ii*72+24]).split(b'\0')[0].decode('ascii')
            if not flags&0x3a or not ('BARRIER' in name or 'BARRMETHWY' in name):hidden=False;break
        if hidden:inactive.add(key)
    return definitions,inactive

def compile_collision(source_root,master_path,carbon_attributes,mw_attributes,destination,*,surface_policy=None,repair_bus_terminal=False):
    started=time.monotonic();bank=Container(source_root,'systems');scenes=Container(source_root,'scenes');master=Path(master_path).read_bytes();defs,inactive=inactive_barriers(master,scenes)
    arrays=[];surfaces=[];kinds=[];stats=Counter()
    for key,row in bank.entries.items():
        if row['metadata']['origin']['chunk']!='0003b801':continue
        items=directory(bank.get(key));geometries={i:geometry(p) for (kind,i),(p,n) in items.items() if kind=='ca'};stats['packs']+=1;stats['articles']+=len(geometries)
        for (kind,ordinal),(p,n) in items.items():
            if kind!='ci':continue
            if len(p)!=n*64:raise ValueError('Collision instance exact stride')
            for i in range(n):
                at=i*64;r1=np.array(struct.unpack_from('<3f',p,at));r3=np.array(struct.unpack_from('<3f',p,at+32));pos=np.array(struct.unpack_from('<3f',p,at+48));flag=p[at+18];group,article=struct.unpack_from('<HH',p,at+24);inv=np.array([r1,np.cross(r3,r1),r3]);stats['instances']+=1
                if group and group not in defs:raise ValueError('Unknown original collision group')
                if article not in geometries or np.max(np.abs(inv@inv.T-np.eye(3)))>=.015:raise ValueError('Collision article/rigid basis')
                if flag&6:stats['dynamicOrDisabledInstances']+=1;continue
                if group in inactive:stats['inactiveBarrierInstances']+=1;continue
                tri,ids,ff,walls,wids=geometries[article]
                if (repair_bus_terminal and group==BUS_TERMINAL_RAMP_GROUP
                    and hashlib.sha256(p[at:at+64]).hexdigest()==BUS_TERMINAL_RAMP_INSTANCE
                    and hashlib.sha256(items['ca',article][0]).hexdigest()==BUS_TERMINAL_RAMP_ARTICLE):
                    stats['busTerminalHiddenRampInstances']+=1
                    stats['busTerminalHiddenRampFaces']+=len(tri)
                    continue
                if len(tri):
                    world=(tri-pos)@inv;normal=np.cross(world[:,1]-world[:,0],world[:,2]-world[:,0]);length=np.linalg.norm(normal,axis=1);keep=(length>=1e-7)&((ff&128)==0)&(np.abs(normal[:,1])>=.05*length)
                    stats['sourceStripTriangles']+=len(tri);stats['excludedStripTriangles']+=int((~keep).sum());world=world[keep];source_ids=ids[keep];normal=normal[keep]
                    # Rendering (z,-x,height) changes handedness. The accepted
                    # MW CARP contract uses negative raw-Y floor winding.
                    flip=normal[:,1]>0
                    world[flip]=world[flip][:,[1,0,2]];world+=np.array([-546.,0.,-6016.]);arrays.append(np.float32(world));surfaces.append(source_ids);kinds.append(np.zeros(len(world),'<u4'))
                if len(walls):arrays.append(np.float32((walls-pos)@inv+[-546.,0.,-6016.]));surfaces.append(wids);kinds.append(np.ones(len(walls),'<u4'));stats['wallTriangles']+=len(walls)
    if repair_bus_terminal and stats['busTerminalHiddenRampInstances']!=1:
        raise ValueError('Expected exactly one original Rockport bus terminal ramp')
    points=np.concatenate(arrays);hashes=np.concatenate(surfaces);wall=np.concatenate(kinds)
    if not len(points) or not np.isfinite(points).all():raise ValueError('Converted physics coordinates')
    diagnostic=Path(destination).with_suffix('.surfaces.json')
    diagnostic.parent.mkdir(parents=True,exist_ok=True)
    u,c=np.unique(hashes,return_counts=True)
    diagnostic.write_text(json.dumps(dict(selected_floor_surfaces={f'{h:08X}':int(n) for h,n in zip(u,c)},statistics=dict(stats)),indent=2),encoding='utf-8')
    if surface_policy is None:
        original={int(r['hash'],16):r['name'] for r in json.loads(Path(carbon_attributes).read_text())['rows'] if r['class']=='simsurface'};supported={r['name'] for r in json.loads(Path(mw_attributes).read_text())['rows'] if r['class']=='simsurface'}
    else:
        original={int(h,16):name for h,name in surface_policy['materials'].items()}
        supported=set(original.values())
    # Named original W2C/World surface references absent from Carbon's vault.
    # These exact hash/name pairs were independently resolved by VLT32Hasher
    # in the retained V0 source preflight; they are adapter input, not geometry.
    named_external={0x09ebff97:'grass_medium_tile',0xc0625782:'asphalt_stripe_yellow',0x40afea0b:'None'}
    if surface_policy is None:original.update(named_external)
    aliases={'asphalt_drift':'asphalt','drift_scoring_band':'asphalt_no_leaves','asphalt_stripe_yellow':'asphalt','grass_medium_tile':'grass','gravel_drift':'gravel','tree_canopy':'wood','puddle':'asphalt','None':'asphalt_no_leaves'};surface_map={}
    unresolved_wall_materials=[]
    for h in np.unique(hashes):
        name=original.get(int(h))
        if name is None and h!=0:
            if np.any(wall[hashes==h]!=1):raise ValueError(f'Original source floor surface name missing {h:08x}')
            if surface_policy is not None and f'{h:08x}' not in surface_policy['concreteWallOnly']:
                raise ValueError(f'Unclassified source wall surface {h:08x}')
            # The original has no recoverable simsurface definition for these
            # wall-only hashes. Retain them in the mapping ledger and use the
            # native rigid-wall response; never infer a missing floor material.
            unresolved_wall_materials.append(f'{h:08x}');surface_map[int(h)]='concrete';continue
        dest=name if name in supported else aliases.get(name,'asphalt_no_leaves' if int(h)==0 else None)
        if dest is None or dest not in supported:raise ValueError(f'Native surface adapter missing {h:08x} {name}')
        surface_map[int(h)]=dest
    names=sorted(set(surface_map.values()));name_index={n:i for i,n in enumerate(names)};mapped=np.array([name_index[surface_map[int(h)]] for h in hashes],'<u4');lo=points.min(1);hi=points.max(1);centers=(lo+hi)*.5;nodes=[];order=[]
    def build(ids):
        index=len(nodes);bounds=np.r_[lo[ids].min(0),hi[ids].max(0)];nodes.append(None)
        if len(ids)<=8:
            first=len(order);order.extend(ids.tolist());nodes[index]=(*bounds,first,len(ids)|0x80000000)
        else:
            axis=int(np.argmax(np.ptp(centers[ids],axis=0)));middle=len(ids)//2;split=np.argpartition(centers[ids,axis],middle);left=build(ids[split[:middle]]);right=build(ids[split[middle:]]);nodes[index]=(*bounds,left,right)
        return index
    build(np.arange(len(points),dtype='<u4'));order=np.array(order,'<u4');records=np.empty(len(order),dtype=np.dtype([('points','<f4',(3,3)),('surface','<u4'),('wall','<u4')]));records['points']=points[order];records['surface']=mapped[order];records['wall']=wall[order]
    destination=Path(destination)
    if destination.exists():raise ValueError('Immutable collision output exists')
    destination.parent.mkdir(parents=True,exist_ok=True)
    with destination.open('xb') as f:
        f.write(struct.pack('<5I',0x31434d57,1,len(points),len(nodes),len(names)))
        for name in names:f.write(name.encode('ascii').ljust(80,b'\0'))
        for node in nodes:f.write(struct.pack('<6f2I',*node))
        f.write(records.tobytes())
    report=dict(source='Original X360 CARP only',triangles=len(points),nodes=len(nodes),surfaces=names,surfaceMapping={f'{h:08x}':v for h,v in surface_map.items()},unresolvedOriginalWallHashesUsingNativeConcrete=unresolved_wall_materials,statistics=dict(stats),inactiveBarrierGroups=sorted(inactive),sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),bytes=destination.stat().st_size,seconds=time.monotonic()-started,oldConvertedCollisionRead=False)
    destination.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8');return report

if __name__=='__main__':print(json.dumps(compile_collision(*sys.argv[1:]),indent=2))
