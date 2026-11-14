"""Four-file source-generation envelope; bounded assets and independent blocks.

This is a new contract. Accepted V1/V2 pack files and their compiler are not
modified. Large world-data files are never read or decompressed as one object.
"""
from dataclasses import dataclass
import hashlib,json,os,struct,zlib
from pathlib import Path

HEADER=256
ASSET_STRIDE=128
BLOCK_STRIDE=64
DEP_STRIDE=40
BLOCK_LIMIT=1024**2
ASSET_LIMIT=64*1024**2
INDEX_LIMIT=64*1024**2
MAX_ASSETS=131072
MAX_BLOCKS=524288
MAX_META=65536
MAX_TOTAL=80*1024**3
NAMES={1:'L5RA.w2i',2:'STREAML5RA.w2d',3:'L5RA.w2m'}
MAGIC={1:b'W2SRCI1\0',2:b'W2SRCD1\0',3:b'W2SRCM1\0'}

def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False,allow_nan=False).encode('utf-8')
def sha(data):return hashlib.sha256(data).digest()
def crc(data):return zlib.crc32(data)&0xffffffff
def asset_id(source_id,kind,key):
    if len(source_id)!=32 or not 0<kind<2**32 or not key or len(key.encode('utf-8'))>4096:raise ValueError('Asset identity')
    return sha(b'W2CMW.SourceAsset.r1\0'+source_id+struct.pack('<I',kind)+key.encode('utf-8'))

@dataclass(frozen=True)
class Slice:
    path:Path
    offset:int
    length:int
    expected_sha256:str|None=None
    def pieces(self):
        if self.path.is_symlink() or self.path.is_junction() or self.offset<0 or self.length<0 or self.length>ASSET_LIMIT or self.offset+self.length>self.path.stat().st_size:raise ValueError('Source payload range/link')
        with self.path.open('rb') as f:
            f.seek(self.offset);left=self.length
            while left:
                b=f.read(min(left,BLOCK_LIMIT))
                if not b:raise ValueError('Source payload truncated')
                left-=len(b);yield b

def envelope(role,file_bytes,build,source,compiler,*,assets=0,blocks=0,asset_at=0,block_at=0,meta_at=0,meta_bytes=0,dep_at=0,deps=0,payload_hash=bytes(32),payload_crc=0):
    b=bytearray(HEADER);b[:8]=MAGIC[role]
    struct.pack_into('<HH5I',b,8,1,0,HEADER,role,3,ASSET_STRIDE,BLOCK_STRIDE)
    struct.pack_into('<QQIIQQQQII',b,32,file_bytes,asset_at,assets,blocks,block_at,meta_at,meta_bytes,dep_at,deps,DEP_STRIDE)
    b[96:128]=build;b[128:160]=source;b[160:192]=compiler;b[192:224]=payload_hash
    struct.pack_into('<6I',b,224,0,payload_crc,BLOCK_LIMIT,0,1,1)
    struct.pack_into('<I',b,224,crc(b));return bytes(b)

class Writer:
    def __init__(self,root,source_manifest,compiler_fingerprint,world_definition):
        self.root=Path(root)
        if self.root.exists() and any(self.root.iterdir()):raise ValueError('Immutable bundle trial exists')
        self.root.mkdir(parents=True,exist_ok=True)
        self.source_manifest=source_manifest;self.source=sha(b'W2CMW.Source.r1\0'+canonical(source_manifest));self.compiler=bytes.fromhex(compiler_fingerprint)
        if len(self.compiler)!=32:raise ValueError('Compiler fingerprint')
        self.world_definition=world_definition;self.assets=[];self.blocks=[];self.ids=set();self.keys=set();self.files={};self.digests={}
        for role in (2,3):
            f=(self.root/NAMES[role]).open('xb');f.write(bytes(HEADER));self.files[role]=f;self.digests[role]=hashlib.sha256()
        self.finished=False
    def add(self,kind,key,payload=b'',metadata=None,*,count=1,stride=0,dependencies=(),role=2):
        if self.finished or role not in (2,3) or not 0<=count<=0xffffffff or not 0<=stride<=0xffffffff:raise ValueError('Bundle asset fields')
        identity=asset_id(self.source,kind,key)
        if identity in self.ids or key in self.keys:raise ValueError('Duplicate asset identity/key')
        if len(self.assets)>=MAX_ASSETS or len(dependencies)>1024:raise ValueError('Asset/dependency cap')
        details=dict(metadata or {})
        if 'sourceKey' in details and details['sourceKey']!=key:raise ValueError('Source key metadata mismatch')
        details['sourceKey']=key
        meta=canonical(details)
        if len(meta)>MAX_META:raise ValueError('Asset metadata cap')
        pieces=payload.pieces() if isinstance(payload,Slice) else (memoryview(payload)[i:i+BLOCK_LIMIT] for i in range(0,len(payload),BLOCK_LIMIT))
        first=len(self.blocks);length=0;digest=hashlib.sha256();f=self.files[role]
        for piece in pieces:
            data=bytes(piece);length+=len(data)
            if not data or len(data)>BLOCK_LIMIT or length>ASSET_LIMIT or len(self.blocks)>=MAX_BLOCKS:raise ValueError('Bounded asset/block')
            digest.update(data);padding=(-f.tell())%64
            if padding:f.write(bytes(padding));self.digests[role].update(bytes(padding))
            offset=f.tell();f.write(data);self.digests[role].update(data)
            if f.tell()>MAX_TOTAL:raise ValueError('World volume budget')
            self.blocks.append((offset,len(data),len(data),0,role,0,crc(data),crc(data),sha(data)))
        actual=digest.digest()
        if isinstance(payload,Slice) and payload.expected_sha256 is not None and actual.hex()!=payload.expected_sha256:raise ValueError('Source fingerprint changed')
        refs=[]
        for dep_role,dep_kind,dep_key in dependencies:
            if not 0<dep_role<=0xffffffff:raise ValueError('Dependency role')
            refs.append((dep_role,dep_kind,asset_id(self.source,dep_kind,dep_key)))
        self.ids.add(identity);self.keys.add(key)
        self.assets.append(dict(id=identity,kind=kind,key=key,bytes=length,first=first,blocks=len(self.blocks)-first,count=count,stride=stride,meta=meta,deps=refs,sha=actual))
        return identity.hex()
    def close(self):
        for f in self.files.values():
            if not f.closed:f.close()
    def finish(self):
        if self.finished:raise ValueError('Bundle finalized')
        try:
            ordered=sorted(self.assets,key=lambda a:a['id']);byid={a['id']:a for a in ordered}
            colors={}
            def visit(key,depth):
                if depth>64 or colors.get(key)==1:raise ValueError('Dependency cycle/depth')
                if colors.get(key)==2:return
                colors[key]=1
                for role,kind,target in byid[key]['deps']:
                    if target not in byid or byid[target]['kind']!=kind:raise ValueError('Missing typed dependency')
                    visit(target,depth+1)
                colors[key]=2
            for key in byid:visit(key,0)
            recipe=dict(schema=1,source=self.source.hex(),compiler=self.compiler.hex(),world=self.world_definition,assets=[dict(id=a['id'].hex(),kind=a['kind'],bytes=a['bytes'],sha=a['sha'].hex(),meta=sha(a['meta']).hex(),count=a['count'],stride=a['stride'],deps=[(r,k,i.hex()) for r,k,i in a['deps']]) for a in ordered])
            build=sha(b'W2CMW.Build.r1\0'+canonical(recipe));record_table=bytearray();meta_table=bytearray();deps_table=bytearray()
            asset_at=HEADER;block_at=asset_at+len(ordered)*ASSET_STRIDE;meta_at=block_at+len(self.blocks)*BLOCK_STRIDE
            for a in ordered:
                while len(meta_table)%8:meta_table.append(0)
                mo=meta_at+len(meta_table);meta_table.extend(a['meta']);first_dep=len(deps_table)//DEP_STRIDE
                for role,kind,key in a['deps']:deps_table.extend(struct.pack('<II32s',role,kind,key))
                row=bytearray(ASSET_STRIDE);row[:32]=a['id']
                struct.pack_into('<IIQIIIIQIIII',row,32,a['kind'],0,a['bytes'],a['first'],a['blocks'],a['count'],a['stride'],mo,len(a['meta']),first_dep,len(a['deps']),0)
                row[88:120]=a['sha'];struct.pack_into('<I',row,120,crc(a['meta']));record_table.extend(row)
            block_table=b''.join(struct.pack('<QIIHHIII32s',*row) for row in self.blocks)
            while len(meta_table)%8:meta_table.append(0)
            dep_at=meta_at+len(meta_table);payload=record_table+block_table+meta_table+deps_table
            if len(payload)>INDEX_LIMIT:raise ValueError('Aggregate index budget')
            index_header=envelope(1,HEADER+len(payload),build,self.source,self.compiler,assets=len(ordered),blocks=len(self.blocks),asset_at=asset_at,block_at=block_at,meta_at=meta_at,meta_bytes=len(meta_table),dep_at=dep_at,deps=len(deps_table)//DEP_STRIDE,payload_hash=sha(payload),payload_crc=crc(payload))
            for role,f in self.files.items():
                size=f.tell();f.seek(0);f.write(envelope(role,size,build,self.source,self.compiler,payload_hash=self.digests[role].digest()));f.flush();os.fsync(f.fileno());f.close()
            with (self.root/NAMES[1]).open('xb') as f:f.write(index_header);f.write(payload);f.flush();os.fsync(f.fileno())
            symbols=dict(schema=1,buildId=build.hex(),sourceId=self.source.hex(),compilerFingerprint=self.compiler.hex(),sourceManifest=self.source_manifest,worldDefinition=self.world_definition,assets=[dict(id=a['id'].hex(),kind=a['kind'],key=a['key'],sha256=a['sha'].hex(),bytes=a['bytes']) for a in ordered])
            with (self.root/'L5RA.symbols.json').open('xb') as f:f.write(canonical(symbols)+b'\n');f.flush();os.fsync(f.fileno())
            self.finished=True
            return dict(buildId=build.hex(),sourceId=self.source.hex(),assets=len(ordered),blocks=len(self.blocks),indexBytes=HEADER+len(payload),files=[dict(name=p.name,bytes=p.stat().st_size) for p in sorted(self.root.iterdir())])
        finally:self.close()

class Reader:
    """Reference reader. Native validation is an independent implementation."""
    def __init__(self,root):
        self.files={}
        try:self._open(root)
        except BaseException:self.close();raise
    def _open(self,root):
        self.root=Path(root);self.assets={};self.files={};self.headers={}
        for role,name in NAMES.items():
            path=self.root/name
            if path.is_symlink() or path.is_junction():raise ValueError('Bundle reparse point')
            f=path.open('rb');self.files[role]=f;h=f.read(HEADER)
            if len(h)!=HEADER or h[:8]!=MAGIC[role] or struct.unpack_from('<HH5I',h,8)!=(1,0,256,role,3,128,64) or struct.unpack_from('<Q',h,32)[0]!=path.stat().st_size or path.stat().st_size>MAX_TOTAL:raise ValueError('Envelope contract')
            c=bytearray(h);expected=struct.unpack_from('<I',h,224)[0];struct.pack_into('<I',c,224,0)
            if crc(c)!=expected or struct.unpack_from('<4I',h,232)!=(BLOCK_LIMIT,0,1,1) or h[248:]!=bytes(8):raise ValueError('Envelope CRC/features')
            if role>1 and (h[40:92]!=bytes(52) or struct.unpack_from('<I',h,92)[0]!=40):raise ValueError('Data envelope reserved fields')
            self.headers[role]=h
        h=self.headers[1];self.build=h[96:128];self.source=h[128:160]
        if any(v[96:192]!=h[96:192] for v in self.headers.values()):raise ValueError('Mixed generation')
        asset_at,ac,bc,block_at,meta_at,meta_bytes,dep_at,dc,ds=struct.unpack_from('<QIIQQQQII',h,40)
        if ac>MAX_ASSETS or bc>MAX_BLOCKS or ds!=DEP_STRIDE or asset_at!=256 or block_at!=asset_at+ac*128 or meta_at!=block_at+bc*64 or dep_at!=meta_at+meta_bytes or dep_at+dc*40!=struct.unpack_from('<Q',h,32)[0] or dep_at+dc*40-256>INDEX_LIMIT:raise ValueError('Index dimensions')
        payload=self.files[1].read(INDEX_LIMIT+1)
        if len(payload)>INDEX_LIMIT or sha(payload)!=h[192:224] or crc(payload)!=struct.unpack_from('<I',h,228)[0]:raise ValueError('Index digest/CRC')
        self.index=h+payload;self.blocks=[];ranges={2:[],3:[]};owned_blocks=set();meta_ranges=[];dep_ranges=[]
        for i in range(bc):
            row=struct.unpack_from('<QIIHHIII32s',self.index,block_at+64*i);offset,stored,decoded,codec,role,reserved,pc,uc,digest=row
            if role not in (2,3) or codec!=0 or reserved or not 0<decoded<=BLOCK_LIMIT or stored!=decoded or offset<256 or offset%64 or offset+stored>len_size(self.files[role]):raise ValueError('Block bounds/codec')
            self.blocks.append(row);ranges[role].append((offset,offset+stored))
        previous=b''
        for i in range(ac):
            at=asset_at+i*128;key=self.index[at:at+32];kind,flags,n,first,nb,count,stride,mo,mn,df,dn,reserved=struct.unpack_from('<IIQIIIIQIIII',self.index,at+32)
            if key<=previous or not kind or flags or reserved or self.index[at+124:at+128]!=bytes(4) or n>ASSET_LIMIT or mn>MAX_META or mo<meta_at or mo%8 or mo+mn>meta_at+meta_bytes or first+nb>bc or dn>1024 or df+dn>dc:raise ValueError('Asset dimensions/order')
            previous=key;meta=self.index[mo:mo+mn]
            if not mn or crc(meta)!=struct.unpack_from('<I',self.index,at+120)[0]:raise ValueError('Metadata CRC')
            def reject_constant(value):raise ValueError('Nonfinite metadata constant')
            parsed=json.loads(meta,parse_constant=reject_constant)
            if not isinstance(parsed,dict):raise ValueError('Typed metadata object')
            if sum(self.blocks[j][2] for j in range(first,first+nb))!=n:raise ValueError('Asset block byte sum')
            for j in range(first,first+nb):
                if j in owned_blocks:raise ValueError('Block has multiple owners')
                owned_blocks.add(j)
            refs=[struct.unpack_from('<II32s',self.index,dep_at+(df+j)*40) for j in range(dn)]
            if asset_id(self.source,kind,parsed.get('sourceKey',''))!=key:raise ValueError('Source asset ID/key mismatch')
            self.assets[key]=dict(kind=kind,bytes=n,first=first,blocks=nb,count=count,stride=stride,metadata=parsed,metadataSha=sha(meta),sha=self.index[at+88:at+120],deps=refs)
            meta_ranges.append((mo,mo+mn));dep_ranges.extend(range(df,df+dn))
        if len(owned_blocks)!=bc or len(set(dep_ranges))!=dc or len(dep_ranges)!=dc:raise ValueError('Unowned block/dependency table')
        end=meta_at
        for lo,hi in sorted(meta_ranges):
            if lo<end or self.index[end:lo]!=bytes(lo-end):raise ValueError('Metadata overlap/padding')
            end=hi
        if self.index[end:meta_at+meta_bytes]!=bytes(meta_at+meta_bytes-end):raise ValueError('Metadata tail')
        for role,spans in ranges.items():
            end=256
            for lo,hi in sorted(spans):
                if lo<end or lo-end>63:raise ValueError('Data overlap/unowned gap')
                self.files[role].seek(end)
                if self.files[role].read(lo-end)!=bytes(lo-end):raise ValueError('Data padding')
                end=hi
            if end!=len_size(self.files[role]):raise ValueError('Unowned data extent')
        colors={}
        def visit(key,depth):
            if depth>64 or colors.get(key)==1:raise ValueError('Dependency cycle/depth')
            if colors.get(key)==2:return
            colors[key]=1
            for role,kind,target in self.assets[key]['deps']:
                if not role or target not in self.assets or self.assets[target]['kind']!=kind:raise ValueError('Missing typed dependency')
                visit(target,depth+1)
            colors[key]=2
        for key in self.assets:visit(key,0)
        symbols_path=self.root/'L5RA.symbols.json'
        if symbols_path.is_symlink() or symbols_path.is_junction() or symbols_path.stat().st_size>32*1024**2:raise ValueError('Symbols file bound/link')
        symbols=json.loads(symbols_path.read_bytes(),parse_constant=lambda v:(_ for _ in ()).throw(ValueError('Nonfinite symbols')))
        if symbols.get('schema')!=1 or symbols.get('buildId')!=self.build.hex() or symbols.get('sourceId')!=self.source.hex() or symbols.get('compilerFingerprint')!=h[160:192].hex() or sha(b'W2CMW.Source.r1\0'+canonical(symbols['sourceManifest']))!=self.source:raise ValueError('Symbols/source generation mismatch')
        expected=[];recipe=[]
        for identity,a in self.assets.items():
            expected.append(dict(id=identity.hex(),kind=a['kind'],key=a['metadata']['sourceKey'],sha256=a['sha'].hex(),bytes=a['bytes']))
            recipe.append(dict(id=identity.hex(),kind=a['kind'],bytes=a['bytes'],sha=a['sha'].hex(),meta=a['metadataSha'].hex(),count=a['count'],stride=a['stride'],deps=[(r,k,i.hex()) for r,k,i in a['deps']]))
        if symbols.get('assets')!=expected:raise ValueError('Symbols asset ledger mismatch')
        document=dict(schema=1,source=self.source.hex(),compiler=h[160:192].hex(),world=symbols['worldDefinition'],assets=recipe)
        if sha(b'W2CMW.Build.r1\0'+canonical(document))!=self.build:raise ValueError('Build recipe does not match typed asset semantics')
    def read_id(self,key):
        a=self.assets[key];out=bytearray()
        for offset,stored,decoded,codec,role,reserved,pc,uc,digest in self.blocks[a['first']:a['first']+a['blocks']]:
            f=self.files[role];f.seek(offset);b=f.read(stored)
            if len(b)!=stored or crc(b)!=pc or crc(b)!=uc or sha(b)!=digest:raise ValueError('Block integrity before use')
            out.extend(b)
        if len(out)!=a['bytes'] or sha(out)!=a['sha']:raise ValueError('Asset integrity before use')
        return bytes(out)
    def contains(self,kind,key):return asset_id(self.source,kind,key) in self.assets
    def read(self,kind,key):return self.read_id(asset_id(self.source,kind,key))
    def close(self):
        for f in self.files.values():f.close()
def len_size(f):return os.fstat(f.fileno()).st_size
