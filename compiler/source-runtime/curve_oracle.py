"""Private offline format research. Does not launch or patch either game.

Emulates the supplied Carbon executable's scalar track decoder in an isolated
Unicorn address space. Not a shipping converter dependency or runtime feature.
"""
import argparse, json, struct, sys
from pathlib import Path
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP
sys.path.insert(0, str(Path(__file__).resolve().parent))
from audit_breaker_sequences import chunks


def evaluate(executable, ingame, scene, phases=None):
    pe = pefile.PE(str(executable))
    image = pe.get_memory_mapped_image()
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(0x400000, (len(image) + 4095) & ~4095)
    machine.mem_write(0x400000, image)
    for start, size in [(0,4096),(0x10000000,65536),(0x11000000,65536),
                        (0x20000000,131072),(0x30000000,4096)]:
        machine.mem_map(start, size)
    digest = 0xffffffff
    for char in scene:
        digest = (digest * 33 + ord(char)) & 0xffffffff
    groups = [r for tag, _, body in chunks(ingame.read_bytes()) if tag == 0x8003b200
              for _, _, r in chunks(body) if r[:4] == struct.pack('<I',digest)]
    if len(groups) != 1 or struct.unpack_from('<I',groups[0],4)[0] != 1:
        raise ValueError('Exactly one camera track required for this research')
    machine.mem_write(0x10000000, groups[0][8:])

    def call(address, args):
        stack = 0x2001f000
        machine.mem_write(stack,struct.pack('<'+'I'*(len(args)+1),0x30000000,*args))
        machine.reg_write(UC_X86_REG_ESP,stack)
        machine.reg_write(UC_X86_REG_ECX,0x11000000)
        machine.emu_start(address,0x30000000,count=2000000)
        if machine.reg_read(UC_X86_REG_EIP) != 0x30000000:
            raise RuntimeError('Emulation did not return')
    call(0x489e50,[])  # Native camera field/group registry initialization.
    call(0x497b00,[])
    call(0x48a4b0,[0x10000000,0])
    descriptors=[]
    for index in range(55):
        values=struct.unpack('<22I',pe.get_data(0xA57C90-0x400000+index*88,88))
        text=image[values[0]-0x400000:].split(b'\0',1)[0].decode('ascii')
        descriptors.append(dict(name=text,curve=values[2],kind=values[3],bits=values[4]))
    frames=[]
    for phase in phases if phases is not None else [0,.25,.5,.75,1]:
        call(0x493da0,[struct.unpack('<I',struct.pack('<f',phase))[0],1,0])
        raw=bytes(machine.mem_read(0x11000010,55*4))
        frames.append(dict(phase=phase,fields={d['name']:dict(value=struct.unpack_from('<f',raw,4*i)[0],bits=struct.unpack_from('<I',raw,4*i)[0]) for i,d in enumerate(descriptors)}))
    return dict(scene=scene,length=struct.unpack_from('<f',groups[0],20)[0],descriptors=descriptors,frames=frames)


if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path);parser.add_argument('ingame',type=Path);parser.add_argument('scene');parser.add_argument('output',type=Path)
    args=parser.parse_args()
    args.output.write_text(json.dumps(evaluate(args.executable,args.ingame,args.scene),indent=2)+'\n',encoding='utf-8')
