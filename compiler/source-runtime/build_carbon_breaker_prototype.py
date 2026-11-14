"""Build a private, opt-in missing-sequencer prototype. No game is edited.

Input is the user's W2C/Carbon GLOBALB, not a bundled game resource. Keep MW's
existing engines intact. ESpawnExplosion/ESpawnFragment/EPlayObjectEffect use
32-bit flags in MW, unlike Carbon's packed bytes. See the pinned reference
headers and the supported executable callbacks in the evidence directory.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from audit_breaker_sequences import chunks, sequences, entries, commands

NAMES = ('cranetrigger_seq', 'droppedfromcrane_seq', 'tollbooth_seq')
# Private experiment's verified engine-name hashes (lookup3, seed ABCDEF00).
RENAMES = {'tollbooth_seq': ('w2mw_cb_tollbooth_seq',0x919191C6,0x7AC8CCEE)}
SIZES = {0x7DF7F613:16, 0x762442F3:32, 0x66666DC8:32,
         0xDDA7829B:16, 0xF07A31AC:16}


def align(data, alignment=16, remainder=0):
    return data + bytes((remainder-len(data)) % alignment)


def adapt_command(kind, data, attributes, changes):
    # MW generated event schemas and EN1.3 callbacks: no-argument commands
    # have only alignment padding; DynamicRegion adds one int; ProcessStimulus
    # carries three UCrc32 fields. Preserve fields, reject extensions.
    if kind in (0x0713D2FA,0xDEDC722A,0xC7389F39):
        if len(data)!=16 or any(data[4:]):raise ValueError('Unexpected no-argument event extension')
        return data
    if kind==0xAA6E2CDB:
        if len(data)!=16 or struct.unpack_from('<I',data,4)[0] not in (0,1) or any(data[8:]):raise ValueError('Unexpected dynamic-region ABI')
        return data
    if kind==0xE3C86F4C:
        if len(data)!=16:raise ValueError('Unexpected process-stimulus ABI')
        return data
    if kind == 0x3338EFB8:  # EStopObjectEffect: effect identifier, MW callback 00631CC0.
        if len(data) != 16 or any(data[8:]):
            raise ValueError('Unexpected stop-object-effect ABI')
        return data
    if kind == 0x31BBBAE2:  # ESpawnExplosion: five bools -> five uint32s.
        if len(data) != 32 or any(x > 1 for x in data[20:25]) or any(data[25:]):
            raise ValueError('Unexpected Carbon explosion ABI')
        changes.append('ESpawnExplosion: flags byte[5] -> uint32[5]')
        return align(data[:20] + struct.pack('<5I', *data[20:25]))
    if kind == 0x2554C48A:  # ESpawnFragment: five MW flags, one Carbon extension.
        if len(data) != 48 or any(x > 1 for x in data[28:34]) or any(data[33:]):
            raise ValueError('Unsupported Carbon fragment extension')
        result = bytearray(data[:28] + struct.pack('<5I', *data[28:33]))
        original = struct.unpack_from('<I', result, 12)[0]
        if original and original not in attributes:
            raise ValueError(f'Unresolved child smackable {original:08X}')
        if original:
            replacement = attributes[original]
            struct.pack_into('<I', result, 12, replacement)
            if original != replacement:
                changes.append(f'child attribute {original:08X} -> {replacement:08X}')
        changes.append('ESpawnFragment: flags byte[5] -> uint32[5]')
        return bytes(result)
    if kind == 0xC1AF4FDD:  # Carbon reflection: two uint16s, MW two uint32s.
        if len(data) != 32 or any(data[24:]):
            raise ValueError('Unexpected Carbon effect ABI')
        flags = struct.unpack_from('<2H', data, 20)
        changes.append('EPlayObjectEffect: flags uint16[2] -> uint32[2]')
        return align(data[:20] + struct.pack('<2I', *flags))
    if kind not in SIZES or len(data) != SIZES[kind]:
        raise ValueError(f'Unverified command {kind:08X}/{len(data)}')
    return data


def adapt_list(raw, attributes, changes):
    count = struct.unpack_from('<I', raw)[0]
    result = bytearray(raw[:16] + bytes(count*16))
    for index in range(count):
        at = 16+16*index
        kind, size, relative, unused = struct.unpack_from('<4I', raw, at)
        payload = adapt_command(kind, raw[at+relative:at+relative+size], attributes, changes)
        result = bytearray(align(result))
        struct.pack_into('<4I', result, at, kind, len(payload), len(result)-at, unused)
        result.extend(payload)
    return bytes(result)


DEFAULT_STATE_STIMULUS=0xFA6C9D01

def adapt_sequence(record, attributes, rename=None, default_state_actions=False, scene_renames=None, group_renames=None):
    raw = record['raw']
    source_marker = raw.find(b'PRAC',0,128)
    table = record['entries']
    by_hash={r[1]:r for r in (group_renames.values() if group_renames else [rename] if rename else [])}
    background_actions=set()
    for tag,entry in table.items():
        if tag>>16!=0x6554 or not any(entry['data'][8:16]):continue
        payload=entry['data'];action,flags=struct.unpack_from('<2I',payload,8)
        if not default_state_actions:raise ValueError('Unsupported Carbon state extension')
        if flags!=1 or action not in table or action>>16!=0x6541 or struct.unpack_from('<I',payload,4)[0]:raise ValueError('Unverified default-state action/filter/flags')
        # Carbon SetState 005696C0 invokes +8 with background flag +12.
        # MW uses a private synthetic stimulus plus guarded runtime hooks to
        # start it, retain the state, and interrupt it before a real stimulus.
        # Reject sharing with an ordinary response before making duration
        # unbounded: Carbon flag 1 holds the action after its final event.
        for st,e in table.items():
            if st>>16!=0x6554:continue
            count=struct.unpack_from('<I',e['data'])[0]
            if any(struct.unpack_from('<I',e['data'],16+4*count+8*i+4)[0]==action for i in range(count)):
                raise ValueError('Default-state action is shared with an ordinary response')
        background_actions.add(action)
    # Every child header in this standalone pack is at 8 mod 16. Its data is
    # already aligned. Remove source-file-dependent 0x11 prefix padding before
    # the 16-byte bChunkCarpHeader. Keep each child size at 0 mod 16 overall.
    prefix = source_marker-16
    if prefix not in (0,8) or any(x != 0x11 for x in raw[:prefix]):
        raise ValueError('Unexpected source alignment prefix')
    marker = 16
    result = bytearray(raw[prefix:source_marker+16+16*len(table)])
    changes = []
    for index, (tag, entry) in enumerate(table.items()):
        payload = entry['data']
        entry_key=entry['key']
        if scene_renames and tag>>16==0x6353:
            text=payload.split(b'\0')[0].decode('ascii')
            if text in scene_renames:
                payload=scene_renames[text].encode('ascii')+b'\0'
                changes.append('Namespaced source camera: '+text+' -> '+scene_renames[text])
        active_rename=by_hash.get(entry_key)
        if active_rename and tag>>16 in (0x694E,0x6545):entry_key=active_rename[2]
        if active_rename and tag>>16==0x694E:
            payload=active_rename[0].encode('ascii')+b'\0'
            changes.append('Namespaced engine identity: '+active_rename[0])
        if tag >> 16 == 0x656C:
            payload = adapt_list(payload, attributes, changes)
        elif tag >> 16 == 0x6545:
            count = struct.unpack_from('<I', payload, 4)[0]
            if len(payload) != 12+count*8 or struct.unpack_from('<I',payload,8)[0] != 0x27DB:
                raise ValueError('Unexpected Carbon engine extension')
            payload = payload[:8]+payload[12:]
            changes.append('EventSeqEngine: remove Carbon-only word at +8; relocate system arrays to MW +8')
        elif tag >> 16 == 0x6554:
            count = struct.unpack_from('<I', payload)[0]
            if len(payload) != 16+count*12:raise ValueError('Unexpected Carbon state extent')
            action,flags=struct.unpack_from('<2I',payload,8)
            if action:
                stimuli=struct.unpack_from('<'+'I'*count,payload,16)
                if DEFAULT_STATE_STIMULUS in stimuli:raise ValueError('Default-state stimulus collision')
                responses=[payload[16+count*4+8*i:24+count*4+8*i] for i in range(count)]
                rows=sorted(list(zip(stimuli,responses))+[(DEFAULT_STATE_STIMULUS,struct.pack('<2I',entry['key'],action))])
                payload=struct.pack('<2I',count+1,0)+b''.join(struct.pack('<I',key) for key,value in rows)+b''.join(value for key,value in rows)
                changes.append('EventSeqState: preserve background entry action through synthetic stimulus and runtime bridge')
            else:
                payload=payload[:8]+payload[16:]
                changes.append('EventSeqState: remove zero Carbon extension; relocate stimuli/responses to MW +8')
        elif tag in background_actions:
            payload=bytearray(payload);struct.pack_into('<f',payload,12,-1.0);payload=bytes(payload)
            changes.append('Default background action: preserve one-shot timeline and hold state until interrupted')
        result = bytearray(align(result))
        at = marker+16+16*index
        struct.pack_into('<4I', result, at, tag, len(payload)*256+entry['kind'], entry_key, len(result)-at)
        result.extend(payload)
    result = bytearray(align(result,16,8))
    struct.pack_into('<I',result,marker-16,len(result)-marker)
    check = entries(result)
    for tag, value in table.items():
        if tag >> 16 not in (0x656C,0x6545,0x6554) and tag not in background_actions and not(by_hash and tag>>16==0x694E) and not(scene_renames and tag>>16==0x6353 and value['data'].split(b'\0')[0].decode('ascii') in scene_renames) and check[tag] != value:
            raise ValueError('Changed stimulus, timing, state or identity')
    commands(dict(entries=check))
    return bytes(result), sorted(set(changes))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-global',type=Path,required=True)
    parser.add_argument('--mw-track',type=Path,required=True)
    parser.add_argument('--mw-attributes',type=Path,required=True)
    parser.add_argument('--source-attributes',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--extra-sequences',nargs='+',default=[],help='Explicit extra source engines; unknown command ABIs still refuse')
    parser.add_argument('--engine-renames',type=Path,help='Explicit source/name/hash namespace plan; preserves MW engines')
    parser.add_argument('--scene-renames',type=Path,help='Explicit source camera namespaces')
    parser.add_argument('--default-state-actions',action='store_true',help='Requires the matching MW background-state runtime bridge')
    args=parser.parse_args()
    source_bytes=args.source_global.read_bytes()
    source=sequences(source_bytes)
    existing=sequences(args.mw_track.read_bytes())
    names=tuple(dict.fromkeys(NAMES+tuple(args.extra_sequences)))
    renames=dict(RENAMES)
    if args.engine_renames:
        for name,row in json.loads(args.engine_renames.read_text()).items():
            renames[name]=(row['target'],int(row['sourceHash'],16),int(row['targetHash'],16))
    mr=[r for r in json.loads(args.mw_attributes.read_text())['rows'] if r['class']=='smackable']
    sr=[r for r in json.loads(args.source_attributes.read_text())['rows'] if r['class']=='smackable']
    byname={r['name']:int(r['hash'],16) for r in mr}
    attributes={int(r['hash'],16):int(r['hash'],16) for r in mr}
    for row in sr:
        name='w2mw_pb2_'+row['name']
        if name in byname:
            attributes[int(row['hash'],16)]=byname[name]
    body=bytearray();report=[];seen=set()
    for name in names:
        identity=hashlib.sha256(source[name]['raw']).digest()
        if identity in seen:continue
        seen.add(identity)
        rename=renames.get(name)
        target=rename[0] if rename else name
        if target in existing:
            raise ValueError('Never replace an existing MW engine')
        scene_renames=json.loads(args.scene_renames.read_text()) if args.scene_renames else None
        grouped={n:renames[n] for n in source[name]['names'] if n in renames}
        raw,changes=adapt_sequence(source[name],attributes,rename,args.default_state_actions,scene_renames,grouped or None)
        body.extend(struct.pack('<II',0x3B811,len(raw))+raw)
        report.append(dict(sourceName=name,name=target,bytes=len(raw),changes=changes))
    output=struct.pack('<II',0x8003B810,len(body))+body
    readback=sequences(output)
    if set(readback)!={renames[n][0] if n in renames else n for n in names}:raise ValueError('Prototype readback identity')
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'carbon-sequences-mw.bun').write_bytes(output)
    result=dict(schema=1,privatePrototype=True,source=str(args.source_global),
                sourceSha256=hashlib.sha256(source_bytes).hexdigest(),
                bytes=len(output),sha256=hashlib.sha256(output).hexdigest(),sequences=report,
                existingMWSequencesPreserved=True,sourceTimingAndStimuliPreserved=True,
                nativeGameFilesModified=False,runtimeVerified=False,
                cutscene='WMCrane scene conversion/placement and police-context acceptance still required')
    (args.output/'build-report.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
