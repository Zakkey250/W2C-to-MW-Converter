"""Bind complete independent texture headers without duplicating pixel blobs.

The importer index remains immutable. A new verified metadata view shares its
read-only pixel container and has its own fingerprint and validation receipt.
"""
import hashlib,json,sys
from pathlib import Path
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'compiler/source-world'))
from source_ir import Container
E=P/'evidence/source-world-20261110';W=Path(r'E:\NFSMW-Labs\W2CMW-v2\SourceWorld\complete-trial')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    result=enrich(W,E/'independent-texture-source-index.jsonl')
    (E/'texture-metadata-verification.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(dict(passed=True,records=result['records'],checked=result['independentMetadataRecords'],metadataCorrected=len(result['correctedMetadataRecords']),pixelBlobsCopied=0)))

def enrich(source_root,index):
    independent={}
    for line in index.read_text(encoding='utf-8').splitlines():
        s=json.loads(line);independent[(s['role'],s['packOffset'],s['hash'])]=s
    changed=[];checked=0;destination=source_root/'textures-verified.jsonl'
    if destination.exists():raise ValueError('Immutable verified texture index exists')
    pending=destination.with_suffix('.pending')
    with pending.open('x',encoding='utf-8') as out:
        for line in (source_root/'textures.jsonl').read_text(encoding='utf-8').splitlines():
            r=json.loads(line);t=r['metadata']['texture'];origin=r['metadata']['origin'];s=independent.get((origin['role'],origin['offset'],t['hash']))
            if s:
                assert t['sourceDataSha256']==s['sourceDataSha256'] and t['sourcePaletteSha256']==s['paletteSha256'];m=s['renderMetadata'];raw=bytes.fromhex(s['sourceHeaderHex'])
                fields=dict(Name=s['name'],TypeHash=int(m['typeHash'],16),Width=s['width'],Height=s['height'],MipMapCount=s['mips'],Format=s['format'],compression=s['compression'],AlphaUsageType=m['alphaUsage'],AlphaBlendType=m['alphaBlend'],RenderFlags=m['flags'],TilableUV=raw[51 if s['version']==8 else 79],ScrollType=m['scroll'],ScrollTimeStep=m['step'],ScrollSpeedS=m['speed'][0],ScrollSpeedT=m['speed'][1],OffsetS=m['offset'][0],OffsetT=m['offset'][1],ScaleS=m['scale'][0],ScaleT=m['scale'][1],sourceHeaderHex=s['sourceHeaderHex'])
                differences=[key for key,value in fields.items() if t[key]!=value]
                if differences:changed.append(dict(key=r['key'],sourceVersion=s['version'],fields=differences))
                t.update(fields);checked+=1
            out.write(json.dumps(r,separators=(',',':'))+'\n')
    pending.replace(destination)
    view=Container(source_root,'textures',index_name='textures-verified')
    result=dict(passed=True,sourceIndexSha256=sha(source_root/'textures.jsonl'),verifiedIndexSha256=sha(destination),payloadFileUnchanged=True,payloadFile=str(source_root/'textures.sourceir'),records=len(view.entries),independentMetadataRecords=checked,correctedMetadataRecords=changed,compressedTpkMetadataIndependentOraclePending=True)
    view.close()
    return result
if __name__=='__main__':main()
