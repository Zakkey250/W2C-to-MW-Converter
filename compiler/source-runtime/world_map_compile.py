"""W2C world and radar share its original 2048 image, with exact BC3 tiles.

No resize, resampling, decode/re-encode, redrawing or replacement world atlas.
The native 8x8 radar tiles retain the original compressed 4x4 pixel blocks.
"""
import hashlib,json,struct,sys
from pathlib import Path
from native_models import chunk,append
from native_textures import envelope
def bh(name):
    h=0xffffffff
    for c in name.encode('ascii'):h=(h*33+c)&0xffffffff
    return h
def main(original,template,calibration,output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    if (output/'textures.bin').exists():raise ValueError('Immutable original map trial exists')
    source=Path(original).read_bytes();source_sha=hashlib.sha256(source).hexdigest();cal=json.loads(Path(calibration).read_bytes());head=envelope(Path(template).read_bytes())
    if source_sha!='ff1aea750de167ee8f8d1ad47bd904268d22cd7ef75a7d64885764392aaa8fc7' or cal['sourceSha256']!=source_sha:raise ValueError('Original W2C map identity')
    if source[:4]!=b'DDS ' or source[84:88]!=b'DXT5' or struct.unpack_from('<II',source,12)!=(2048,2048) or struct.unpack_from('<I',source,28)[0]!=1 or len(source)!=128+2048**2:raise ValueError('Original map BC3 dimensions')
    pixels_source=source[128:];textures=[('W2V2_ORIGINAL_TRACK_MAP',2048,pixels_source)];reassembled=bytearray(len(pixels_source))
    for tile in range(64):
        tx,ty=tile%8,tile//8;raw=bytearray()
        for y in range(64):
            at=((ty*64+y)*512+tx*64)*16;raw.extend(pixels_source[at:at+1024]);reassembled[at:at+1024]=raw[-1024:]
        textures.append((f'W2V2_ORIGINAL_MINIMAP_{tile}',256,bytes(raw)))
    if bytes(reassembled)!=pixels_source:raise ValueError('Native radar tiles changed original BC3 blocks')
    pack='W2CMW_V2_ORIGINAL_MAP';ph=bh(pack);info=bytearray(head[0x33310001]);info[4:32]=(pack+'\0').encode().ljust(28,b'\0');info[32:96]=(pack+'.tpk\0').encode().ljust(64,b'\0');struct.pack_into('<I',info,96,ph)
    descriptors=bytearray();formats=bytearray();pixels=bytearray();hashes=[];audit=[]
    for name,width,raw in sorted(textures,key=lambda t:bh(t[0])):
        h=bh(name);offset=len(pixels);pixels.extend(raw);pixels.extend(bytes((-len(pixels))%128));hashes.append(h);d=bytearray(head[0x33310004][:124]);d[:36]=bytes(36);d[12:36]=name.encode()[:23].ljust(24,b'\0');struct.pack_into('<8I',d,36,h,0,0,offset,0xffffffff,len(raw),0,len(raw));struct.pack_into('<HH',d,68,width,width);d[72]=d[73]=width.bit_length()-1;d[74]=0x26;d[78]=1;d[79]=0;d[81]=d[84]=d[85]=d[86]=d[87]=0;descriptors.extend(d);fmt=bytearray(head[0x33310005][:32]);struct.pack_into('<I',fmt,20,0x35545844);formats.extend(fmt);audit.append(dict(name=name,hash=f'{h:08x}',width=width,height=width,mips=1,pixelSha256=hashlib.sha256(raw).hexdigest(),pixelBytes=len(raw)))
    header=chunk(0x33310001,info)+chunk(0x33310002,b''.join(struct.pack('<II',h,0) for h in hashes))+chunk(0x33310004,descriptors)+chunk(0x33310005,formats);body=bytearray(chunk(0,bytes(48))+chunk(0xb3310000,header));gap=(-(8+len(body)))%128
    if gap:
        if gap<8:gap+=128
        body.extend(chunk(0,bytes(gap-8)))
    data=bytearray(chunk(0x33320001,struct.pack('<6I',0,0,1,ph,0,0)));base=8+len(body)+8;gap=(-(base+len(data)))%128
    if gap<8:gap+=128
    data.extend(chunk(0,bytes(gap-8)));append(data,base,0x33320002,pixels,128);body.extend(chunk(0xb3320000,data));payload=chunk(0xb3300000,body);(output/'textures.bin').write_bytes(payload)
    aliases={}
    for unlock in ('','_UNLOCK_1','_UNLOCK_2'):
        aliases['TRACK_MAP'+unlock]='W2V2_ORIGINAL_TRACK_MAP'
        for i in range(64):aliases[f'MINI_MAP{unlock}_CHOP{i}']=f'W2V2_ORIGINAL_MINIMAP_{i}'
    # The retained original TexWizard can already have translated native keys.
    # Intercept its previous World aliases in our own provider; do not edit it.
    for source_name,target_name in list(aliases.items()):aliases['_'+source_name]=target_name
    result=dict(source=str(original),sourceSha256=source_sha,nativeTextures=65,fullImageAnd64RadarTilesSameOriginal=True,sourceCompressedBlocksExact=True,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest(),gameCalibrationAccepted=False)
    (output/'map-aliases.json').write_text(json.dumps({f'{bh(k):08x}':f'{bh(v):08x}' for k,v in aliases.items()},indent=2),encoding='utf-8');(output/'textures.jsonl').write_text(''.join(json.dumps(r,separators=(',',':'))+'\n' for r in audit),encoding='utf-8');(output/'world-map-summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main(*sys.argv[1:])
