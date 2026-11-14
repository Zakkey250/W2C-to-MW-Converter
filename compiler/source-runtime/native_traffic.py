"""Offline MW-indexed World roads and traffic polygons from supplied originals.

Never writes to a game installation. Runtime traffic acceptance is separate.
"""
import argparse,hashlib,json,math,struct,sys
from collections import defaultdict
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree

from calculate_port import chunk, read_graph, walk
from traffic_geometry import (
    BUCKET, ORDER, REPLACED, align8, cost_rows, graph_components, inside, polygons,
    registry, replace_children, replace_top, world_polygons,
)

def sha(data):return hashlib.sha256(data).hexdigest().upper()

def source_tables(payload):
    marker=payload.find(b'PRAC',0,128)
    if marker<0:raise ValueError('Missing source CARP')
    out={}
    for pos in range(marker+16,marker+16+16*128,16):
        if payload[pos:pos+4]==b'\xaa'*4:break
        tag=payload[pos:pos+4][::-1].decode('latin1')
        if tag not in ORDER:continue
        packed,count,relative=struct.unpack_from('<III',payload,pos+4)
        start=pos+relative;size=packed>>8
        if packed&255!=2 or start+size>len(payload):raise ValueError('Invalid '+tag)
        out[tag]=dict(data=payload[start:start+size],count=count)
    if not set(REPLACED)<=out.keys():raise ValueError('Incomplete source roads')
    return out

def paths(base,world):
    parent=chunk(base,0x80034150)
    index=bytearray(chunk(parent,0x34151));oldpaths=chunk(parent,0x34152)
    listed=struct.unpack_from('<I',index,4)[0]
    ids=list(struct.unpack_from('<'+'H'*listed,index,8))
    pathids=set();at=0
    while at<len(oldpaths):
        if struct.unpack_from('<II',oldpaths,at)!=(11,11):raise ValueError('Invalid MW polygon')
        pid,n=struct.unpack_from('<HB',oldpaths,at+8);pathids.add(pid);at+=36+8*n
    if at!=len(oldpaths):raise ValueError('MW polygon tail')
    source=world_polygons(Path(world).read_bytes())
    free=(i for i in range(1,2030) if i not in pathids and i not in ids)
    mapping={old:next(free) for old in sorted(source)}
    combined=bytearray(oldpaths)
    for old,new in mapping.items():
        row=bytearray(source[old][0]);struct.pack_into('<H',row,8,new);combined.extend(row)
    allids=sorted(ids+list(mapping.values()))
    # Stock MW lists four IDs twice. Retain its index entries verbatim in
    # multiplicity; only newly allocated World IDs must be unique/disjoint.
    if len(set(mapping.values()))!=len(mapping) or set(mapping.values())&(pathids|set(ids)):
        raise ValueError('Repeated new polygon index')
    required=8+len(allids)*2
    index.extend(b'\0'*max(0,required-len(index)))
    struct.pack_into('<I',index,4,len(allids));struct.pack_into('<'+'H'*len(allids),index,8,*allids)
    links=chunk(parent,0x34153);rows=[];at=0
    while at<len(links):
        pid=struct.unpack_from('<H',links,at+12)[0];end=at+20+2*links[at+15]
        if end>len(links):raise ValueError('Invalid MW links')
        rows.append((pid,links[at:end]));at=end
    template=next(row for pid,row in rows if pid==2028)
    for new in mapping.values():
        row=bytearray(template);struct.pack_into('<H',row,12,new);row[14]=1
        struct.pack_into('<HHH',row,16,1,new,0);rows.append((new,bytes(row)))
    changes={0x34151:bytes(index),0x34152:bytes(combined),0x34153:b''.join(row for _,row in sorted(rows))}
    revised=replace_children(parent,changes)
    index.extend(b'\0'*((-(len(revised)-len(parent)))%128));changes[0x34151]=bytes(index)
    revised=replace_children(parent,changes)
    output=replace_top(base,0x80034150,revised)
    return output,mapping

def convert(base,source,world):
    mt=registry(chunk(base,0x3B800));st=source_tables(chunk(source,0x3B800))
    mp,me,mi=read_graph(base);wp,we,wi=read_graph(source)
    wp=wp+np.array([-546.,0.,-6016.])
    nodesource=bytearray(st['RNnd']['data'])
    for i,p in enumerate(wp):struct.pack_into('<3f',nodesource,i*32,*p)
    st['RNnd']['data']=bytes(nodesource)
    dist,nearest=cKDTree(wp[:,[0,2]]).query(mp[:,[0,2]])
    matches={i:int(j) for i,j in enumerate(nearest) if dist[i]<.02}
    if len(matches)<4000 or len(set(matches.values()))!=len(matches):raise ValueError('Ambiguous road registration')
    node_map={j:i for i,j in matches.items()};added_nodes=[]
    for j in range(len(wp)):
        if j not in node_map:node_map[j]=len(mp)+len(added_nodes);added_nodes.append(j)
    pair_index={tuple(sorted(map(int,e))):i for i,e in enumerate(me)}
    if len(pair_index)!=len(me):raise ValueError('Duplicate MW road pair')
    segment_map={};added_segments=[]
    for sid,(a,b) in enumerate(we):
        pair=tuple(sorted((node_map[int(a)],node_map[int(b)])))
        if pair in pair_index:segment_map[sid]=pair_index[pair]
        else:segment_map[sid]=len(me)+len(added_segments);added_segments.append(sid)
    if len(set(segment_map.values()))!=len(we):raise ValueError('Ambiguous road remap')
    nodes=bytearray(mt['RNnd']['data']);segments=bytearray(mt['RNsg']['data'])
    # IDs/profile identity stay native; W2C is the world-coordinate authority.
    # Matching only horizontal coordinates must not retain MW's different
    # elevation (23 shared original nodes differ by more than one metre).
    for mw_id,source_id in matches.items():nodes[mw_id*32:mw_id*32+12]=nodesource[source_id*32:source_id*32+12]
    for sid in added_nodes:
        row=bytearray(st['RNnd']['data'][sid*32:(sid+1)*32]);struct.pack_into('<H',row,12,node_map[sid])
        profile=struct.unpack_from('<h',row,14)[0]
        if not -1<=profile<wi['profiles']:raise ValueError('Invalid profile')
        if profile>=0:struct.pack_into('<h',row,14,profile+mi['profiles'])
        nodes.extend(row)
    for sid in added_segments:
        row=bytearray(st['RNsg']['data'][sid*22:(sid+1)*22]);a,b=struct.unpack_from('<HH',row)
        struct.pack_into('<HH',row,0,node_map[a],node_map[b]);road=struct.unpack_from('<h',row,6)[0]
        if not -1<=road<wi['roads']:raise ValueError('Invalid road')
        if road>=0:struct.pack_into('<h',row,6,road+mi['roads'])
        struct.pack_into('<H',row,8,segment_map[sid]);segments.extend(row)
    adj=defaultdict(list)
    for sid in range(len(segments)//22):
        a,b=struct.unpack_from('<HH',segments,sid*22)
        if a==b:raise ValueError('Self edge')
        adj[a].append(sid);adj[b].append(sid)
    for n in range(len(nodes)//32):
        if n<len(mp):
            count=nodes[n*32+16];old=list(struct.unpack_from('<7H',nodes,n*32+18)[:count]);links=old+[s for s in adj[n] if s not in old]
        else:
            oldid=added_nodes[n-len(mp)];count=nodesource[oldid*32+16]
            links=[segment_map[s] for s in struct.unpack_from('<7H',nodesource,oldid*32+18)[:count]]
        if set(links)!=set(adj[n]) or len(links)>7:raise ValueError('Invalid adjacency')
        nodes[n*32+16]=len(links);struct.pack_into('<7H',nodes,n*32+18,*(links+[65535]*(7-len(links))))
        if n<len(mp) and nodes[n*32+12:n*32+16]!=mt['RNnd']['data'][n*32+12:n*32+16]:raise ValueError('MW node ordinal/profile identity changed')
    if segments[:len(mt['RNsg']['data'])]!=mt['RNsg']['data']:raise ValueError('MW segment prefix changed')
    combined,mapping=paths(base,world);poly,buckets=polygons(combined)
    position=np.array([struct.unpack_from('<3f',nodes,i*32) for i in range(len(nodes)//32)]);edges=np.array([struct.unpack_from('<HH',segments,i*22) for i in range(len(segments)//22)])
    # Rebuild references using final IDs, never copied cost-grid segment IDs.
    cells=defaultdict(lambda:[set(),set()]);covered=set()
    origin_x,origin_y=-13760.,-13376.;rows,columns=288,256
    oldgrid=mt['CGrd']['data'];ox,_,oy=struct.unpack_from('<3f',oldgrid)
    oldcols=struct.unpack_from('<I',oldgrid,28)[0];previous={}
    for cell,pairs,row in cost_rows(mt['CGcn']['data']):
        nx=(ox+(cell%oldcols)*64-origin_x)/64
        ny=(oy+(cell//oldcols)*64-origin_y)/64
        if abs(nx-round(nx))>1e-5 or abs(ny-round(ny))>1e-5:raise ValueError('MW grid registration')
        previous[int(round(ny))*columns+int(round(nx))]={pid:link for link,pid in pairs}
    for sid,(a,b) in enumerate(edges):
        ax,ay=position[a,2],-position[a,0];bx,by=position[b,2],-position[b,0]
        steps=max(1,math.ceil(math.hypot(bx-ax,by-ay)/24))
        for step in range(steps+1):
            f=step/steps;x,y=ax+(bx-ax)*f,ay+(by-ay)*f
            hits=[pid for pid in buckets.get((math.floor(x/BUCKET),math.floor(y/BUCKET)),()) if poly[pid][0][0]<=x<=poly[pid][0][2] and poly[pid][0][1]<=y<=poly[pid][0][3] and inside(poly[pid][1],x,y)]
            if not hits:continue
            ix=math.floor((-y-origin_x)/64);iy=math.floor((x-origin_y)/64)
            if not (0<=ix<columns and 0<=iy<rows):raise ValueError('Road outside grid')
            refs,pids=cells[iy*columns+ix];refs.add(sid);pids.update(hits);covered.add(sid)
    cost=[]
    for cell,(refs,pids) in sorted(cells.items()):
        if max(len(refs),len(pids))>255:raise ValueError('Cell capacity')
        pairs=b''.join(struct.pack('<HH',previous.get(cell,{}).get(pid,0),pid) for pid in sorted(pids))
        cost.append(struct.pack('<II4B4H',0,cell,len(pids),0,0,len(refs),0,len(pairs),len(pairs),len(pairs))+pairs+struct.pack('<'+'I'*len(refs),*sorted(refs)))
    profiles=mt['RNpf']['data']+st['RNpf']['data'];roads=mt['RNrd']['data']+st['RNrd']['data']
    header=bytearray(mt['RNhd']['data']);struct.pack_into('<HHH',header,0,len(profiles)//64,len(nodes)//32,len(segments)//22);struct.pack_into('<H',header,8,len(roads)//8)
    cgrid=bytearray(mt['CGrd']['data']);struct.pack_into('<3f',cgrid,0,origin_x,0,origin_y);struct.pack_into('<II',cgrid,24,rows,columns)
    tables={'CGcn':(b''.join(cost),len(cost)),'CGrd':(bytes(cgrid),1),'RNhd':(bytes(header),mt['RNhd']['count']),'RNnd':(bytes(nodes),len(nodes)//32),'RNsg':(bytes(segments),len(segments)//22),'RNpf':(profiles,len(profiles)//64),'RNrd':(roads,len(roads)//8)}
    old=chunk(combined,0x3B800);blob=bytearray(old[:264])
    for tag in ORDER:
        align8(blob);content,count=tables[tag];start=len(blob);blob.extend(content)
        struct.pack_into('<III',blob,mt[tag]['pos']+4,(len(content)<<8)|2,count,start-mt[tag]['pos'])
    blob.extend(b'\0'*((len(old)-len(blob))%128));built=replace_top(combined,0x3B800,bytes(blob))
    _,_,info=read_graph(built)
    if info['reciprocal_adjacency_errors']:raise ValueError('Graph verification failed')
    serialized=registry(chunk(built,0x3B800))
    for cell,pairs,row in cost_rows(serialized['CGcn']['data']):
        ns=row[11];nr=row[8];refs=struct.unpack_from('<'+'I'*ns,row,20+4*nr)
        if any(s>=len(edges) for s in refs) or any(pid not in poly for link,pid in pairs):raise ValueError('Dangling traffic reference')
    mainmask=graph_components(len(position),edges)
    missing_main=sorted(set(np.flatnonzero(mainmask).tolist())-covered)
    if missing_main:raise ValueError(f'Main component traffic coverage missing: {len(missing_main)}')
    for (k,s,e),(ak,ss,ee) in zip(walk(base),walk(built)):
        if k!=ak or (k not in (0x3B800,0x80034150) and base[s:e]!=built[ss:ee]):raise ValueError('Unrelated chunk changed')
    report=dict(baseSHA256=sha(base),sourceSHA256=sha(source),outputSHA256=sha(built),graph=info,mwNodePrefix=len(mp),mwSegmentPrefix=len(me),horizontalMatches=len(matches),sharedNodesReboundToExactOriginalW2CCoordinates=len(matches),nativeNodeAndSegmentIdentifiersPreserved=True,addedNodes=len(added_nodes),addedSegments=len(added_segments),polygons=len(poly),worldPolygonRemap=mapping,costCells=len(cost),coveredSegments=len(covered),uncoveredSegments=sorted(set(range(len(edges)))-covered),uncoveredMainComponent=missing_main,originalMWLinkPreferencesPreserved=True,runtimeVerified=False)
    return built,report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('mw-master','w2c-master','world-master','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise FileExistsError(a.output)
    built,report=convert(a.mw_master.read_bytes(),a.w2c_master.read_bytes(),a.world_master)
    a.output.mkdir(parents=True);(a.output/'L2RA.BUN').write_bytes(built)
    (a.output/'traffic-conversion.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('worldPolygonRemap','uncoveredSegments')},indent=2))
