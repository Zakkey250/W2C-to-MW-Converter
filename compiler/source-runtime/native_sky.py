"""Retain pristine MW's exact native sky bank after scenery suppression."""
import hashlib,json,mmap,struct,sys
from pathlib import Path
from toll_compile import word, aligned
from source_ir import chunks
def sha(b):return hashlib.sha256(b).hexdigest()
def main(work,original_stream,output):
    work=Path(work);original_stream=Path(original_stream);output=Path(output)
    if output.exists():raise ValueError('Immutable native sky output exists')
    original=(work/'offline-original-references/mw-pristine/GLOBAL/InGameA.bun').read_bytes()
    result,sky=append_sky(original,original_stream)
    output.mkdir(parents=True);(output/'InGameA.bun').write_bytes(result);(output/'original-sky-bank.bun').write_bytes(sky)
    with original_stream.open('rb') as stream:
        source_sha=hashlib.file_digest(stream,'sha256').hexdigest()
    report=dict(passed=True,pristineStream=str(original_stream),pristineStreamSha256=source_sha,nativeBankBytes=len(sky),nativeBankSha256=sha(sky),sourceNativeSkyModel='ed85ba82',originalInGameASha256=sha(original),outputBytes=len(result),outputSha256=sha(result),originalBankAndInGamePrefixByteExact=True,gameAccepted=False)
    (output/'build-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))

def find_sky(original_stream):
    with original_stream.open('rb') as f, mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
        for tag,offset,start,end in chunks(data):
            if tag!=0x80134000 or offset%128 or end-offset>1024*1024:
                continue
            for kind,_,a,b in chunks(data,start,end):
                if kind!=0x80134010:
                    continue
                for leaf,_,begin,stop in chunks(data,a,b):
                    key=aligned(begin,16)+16
                    if leaf==0x134011 and key+4<=stop and word(data,key)==0xed85ba82:
                        return data[offset:end]
    raise ValueError('Could not extract the MW sky model from STREAML2RA.BUN.')


def append_sky(original,original_stream):
    sky=find_sky(original_stream)
    end=0
    while end<len(original):
        if end+8>len(original):raise ValueError('MW InGameA chunk header')
        _,size=struct.unpack_from('<II',original,end)
        end+=8+size
    if end!=len(original) or not original:raise ValueError('MW InGameA chunk extent')
    prefix_padding=(-end)%128
    if prefix_padding and prefix_padding<8:prefix_padding+=128
    if prefix_padding:original+=struct.pack('<II',0,prefix_padding-8)+bytes(prefix_padding-8)
    padding=(-len(sky))%128
    if padding and padding<8:padding+=128
    result=original+sky+(struct.pack('<II',0,padding-8)+bytes(padding-8) if padding else b'')
    return result,sky
if __name__=='__main__':main(*sys.argv[1:])
