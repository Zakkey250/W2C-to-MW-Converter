"""Private camera/marker conversion experiment; never edits a game directory.

The executable is a research-only decoder oracle, not a redistributable asset.
Converted MW camera keys and track markers are the only generated runtime data.
"""
import argparse, hashlib, json, math, struct
from pathlib import Path
from audit_breaker_sequences import chunks
from native_camera import read_native_camera


def bhash(text):
    result=0xffffffff
    for char in text:result=(result*33+ord(char))&0xffffffff
    return result


def camera_key(first, last, parameter):
    result=bytearray(132)
    value=lambda f,n:f['fields'][n]['value']
    integer=lambda f,n:f['fields'][n]['bits']
    # MW's native moment camera type; linear segments approximate the source
    # cubic paths within a separately measured numerical error bound.
    # Match the entry flag in MW's native moment keys.
    result[0]=13;result[1]=0
    for offset,name in [(4,'SPACE_EYE'),(5,'SPACE_LOOK'),(6,'CONSTRAIN_TO_CARS'),
                        (7,'CONSTRAIN_TO_WORLD'),(9,'CAR_SPACE_LAG'),(10,'OVERLAY')]:
        a,b=integer(first,name),integer(last,name)
        if a!=b or not 0<=a<=255:raise ValueError('Unrepresented camera flag transition: '+name)
        result[offset]=a
    struct.pack_into('<I',result,12,integer(first,'SHAKE_TYPE'))
    struct.pack_into('<f',result,16,parameter)
    struct.pack_into('<2f',result,20,1,1)
    for offset,names in [(28,['EYE_X','EYE_Y','EYE_Z']), (52,['LOOK_X','LOOK_Y','LOOK_Z'])]:
        # EN1.3's 0046D0A0 addresses endpoint i as key+offset+i*12:
        # two contiguous XYZ vectors, despite the reconstructed header's
        # misleading float[3][2] declaration. Interleaving components made
        # every resampled key jump through the sign/building.
        struct.pack_into('<6f',result,offset,*(value(first,n) for n in names),*(value(last,n) for n in names))
    for offset,name in [(76,'DUTCH'),(84,'LENS_LENGTH'),(92,'NEAR_CLIP'),
                         (100,'SHAKE_AMPLITUDE'),(108,'SHAKE_FREQUENCY'),(116,'FOCAL_DISTANCE')]:
        struct.pack_into('<2f',result,offset,value(first,name),value(last,name))
    for offset,name in [(124,'APERTURE'),(126,'LETTERBOX'),(128,'TIME_SCALE')]:
        values=[integer(first,name),integer(last,name)]
        if any(v>255 for v in values):raise ValueError('Unrepresented camera byte: '+name)
        result[offset:offset+2]=bytes(values)
    for name in ['AVATAR_EYE','AVATAR_LOOK','CAR_SPACE_BUNGEE','RAWFOCUS_OVERRIDE',
                 'SHAKE_QUAT_X','SHAKE_QUAT_Y','SHAKE_QUAT_Z','SHAKE_POS_X','SHAKE_POS_Y','SHAKE_POS_Z']:
        if integer(first,name) or integer(last,name):raise ValueError('Unrepresented Carbon camera feature: '+name)
    return result


def compile_camera_assets(executable, ingame, track_path, scene_plan):
    from curve_oracle import evaluate

    camera_body=bytearray();marker_body=bytearray();report=[]
    marker_data=[data for tag,_,data in chunks(track_path.read_bytes()) if tag==0x34146]
    if len(marker_data)!=1 or marker_data[0][:8]!=bytes([0x11])*8:raise ValueError('Unexpected marker bank')
    source_markers=marker_data[0][8:]
    if len(source_markers)%48:raise ValueError('Marker stride')
    for plan in scene_plan:
        scene,marker_name,target=plan['source'],plan['marker'],plan['target']
        encoding=plan.get('sourceEncoding','carbon-ice');native_key_sha=None
        if encoding=='mw-native-keys':
            track,segments,length,native_key_sha=read_native_camera(ingame,bhash(scene),plan['sourceGroupSha256']);position_error=lens_error=0
        elif encoding=='carbon-ice':
            for segments in [48,96,192,384,768]:
                sampled=evaluate(executable,ingame,scene,[n/(2*segments) for n in range(2*segments+1)])
                frames=sampled['frames'];length=sampled['length']
                if not 0<length<=10:raise ValueError('Unexpected moment length')
                position_error=lens_error=0
                for index in range(1,2*segments,2):
                    for name in ['EYE_X','EYE_Y','EYE_Z','LOOK_X','LOOK_Y','LOOK_Z','LENS_LENGTH']:
                        interpolated=(frames[index-1]['fields'][name]['value']+frames[index+1]['fields'][name]['value'])/2
                        error=abs(frames[index]['fields'][name]['value']-interpolated)
                        if name=='LENS_LENGTH':lens_error=max(lens_error,error)
                        else:position_error=max(position_error,error)
                if position_error<=.01 and lens_error<=.02:break
            else:raise ValueError(f'Resampling error too large: {scene}/{position_error}/{lens_error}')
            track=bytearray(40);struct.pack_into('<2f',track,12,0,length);struct.pack_into('<h',track,20,segments);track[23:30]=b'Track 0'
            for n in range(segments):track.extend(camera_key(frames[2*n],frames[2*n+2],n/segments))
        else:raise ValueError('Unsupported original camera encoding')
        group=struct.pack('<2I',bhash(target),1)+track
        camera_body.extend(struct.pack('<2I',0x3B210,len(group))+group)
        found=0
        for index in range(0,len(source_markers),48):
            original=source_markers[index:index+48]
            if struct.unpack_from('<I',original,8)[0]!=bhash(marker_name):continue
            record=bytearray(original);record[:8]=bytes(8)
            x,y,z=struct.unpack_from('<3f',record,16)
            # Existing World2MW MapOffset(-6016,-546,0), in render axes.
            struct.pack_into('<3f',record,16,x-6016,y+546,z)
            marker_body.extend(struct.pack('<I',bhash(target))+record);found+=1
        if not found and not plan.get('unplaced',False):raise ValueError('Missing source marker: '+scene)
        report.append(dict(scene=target,sourceScene=scene,sourceEncoding=encoding,sourceNativeKeySha256=native_key_sha,markers=found,length=length,segments=segments,maxPositionAxisErrorMetres=position_error,maxLensError=lens_error))
    cameras=struct.pack('<2I',0x8003B200,len(camera_body))+camera_body
    markers=struct.pack('<4I',0x314D4243,1,len(marker_body)//52,52)+marker_body
    return cameras, markers, report


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--executable',type=Path);parser.add_argument('--ingame',type=Path,required=True);parser.add_argument('--track',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--scene-plan',type=Path,help='Explicit list of source, target and marker names; no missing source substitution')
    args=parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):raise ValueError('Immutable source camera output exists')
    scene_plan=json.loads(args.scene_plan.read_text()) if args.scene_plan else [dict(source=s,target=s,marker=m) for s,m in [('WMCrane','CraneMoment'),('WMLuckystrike','LuckystrikeMoment')]]
    cameras,markers,report=compile_camera_assets(args.executable,args.ingame,args.track,scene_plan)
    args.output.mkdir(parents=True,exist_ok=True)
    for name,data in [('carbon-moment-cameras.bun',cameras),('carbon-moment-markers.bin',markers)]:
        (args.output/name).write_bytes(data)
    result=dict(privatePrototype=True,scenes=report,files=[dict(path=n,bytes=len(d),sha256=hashlib.sha256(d).hexdigest()) for n,d in [('carbon-moment-cameras.bun',cameras),('carbon-moment-markers.bin',markers)]],sourceExecutableSha256=hashlib.sha256(args.executable.read_bytes()).hexdigest() if args.executable else None,sourceIngameSha256=hashlib.sha256(args.ingame.read_bytes()).hexdigest(),sourceTrackSha256=hashlib.sha256(args.track.read_bytes()).hexdigest(),nativeFilesModified=False,runtimeVerified=False)
    (args.output/'moment-build-report.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
