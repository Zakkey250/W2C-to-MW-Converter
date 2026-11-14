"""Direct indexed model conversion, with complete source-attribute auxiliaries.

GPU clusters retain 40B P3N3C4UV2S1 and IB16. Original tangent/skin/secondary
attributes, full materials, source-index correspondence and malformed original
vertices are preserved separately; invalid primitives are never submitted.
"""
import hashlib,json,mmap,struct,sys,zlib
from pathlib import Path
import numpy as np
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'source-world'))
from source_ir import Container,model,chunks
MAX_CLUSTER=1024**2
TRIANGLES_PER_CLUSTER=6000

def align(data,n):data.extend(bytes((-len(data))%n))
def convert(key,m,source_sha,raw_info=None,source_stream=None):
    mats=[];clusters=[];cluster_payloads=[];rejected=[];rendered=0;source_indices=0;represented=[np.zeros(len(v),bool) for v in m['vertices']]
    for mi,a in enumerate(m['materials']):
        source=m['vertices'][a['set']];indices=a['indices'].reshape(-1,3);source_indices+=indices.size
        floats=source.view('<f4');finite=np.isfinite(floats[:,[0,1,2,3,4,5,6,7]]).all(1)
        accepted=finite[indices].all(1);bad=np.flatnonzero(~accepted)
        rejected.extend((mi,int(i)) for i in bad)
        triangle_ids=np.flatnonzero(accepted);first=len(clusters)
        for start in range(0,len(triangle_ids),TRIANGLES_PER_CLUSTER):
            ids=triangle_ids[start:start+TRIANGLES_PER_CLUSTER];original=indices[ids].ravel();unique,inverse=np.unique(original,return_inverse=True)
            original_vertices=source[unique];gpu=np.zeros((len(unique),10),'<u4');gpu[:,:3]=original_vertices[:,:3];gpu[:,3:6]=original_vertices[:,5:8];gpu[:,6]=original_vertices[:,17];gpu[:,7:9]=original_vertices[:,3:5];gpu[:,9]=np.float32(1).view('<u4')
            represented[a['set']][unique]=True
            missing_normal=(original_vertices[:,19]&1)==0
            if missing_normal.any():gpu[missing_normal,3:6]=np.array([0,0,np.float32(1).view('<u4')],'<u4')
            missing_color=(original_vertices[:,19]&16)==0;gpu[missing_color,6]=0xffffffff
            ib=np.asarray(inverse,'<u2');vb=gpu.tobytes();indices_bytes=ib.tobytes();accounting=128+192+len(vb)+len(ib)*4+len(unique)*4+len(ids)*4
            if len(unique)>65535 or accounting>MAX_CLUSTER:raise ValueError('Model cluster total-byte budget')
            lo=gpu[:,:3].view('<f4').min(0);hi=gpu[:,:3].view('<f4').max(0)
            clusters.append(dict(material=mi,vertices=len(unique),indices=len(ib),bounds=np.r_[lo,hi].tolist(),accounting=accounting,sourceFirst=int(ids[0]),sourceCount=len(ids)))
            cluster_payloads.append((vb,indices_bytes,np.asarray(unique,'<u4').tobytes(),np.asarray(ids,'<u4').tobytes()))
            rendered+=len(ib)
        mats.append(dict(source=a,first=first,clusters=len(clusters)-first))
    material_at=256;cluster_at=material_at+len(mats)*96;payload_at=(cluster_at+len(clusters)*96+63)&~63
    data=bytearray(payload_at)
    for ci,(c,(vb,ib,remap,triangles)) in enumerate(zip(clusters,cluster_payloads)):
        align(data,64);vo=len(data);data.extend(vb);align(data,4);io=len(data);data.extend(ib);align(data,4);ro=len(data);data.extend(remap);to=len(data);data.extend(triangles)
        at=cluster_at+ci*96;struct.pack_into('<4I4Q4I6f2I',data,at,ci,c['material'],c['vertices'],c['indices'],vo,io,ro,to,c['sourceFirst'],c['sourceCount'],40,2,*c['bounds'],c['accounting'],zlib.crc32(vb+ib)&0xffffffff)
    for i,x in enumerate(mats):
        a=x['source'];at=material_at+i*96
        struct.pack_into('<12I',data,at,i,a['set'],a['effect'],a['flags'],a['diffuse'],a['normal'] or 0,a['specular'] or 0,a['sourceIndexStart'],len(a['indices']),sum(c['indices'] for c in clusters[x['first']:x['first']+x['clusters']]),x['first'],x['clusters'])
    align(data,8);aux_at=len(data);aux=bytearray(b'W2SAUX1\0');raw_mats=raw_info.get('fullMaterials',[]) if raw_info else []
    aux.extend(struct.pack('<4I',len(m['vertices']),len(raw_mats),len(rejected),len(raw_info.get('vertexAuxiliary',[])) if raw_info else 0))
    for v in m['vertices']:
        flags=v[:,19];uniform=int(flags[0]) if len(flags) and np.all(flags==flags[0]) else 0xffffffff;mask=int(np.bitwise_or.reduce(flags)) if len(flags) else 0
        aux.extend(struct.pack('<4I',len(v),mask,uniform,0))
        if uniform==0xffffffff:aux.extend(np.asarray(flags,'u1').tobytes());align(aux,4)
        for bit,cols in [(2,[8,9,10]),(4,[11,12,13]),(8,[14,15,16]),(32,[18])]:
            if mask&bit:aux.extend(np.ascontiguousarray(v[:,cols]).tobytes())
    for r in raw_mats:
        header=bytes.fromhex(r['sourceHeaderHex'])
        if len(header)!=144:raise ValueError('Original complete material header')
        aux.extend(header)
    for mi,ti in rejected:aux.extend(struct.pack('<II',mi,ti))
    if raw_info:
        if source_stream is None:raise ValueError('Original auxiliary source required')
        for r in raw_info['vertexAuxiliary']:
            n=r['vertices'];width=r['auxiliaryBytesPerVertex'];at=r['sourceOffset']+r['auxiliaryOffset'];stride=r['stride']
            if at+(n-1)*stride+width>len(source_stream):raise ValueError('Original auxiliary attribute bounds')
            values=np.ndarray((n,width),'u1',buffer=source_stream,offset=at,strides=(stride,1)).tobytes()
            if hashlib.sha256(values).hexdigest()!=r['sha256']:raise ValueError('Original auxiliary attribute fingerprint')
            aux.extend(struct.pack('<4I',r['stream'],n,width,r['auxiliaryOffset']));aux.extend(values);align(aux,4)
        other=raw_info['otherSourceMetadata'];aux.extend(struct.pack('<I',len(other)))
        for r in other:
            values=source_stream[r['offset']:r['offset']+r['bytes']]
            if hashlib.sha256(values).hexdigest()!=r['sha256']:raise ValueError('Source material-light metadata fingerprint')
            aux.extend(struct.pack('<II',int(r['kind'],16),len(values)));aux.extend(values);align(aux,4)
    else:aux.extend(struct.pack('<I',0))
    aux.extend(struct.pack('<I',len(m['markers'])))
    for marker in m['markers']:aux.extend(struct.pack('<I',len(marker)));aux.extend(marker);align(aux,4)
    aux.extend(struct.pack('<I',len(m['morphs'])))
    for morph in m['morphs']:aux.extend(struct.pack('<I',morph))
    invalid=[]
    for si,v in enumerate(m['vertices']):
        # Valid vertices adjacent to a rejected triangle can also disappear
        # from every rendered cluster. Preserve those exact originals too.
        for vi in np.flatnonzero(~represented[si]|~np.isfinite(v.view('<f4')[:,:8]).all(1)):invalid.append((si,int(vi),v[vi].tobytes()))
    aux.extend(struct.pack('<I',len(invalid)))
    for si,vi,raw in invalid:aux.extend(struct.pack('<II',si,vi));aux.extend(raw)
    original_header=b''
    if raw_info:
        def find_header(start,end):
            for kind,at,a,b in chunks(source_stream,start,end):
                if kind==0x134011:return bytes(source_stream[(a+15)&~15:b])
                if kind&0x80000000:
                    value=find_header(a,b)
                    if value:return value
            return b''
        original_header=find_header(raw_info['sourceObjectOffset']+8,raw_info['sourceObjectOffset']+raw_info['sourceObjectBytes'])
        if len(original_header)<160:raise ValueError('Complete original model header required')
    aux.extend(struct.pack('<I',len(original_header)));aux.extend(original_header);align(aux,4)
    data.extend(aux);data[:8]=b'W2SGPU1\0';struct.pack_into('<12I',data,8,1,256,m['hash'],len(mats),len(clusters),40,sum(len(v) for v in m['vertices']),source_indices,rendered,len(rejected),96,96)
    struct.pack_into('<7Q',data,56,len(data),material_at,cluster_at,payload_at,aux_at,len(aux),0)
    data[112:144]=bytes.fromhex(source_sha);data[144:208]=m['pivot'].tobytes()
    finite_bounds=m['bounds'];struct.pack_into('<6f',data,208,*finite_bounds);struct.pack_into('<I',data,232,zlib.crc32(data[256:])&0xffffffff);struct.pack_into('<I',data,236,zlib.crc32(data[:256])&0xffffffff)
    if len(data)>64*1024**2:raise ValueError('Model asset size cap')
    return bytes(data),dict(hash=key,materials=len(mats),clusters=len(clusters),sourceVertices=sum(len(v) for v in m['vertices']),sourceIndices=source_indices,renderedIndices=rendered,rejectedOriginalTriangles=len(rejected),maximumClusterBytes=max((c['accounting'] for c in clusters),default=0),bounds=finite_bounds.tolist(),sourceAuxiliaryPreserved=True)

def compile_all(source_ir,output,original_index=None,original_stream=None):
    source_ir=Path(source_ir);output=Path(output)
    if output.exists() and any(output.iterdir()):raise ValueError('Immutable model compilation exists')
    output.mkdir(parents=True,exist_ok=True);bank=Container(source_ir,'models');details={}
    if original_index:
        with Path(original_index).open(encoding='utf-8') as f:
            for line in f:r=json.loads(line);details[r['key']]=r
    stream_file=Path(original_stream).open('rb') if original_stream else None;raw=mmap.mmap(stream_file.fileno(),0,access=mmap.ACCESS_READ) if stream_file else None
    statistics=dict(models=0,clusters=0,sourceVertices=0,sourceIndices=0,renderedIndices=0,rejectedOriginalTriangles=0,maximumClusterBytes=0)
    try:
        with (output/'models.gpu').open('xb') as f,(output/'models.gpu.jsonl').open('x',encoding='utf-8') as index:
            for key,row in bank.entries.items():
                m=model(bank.get(key));data,meta=convert(key,m,row['sha256'],details.get(key),raw);padding=(-f.tell())%64;f.write(bytes(padding));at=f.tell();f.write(data)
                meta['Name']=row['metadata']['Name'];index.write(json.dumps(dict(key=key,offset=at,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),metadata=meta),separators=(',',':'))+'\n');del m
                for name in ('clusters','sourceVertices','sourceIndices','renderedIndices','rejectedOriginalTriangles'):statistics[name]+=meta[name]
                statistics['maximumClusterBytes']=max(statistics['maximumClusterBytes'],meta['maximumClusterBytes']);statistics['models']+=1
                if statistics['models']%5000==0:print(json.dumps(dict(phase='compile-source-models',models=statistics['models'])),flush=True)
    finally:
        if raw:raw.close()
        if stream_file:stream_file.close()
    (output/'compile-summary.json').write_text(json.dumps(statistics,indent=2),encoding='utf-8');print(json.dumps(dict(phase='source-models-complete',**statistics)),flush=True)
    return statistics

if __name__=='__main__':
    compile_all(sys.argv[1],sys.argv[2],sys.argv[3] if len(sys.argv)>3 else None,sys.argv[4] if len(sys.argv)>4 else None)
