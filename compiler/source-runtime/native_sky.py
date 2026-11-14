"""Retain pristine MW's exact native sky bank after scenery suppression."""
import hashlib,json,struct,sys
from pathlib import Path
def sha(b):return hashlib.sha256(b).hexdigest()
def main(work,original_stream,output):
    work=Path(work);original_stream=Path(original_stream);output=Path(output)
    if output.exists():raise ValueError('Immutable native sky output exists')
    original=(work/'offline-original-references/mw-pristine/GLOBAL/InGameA.bun').read_bytes()
    result,sky=append_sky(original,original_stream)
    output.mkdir(parents=True);(output/'InGameA.bun').write_bytes(result);(output/'original-sky-bank.bun').write_bytes(sky)
    report=dict(passed=True,pristineStream=str(original_stream),pristineStreamSha256='15016611bcb7511d255c092321f87c659b101910c054aba8b750d722036c111a',nativeBankOffset=18686976,nativeBankBytes=len(sky),nativeBankSha256=sha(sky),sourceNativeSkyModel='ed85ba82',sourceNormalizedModelSha256='2fd80b3be05377ae6b2c443c3442438148277bb5af9ad66bed685e2d517358c6',originalInGameASha256=sha(original),outputBytes=len(result),outputSha256=sha(result),originalBankAndInGamePrefixByteExact=True,gameAccepted=False)
    (output/'build-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))

def append_sky(original,original_stream,*,target_ingamea=False):
    with original_stream.open('rb') as f:
        if hashlib.file_digest(f,'sha256').hexdigest()!='15016611bcb7511d255c092321f87c659b101910c054aba8b750d722036c111a':raise ValueError('Pristine MW stream identity')
        f.seek(18686976);head=f.read(8);tag,n=struct.unpack('<II',head)
        if tag!=0x80134000 or n!=20424:raise ValueError('Pristine native sky bank extent')
        sky=head+f.read(n)
    if sha(sky)!='5ca71209a935c1db0b2253674dcd83332679c213bf50299125e8d8a2cf846cd5':raise ValueError('Original MW sky bank fingerprint')
    if not target_ingamea and sha(original)!='f65f05ef2ccea60a284af6c26923e55a95161f9ace7390dbbf237b964950b51d':raise ValueError('Original MW InGameA identity')
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
