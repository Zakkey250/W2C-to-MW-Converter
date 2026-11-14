"""Original W2C END native camera keys; no Carbon curve emulator is applied."""
import hashlib,math,struct
from audit_breaker_sequences import chunks

def read_native_camera(path,scene_hash,expected_sha):
    groups=[g for kind,off,b in chunks(path.read_bytes()) if kind==0x8003b200 for tag,at,g in chunks(b) if tag==0x3b210 and len(g)>=8 and struct.unpack_from('<I',g)[0]==scene_hash]
    if len(groups)!=1:raise ValueError('Native source camera group identity')
    g=groups[0]
    if hashlib.sha256(g).hexdigest()!=expected_sha or struct.unpack_from('<I',g,4)[0]!=1 or len(g)<48:raise ValueError('Native source camera fingerprint/header')
    track=bytearray(g[8:]);count=struct.unpack_from('<h',track,20)[0];start,end=struct.unpack_from('<2f',track,12)
    if not 0<count<=4096 or len(track)!=40+count*132 or not math.isfinite(start) or not math.isfinite(end) or start!=0 or not 0<end<=30:raise ValueError('Native source camera track extent/time')
    for i in range(count):
        key=track[40+132*i:40+132*(i+1)]
        # Both key variants occur in fingerprinted pristine MW InGame banks
        # (13: 466 keys, 14: 12). Preserve the native mode and all key bytes.
        if key[0] not in (13,14):raise ValueError('Unknown original native moment key type')
        floats=[struct.unpack_from('<f',key,o)[0] for o in range(16,124,4)]
        if not all(math.isfinite(x) for x in floats):raise ValueError('Nonfinite original native camera key')
    # These are three runtime links written by the native loader. Source END
    # contains transient compiler addresses; no camera/key data is changed.
    track[:12]=bytes(12)
    return track,count,end,hashlib.sha256(track[40:]).hexdigest()
