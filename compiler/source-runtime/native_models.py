"""MW native model registration envelopes from original normalized geometry.

The custom renderer retains the full 40B model and auxiliaries. These native
envelopes also preserve supported normal/tangent shaders for physical parts.
"""
import hashlib,json,mmap,struct,sys
from pathlib import Path
import numpy as np
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'source-world'))
from source_ir import Container,model,chunks
from prop_compile import MODEL_XOR
TEXTURE_XOR=0x1d8e56a3

def chunk(kind,data):return struct.pack('<II',kind,len(data))+data
def append(out,base,kind,data,alignment=1):out.extend(chunk(kind,b'\x11'*((-(base+len(out)+8))%alignment)+data))
def descendants(data,a,b):
    for kind,off,start,end in chunks(data,a,b):
        yield kind,off,start,end
        if kind&0x80000000:yield from descendants(data,start,end)

def compile_models(source,target,props,raw_index,raw_stream,pristine_global,output,native_stream=None):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    if (output/'models.bun').exists():raise ValueError('Immutable native models exist')
    source_bank=Container(source,'models');native_bank=Container(target,'models');assets=json.loads((Path(props)/'assets.json').read_text());wanted=sorted({key for a in assets.values() for key in a['models']});details={r['key']:r for r in map(json.loads,Path(raw_index).open(encoding='utf-8'))}
    native_raw={}
    if native_stream:
        expected=next(r for r in json.loads((Path(target)/'import-summary.json').read_bytes())['sources'] if r['role']=='mw-stream-pristine')
        with Path(native_stream).open('rb') as f:
            if hashlib.file_digest(f,'sha256').hexdigest()!=expected['sha256']:raise ValueError('Original native stream fingerprint')
            for key in wanted:
                if key in source_bank.entries:continue
                origin=native_bank.entries[key]['metadata']['origin']
                if origin['role']!='mw-stream-pristine':raise ValueError('Unsupported fallback original role')
                f.seek(origin['offset']);kind,n=struct.unpack('<II',f.read(8))
                if kind!=0x80134000 or n>64*1024**2:raise ValueError('Native original solid list bounds')
                raw=struct.pack('<II',kind,n)+f.read(n)
                for tag,off,a,b in chunks(raw,8):
                    if tag!=0x80134010:continue
                    parts={}
                    for k,o,s,e in descendants(raw,a,b):
                        align=16 if k in (0x134011,0x134900,0x134b02) else 1
                        if k in (0x134011,0x134900,0x134013,0x134b02):parts[k]=bytes(raw[s+(-(origin['offset']+s))%align:e])
                    if 0x134011 in parts and struct.unpack_from('<I',parts[0x134011],16)[0]==int(key,16):native_raw[key]=parts;break
                if key not in native_raw:raise ValueError('Native original object absent')
    template=Path(pristine_global).read_bytes();lists=[(a,b) for kind,off,a,b in chunks(template) if kind==0x80134000]
    if not lists:raise ValueError('Original native solid-list template missing')
    a,b=lists[0];list_head=next(bytes(template[s:e]) for kind,off,s,e in descendants(template,a,b) if kind==0x134002);native_object_head=next(bytes(template[(s+15)&~15:e]) for kind,off,s,e in descendants(template,a,b) if kind==0x134011);native_plat=next(bytes(template[(s+15)&~15:e]) for kind,off,s,e in descendants(template,a,b) if kind==0x134900)
    list_head=bytearray(list_head);struct.pack_into('<I',list_head,12,len(wanted));list_head[16:72]=b'W2CMW_V2_NATIVE_MODELS\0'.ljust(56,b'\0');list_head[72:104]=b'W2CMW_V2\0'.ljust(32,b'\0');keys=sorted((int(h,16)^MODEL_XOR for h in wanted));head=chunk(0x134002,list_head)+chunk(0x134003,b''.join(struct.pack('<II',h,0) for h in keys))+chunk(0x134004,b'');body=bytearray(chunk(0x80134001,head));audit=[];textures=set()
    with Path(raw_stream).open('rb') as rf,mmap.mmap(rf.fileno(),0,access=mmap.ACCESS_READ) as raw:
        for key in wanted:
            native=key not in source_bank.entries;bank=native_bank if native else source_bank;m=model(bank.get(key));source=details.get(key) if not native else None;original_parts={};original_streams=[]
            if source:
                origin=source['sourceObjectOffset'];end=origin+source['sourceObjectBytes']
                for kind,off,a,b in descendants(raw,origin+8,end):
                    if kind in (0x134011,0x134900,0x134013,0x134b02):original_parts[kind]=bytes(raw[(a+15)&~15:b]) if kind!=0x134013 else bytes(raw[a:b])
                if 0x134011 not in original_parts or 0x134900 not in original_parts:raise ValueError('Original model header/descriptor closure')
            elif native:original_parts=native_raw.get(key,{})
            if 0x134b02 not in original_parts:raise ValueError('Original native lighting/material ABI metadata required')
            source_header=original_parts.get(0x134011,native_object_head);header=bytearray(source_header[:160]);header[12]=0x16;header[13]=0;struct.pack_into('<I',header,16,int(key,16)^MODEL_XOR);struct.pack_into('<3f',header,32,*m['bounds'][:3]);struct.pack_into('<3f',header,48,*m['bounds'][3:]);header[64:128]=m['pivot'].tobytes()
            table=sorted({h for a in m['materials'] for h in (a['diffuse'],a['normal'],a['specular']) if h is not None});texture_id={h:i for i,h in enumerate(table)};textures.update(table)
            if len(table)>255 or len(m['markers'])>255:raise ValueError('Native byte-sized source table limit')
            header[25]=len(table);header[27]=len(m['markers']);materials=[];indices=[];streams=[];group_effect=None;stream_sets={};stream_count=0;num_vertices=0;num_indices=0
            for ordinal,a in enumerate(m['materials']):
                source_stride=104 if native else 144;original_group=original_parts[0x134b02][ordinal*source_stride:(ordinal+1)*source_stride]
                if len(original_group)!=source_stride:raise ValueError('Original material group range')
                effect=struct.unpack_from('<I',original_group,48)[0] if native else {0:0,1:1,3:3,4:1,5:3,25:5}.get(a['effect'])
                if effect not in (0,1,3,4,5):raise ValueError(f'Unverified native shader {key}:{a["effect"]}')
                if group_effect!=effect:streams.append(bytearray());stream_sets={};stream_count=0;group_effect=effect
                si=a['set'];v=m['vertices'][si]
                if si not in stream_sets:
                    stream_sets[si]=stream_count;stream_count+=len(v);num_vertices+=len(v)
                    if stream_count>65535:raise ValueError('Native stream index bound')
                    vb=np.zeros((len(v),15 if effect in (1,3) else 9),'<u4');vb[:,:3]=v[:,:3];vb[:,3:6]=v[:,5:8];vb[:,6]=v[:,17];vb[:,7:9]=v[:,3:5]
                    if effect in (1,3):
                        vb[:,11:14]=v[:,8:11]
                        aux=next((r for r in source['vertexAuxiliary'] if r['stream']==si and r['auxiliaryBytesPerVertex']==4),None) if source else None
                        if aux:
                            values=np.ndarray((aux['vertices'],4),'u1',buffer=raw,offset=aux['sourceOffset']+aux['auxiliaryOffset'],strides=(aux['stride'],1)).copy()
                            if hashlib.sha256(values.tobytes()).hexdigest()!=aux['sha256']:raise ValueError('Original native tangent W fingerprint')
                            vb[:,14]=values.view('<u4').ravel()
                    if not np.isfinite(vb.view('<f4')[:,[0,1,2,3,4,5,7,8]]).all():raise ValueError('Nonfinite original native physical model')
                    streams[-1].extend(vb.tobytes())
                ix=np.asarray(a['indices'].astype('<u4')+stream_sets[si],'<u2');sg=bytearray(104)
                # +0x1d selects the 0x134013 light-material table; zero is a
                # real index, while 0xff means the native default. Preserve it.
                sg[:48]=original_group[:48]
                if sg[29]!=255 and (0x134013 not in original_parts or sg[29]>=len(original_parts[0x134013])//8):raise ValueError('Original light material reference')
                positions=v[:,:3].view('<f4')[a['indices']];bounds=np.r_[positions.min(0),positions.max(0)] if len(positions) else m['bounds'];struct.pack_into('<6f',sg,0,*bounds)
                diffuse=texture_id[a['diffuse']];sg[24:29]=bytes([diffuse,texture_id.get(a['normal'],diffuse),diffuse,texture_id.get(a['specular'],diffuse),diffuse]);struct.pack_into('<5I',sg,48,effect,0,a['flags'],a['numVerts'],len(ix)//3);struct.pack_into('<I',sg,68,num_indices);struct.pack_into('<I',sg,92,len(ix));materials.append(sg);indices.append(ix.tobytes());num_indices+=len(ix)
            if num_vertices>65535 or num_indices//3>65535:raise ValueError('Native object header 16-bit counters')
            struct.pack_into('<HH',header,20,num_indices//3,num_vertices);desc=bytearray(original_parts.get(0x134900,native_plat)[:48]);struct.pack_into('<I',desc,16,len(materials));struct.pack_into('<I',desc,24,len(streams));struct.pack_into('<I',desc,44,num_indices)
            gap=(-(8+len(body)))%128
            if gap:
                if gap<8:gap+=128
                body.extend(chunk(0,bytes(gap-8)))
            base=8+len(body);obj=bytearray();append(obj,base+8,0x134011,bytes(header)+bank.entries[key]['metadata']['Name'].encode('ascii')+b'\0',16);append(obj,base+8,0x134012,b''.join(struct.pack('<II',h^TEXTURE_XOR,0) for h in table))
            if 0x134013 in original_parts:append(obj,base+8,0x134013,original_parts[0x134013])
            if m['markers']:append(obj,base+8,0x13401a,b''.join(bytes(x) for x in m['markers']),16)
            plat=bytearray();pb=base+8+len(obj)+8;append(plat,pb,0x134900,desc,16);append(plat,pb,0x134b02,b''.join(materials),16)
            for vb in streams:append(plat,pb,0x134b01,vb,128)
            append(plat,pb,0x134b03,b''.join(indices),16)
            for mat in m['materials']:append(plat,pb,0x134c02,mat['name'].encode('utf-8')+b'\0')
            append(obj,base+8,0x80134100,plat);body.extend(chunk(0x80134010,obj));audit.append(dict(sourceModel=key,modelAlias=f'{int(key,16)^MODEL_XOR:08x}',nativeFallback=native,sourceIRSha256=bank.entries[key]['sha256'],vertices=num_vertices,indices=num_indices,materials=len(materials),nativeStreams=len(streams)))
    payload=chunk(0x80134000,body);(output/'models.bun').write_bytes(payload);(output/'models.jsonl').write_text(''.join(json.dumps(r,separators=(',',':'))+'\n' for r in audit),encoding='utf-8');(output/'texture-closure.json').write_text(json.dumps([f'{h:08x}' for h in sorted(textures)],indent=2),encoding='utf-8');report=dict(models=len(wanted),textures=len(textures),bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest(),sourceVerticesNormalsColorsUVsTangentsPreserved=True,gameRuntimeVerified=False)
    (output/'native-model-summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
if __name__=='__main__':compile_models(*sys.argv[1:])
