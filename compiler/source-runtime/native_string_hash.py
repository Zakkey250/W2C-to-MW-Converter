"""Private read-only EN1.3 string-hash oracle for converter format research.

Emulates a user-supplied executable, never launches or patches the game. This
research helper is not a public converter dependency or redistributable asset.
"""
import struct
from pathlib import Path
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP

class NativeStringHash:
    def __init__(self,executable):
        pe=pefile.PE(str(executable));raw=pe.get_memory_mapped_image()
        self.machine=Uc(UC_ARCH_X86,UC_MODE_32)
        self.machine.mem_map(0x400000,(len(raw)+4095)&~4095);self.machine.mem_write(0x400000,raw)
        for address in [0x10000000,0x20000000,0x30000000]:self.machine.mem_map(address,65536)
        for name,expected in [('ESpawnExplosion',0x31BBBAE2),('ESpawnFragment',0x2554C48A),('tollbooth_seq',0x919191C6)]:
            if self(name)!=expected:raise ValueError('Unsupported executable hash routine')
    def __call__(self,text):
        data=text.encode('ascii')+b'\0'
        if len(data)>4096:raise ValueError('Hash input too long')
        m=self.machine;m.mem_write(0x10000000,data);m.mem_write(0x20008000,struct.pack('<II',0x30000000,0x10000000))
        m.reg_write(UC_X86_REG_ESP,0x20008000);m.emu_start(0x5CC240,0x30000000,count=100000)
        if m.reg_read(UC_X86_REG_EIP)!=0x30000000:raise ValueError('Hash routine did not return')
        return m.reg_read(UC_X86_REG_EAX)
