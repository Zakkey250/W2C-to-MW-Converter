"""Independent original TPK/DDS block, mip and render-metadata verification."""
import hashlib,json,mmap,struct,sys
from collections import Counter,defaultdict
from pathlib import Path
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'compiler/source-world'))
from source_ir import chunks,Container
from inspect_raw_models import leaves
E=P/'evidence/source-world-20261110';W=Path(r'E:\NFSMW-Labs\W2CMW-v2\SourceWorld\complete-trial')

def main():
    cfg=json.loads((E/'source-complete-config.json').read_text(encoding='utf-8-sig'))
    result=verify(cfg['files'],W,E/'independent-texture-source-index.jsonl')
    (E/'independent-texture-verification.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))

def verify(inputs,source_root,index):
    bank=Container(source_root,'textures');byhash=defaultdict(list);statistics=Counter();compressed=[]
    for key,r in bank.entries.items():byhash[r['metadata']['texture']['hash']].append(r)
    with index.open('w',encoding='utf-8') as out:
        for source in inputs:
            path=Path(source['path'])
            with path.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
                for kind,top,start,end in chunks(data):
                    if kind!=0xb3300000:continue
                    leaf=list(leaves(data,start,end));version=next(struct.unpack_from('<I',data,a)[0] for k,o,a,b in leaf if k==0x33310001)
                    desc=next(((a,b) for k,o,a,b in leaf if k==0x33310004),None)
                    if desc is None:
                        compressed.append(dict(role=source['role'],offset=top,version=version,reason='CIP source payload requires independent decompression oracle'))
                        continue
                    body=next((a,b) for k,o,a,b in leaf if k==0x33320002);base=body[0]+120
                    fmt=next((a,b) for k,o,a,b in leaf if k==0x33310005);at=desc[0];ordinal=0
                    while at<desc[1]:
                        if version==8:
                            count=89+data[at+88];key,typ,offset,paloff,n,paln,base_size=struct.unpack_from('<7I',data,at+12);width,height=struct.unpack_from('<HH',data,at+40);mips=data[at+50];compression=data[at+46];scroll=data[at+54];usage,blend,flags=struct.unpack_from('<3B',data,at+57);step,speed_s,speed_t,off_s,off_t,scale_s,scale_t=struct.unpack_from('<7h',data,at+60);format_=struct.unpack_from('<I',data,fmt[0]+ordinal*24+12)[0]
                            name=data[at+89:at+count].rstrip(b'\0').decode('ascii')
                        elif version==5:
                            count=124;key,typ,parent,offset,paloff,n,paln,base_size=struct.unpack_from('<8I',data,at+36);width,height=struct.unpack_from('<HH',data,at+68);mips=data[at+78];compression=data[at+74];scroll=data[at+82];usage,blend,flags=struct.unpack_from('<3B',data,at+85);step,speed_s,speed_t,off_s,off_t,scale_s,scale_t=struct.unpack_from('<7h',data,at+88);format_=struct.unpack_from('<I',data,fmt[0]+ordinal*32+20)[0];name=data[at+12:at+36].split(b'\0')[0].decode('ascii')
                        else:raise ValueError('Unsupported independent TPK version')
                        if at+count>desc[1] or base+offset+n>body[1] or not width or not height or width>8192 or height>8192:raise ValueError('Independent original texture bounds')
                        raw=data[base+offset:base+offset+n];palette=None
                        if format_==0x29:
                            assert paln==1024 and base+paloff+paln<=body[1];palette=data[base+paloff:base+paloff+paln];colors=[palette[i:i+4] for i in range(0,1024,4)];expected=b''.join(colors[x] for x in raw)
                        else:expected=raw
                        candidates=[]
                        for r in byhash[f'{key:08x}']:
                            dds=bank.get(r['key']);h,w=struct.unpack_from('<II',dds,12);m=struct.unpack_from('<I',dds,28)[0]
                            if (w,h,m)==(width,height,mips) and bytes(dds[128:])==expected:candidates.append(r)
                            del dds
                        assert candidates,f'Original compressed blocks/mips differ {source["role"]}:{key:08x}'
                        source_raw_header=data[at:at+count]
                        # Namespaces are preserved even when DDS bytes alias.
                        out.write(json.dumps(dict(role=source['role'],packOffset=top,headerOffset=at,hash=f'{key:08x}',name=name,version=version,width=width,height=height,mips=mips,format=format_,compression=compression,sourceHeaderHex=source_raw_header.hex(),sourcePixelOffset=base+offset,sourcePixelBytes=n,sourceDataSha256=hashlib.sha256(raw).hexdigest(),paletteSha256=hashlib.sha256(palette).hexdigest() if palette else None,renderMetadata=dict(typeHash=f'{typ:08x}',alphaUsage=usage,alphaBlend=blend,flags=flags,scroll=scroll,step=step,speed=[speed_s,speed_t],offset=[off_s,off_t],scale=[scale_s,scale_t]),matchingPayloadKeys=[r['key'] for r in candidates]),separators=(',',':'))+'\n')
                        statistics['textures']+=1;statistics['sourceBytesIncludingAllMips']+=n;statistics['sourceMipLevels']+=max(1,mips);statistics['palettedTextures']+=int(palette is not None);statistics['version'+str(version)]+=1
                        ordinal+=1;at+=count
                    assert at==desc[1]
    result=dict(stage='Independent original uncompressed TPK payload verification',passed=True,statistics=dict(statistics),allOriginalBlocksAndMipBytesEqual=True,sourceRenderAndScrollMetadataRetained=True,compressedPacksRequiringIndependentOracle=compressed,completeRuntimeConversion=False)
    bank.close()
    return result
if __name__=='__main__':main()
