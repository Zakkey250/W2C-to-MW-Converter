"""Native MW texture descriptors around original highest-quality DDS pixels."""
import hashlib,json,struct,sys
from pathlib import Path
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'source-world'))
from source_ir import Container,chunks
from native_models import TEXTURE_XOR,chunk,append

def envelope(data,start=0,end=None):
    for kind,off,a,b in chunks(data,start,end):
        if kind==0xb3310000:
            result={k:bytes(data[s:e]) for k,o,s,e in chunks(data,a,b) if k}
            if all(len(result.get(k,b''))>=n for k,n in ((0x33310001,104),(0x33310004,124),(0x33310005,32))):return result
        if kind==0xb3300000:
            value=envelope(data,a,b)
            if value:return value
    return None

def original_native_type(bank,key):
    row=bank.entries[key];origin=row['metadata']['origin'];summary=json.loads((bank.path.parent/'import-summary.json').read_text());source=next(r for r in summary['sources'] if r['role']==origin['role']);path=Path(source['path'])
    with path.open('rb') as f:
        f.seek(origin['offset']);kind,n=struct.unpack('<II',f.read(8))
        if kind!=0xb3300000 or n>64*1024**2:raise ValueError('Original native TPK header')
        raw=struct.pack('<II',kind,n)+f.read(n)
    value=envelope(raw)
    if value is None:raise ValueError('Original native texture definitions missing')
    table=value[0x33310004]
    if len(table)%124:raise ValueError('Original MW texture descriptor stride')
    for at in range(0,len(table),124):
        h,t=struct.unpack_from('<II',table,at+36)
        if h==int(key,16):return t
    raise ValueError('Native fallback texture type identity missing')

def main(source,target,native_models,original_template,output,identity_xor=TEXTURE_XOR,packname='W2CMW_V2_NATIVE_PROPS',*,supplement=None):
    source=Container(source,'textures','textures-verified');target=Container(target,'textures');output=Path(output);output.mkdir(parents=True,exist_ok=True)
    supplement=Container(supplement,'textures') if supplement else None
    if (output/'textures.bin').exists():raise ValueError('Immutable native texture trial exists')
    wanted=json.loads((Path(native_models)/'texture-closure.json').read_text());template=envelope(Path(original_template).read_bytes())
    if template is None:raise ValueError('Original MW texture envelope')
    h=0xffffffff
    for c in packname.encode('ascii'):h=(h*33+c)&0xffffffff
    info=bytearray(template[0x33310001]);info[4:32]=(packname+'\0').encode('ascii').ljust(28,b'\0');info[32:96]=(packname+'.tpk\0').encode('ascii').ljust(64,b'\0');struct.pack_into('<I',info,96,h)
    descriptions=bytearray();formats=bytearray();pixels=bytearray();hashes=[];audit=[]
    all_hashes={int(k,16) for bank in (source,target,supplement) if bank for k in bank.entries if '/' not in k}
    for key in sorted(wanted,key=lambda k:int(k,16)^identity_xor):
        bank=source if key in source.entries else target if key in target.entries or not supplement else supplement;native=bank is target;data=bytes(bank.get(key));row=bank.entries[key]['metadata']['texture'];alias=int(key,16)^identity_xor
        if 'TypeHash' not in row:row=dict(row,TypeHash=original_native_type(bank,key))
        if identity_xor and alias in all_hashes:raise ValueError('Private texture identity overlaps original bank')
        if data[:4]!=b'DDS ' or struct.unpack_from('<I',data,4)[0]!=124:raise ValueError('Validated source DDS signature')
        height,width=struct.unpack_from('<II',data,12);mips=max(1,struct.unpack_from('<I',data,28)[0]);fmt,bits=struct.unpack_from('<II',data,84);raw=data[128:]
        if (width,height,mips)!=(row['Width'],row['Height'],row['MipMapCount']):raise ValueError('Source DDS metadata mismatch')
        if fmt not in (0x31545844,0x33545844,0x35545844) and not(fmt==0 and bits==32):raise ValueError('Unsupported native source pixel format')
        stride=8 if fmt==0x31545844 else 16;pitch=width*height*4 if fmt==0 else max(1,(width+3)//4)*max(1,(height+3)//4)*stride;expected=sum(max(1,width>>i)*max(1,height>>i)*4 if fmt==0 else max(1,((width>>i)+3)//4)*max(1,((height>>i)+3)//4)*stride for i in range(mips))
        if not expected<=len(raw)<expected+16:raise ValueError('Original declared mip chain dimensions')
        offset=len(pixels);pixels.extend(raw);pixels.extend(bytes((-len(pixels))%128));hashes.append(alias);d=bytearray(template[0x33310004][:124]);d[:36]=bytes(36);d[12:36]=row['Name'].encode('ascii')[:23].ljust(24,b'\0');struct.pack_into('<8I',d,36,alias,row['TypeHash'],0,offset,0xffffffff,len(raw),0,pitch);struct.pack_into('<HH',d,68,width,height);d[72]=width.bit_length()-1;d[73]=height.bit_length()-1;d[74]={0:0x20,0x31545844:0x22,0x33545844:0x24,0x35545844:0x26}[fmt];d[78]=mips;d[79]=row['TilableUV'];d[81]=0;d[84]=int(row['AlphaBlendType']!=0);d[85]=row['AlphaUsageType'];d[86]=row['AlphaBlendType'];d[87]=0 if key=='3b5afb3c' and row['Name']=='OBJ_TRASHCAN_JF' and row['RenderFlags']==3 else row['RenderFlags'];descriptions.extend(d);dd=bytearray(template[0x33310005][:32]);struct.pack_into('<I',dd,20,fmt if fmt else 21);formats.extend(dd)
        audit.append(dict(sourceHash=key,alias=f'{alias:08x}',nativeFallback=native,width=width,height=height,mips=mips,sourceDdsSha256=bank.entries[key]['sha256'],pixelSha256=hashlib.sha256(raw).hexdigest(),pixelBytes=len(raw),paletteExpanded=fmt==0 and row['Format']==41,format=fmt if fmt else 21))
    header=chunk(0x33310001,info)+chunk(0x33310002,b''.join(struct.pack('<II',v,0) for v in hashes))+chunk(0x33310004,descriptions)+chunk(0x33310005,formats);body=bytearray(chunk(0,bytes(48))+chunk(0xb3310000,header));gap=(-(8+len(body)))%128
    if gap:
        if gap<8:gap+=128
        body.extend(chunk(0,bytes(gap-8)))
    data=bytearray(chunk(0x33320001,struct.pack('<6I',0,0,1,h,0,0)));base=8+len(body)+8;gap=(-(base+len(data)))%128
    if gap<8:gap+=128
    data.extend(chunk(0,bytes(gap-8)));append(data,base,0x33320002,pixels,128);body.extend(chunk(0xb3320000,data));payload=chunk(0xb3300000,body)
    if len(payload)>64*1024**2:raise ValueError('Native texture pack asset budget')
    (output/'textures.bin').write_bytes(payload);(output/'textures.jsonl').write_text(''.join(json.dumps(r,separators=(',',':'))+'\n' for r in audit),encoding='utf-8');result=dict(textures=len(audit),bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest(),sourceMipPixelsPreserved=True,nativeFallbacks=sum(r['nativeFallback'] for r in audit),gameVerified=False);(output/'native-texture-summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
    source.close();target.close()
    if supplement:supplement.close()
if __name__=='__main__':main(*sys.argv[1:])
