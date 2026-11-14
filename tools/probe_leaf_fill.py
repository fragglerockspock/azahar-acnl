import struct
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_X9,UC_ARM64_REG_X24,UC_ARM64_REG_X25,UC_ARM64_REG_SP
with open(sys.argv[1],'rb') as f:
    e=ELFFile(f)
    a=0x2178a24
    section=next(s for s in e.iter_sections() if s['sh_addr']<=a<s['sh_addr']+s['sh_size'])
    code=section.data()[a-section['sh_addr']:a-section['sh_addr']+0x200]
for fmt,expected in enumerate([0xff00ffff,0xff00ff,0xf81f,0xf83f,0xf0ff]):
    for start,end,keyed in [(0x400008,0x40fff8,True),(0x3ffff8,0x400008,True),(0x410000,0x410008,False)]:
        u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
        for addr in [0x2178000,0xac0b000,0x100000,0x200000]:u.mem_map(addr,0x1000)
        u.mem_write(a,code)
        u.mem_write(0xac0b500,struct.pack('<4I',1,0x410000,0x400000,fmt))
        u.mem_write(0x100000,struct.pack('<2I',start//8,end//8))
        u.reg_write(UC_ARM64_REG_X9,0xac0b000)
        u.reg_write(UC_ARM64_REG_X24,0x100000);u.reg_write(UC_ARM64_REG_X25,0x100000)
        u.reg_write(UC_ARM64_REG_SP,0x200000)
        stopped=[]
        def stop(uc,addr,size,user):
            if addr in [0x2178ab8,0x2178c10]:stopped.append(addr);uc.emu_stop()
        u.hook_add(UC_HOOK_CODE,stop)
        u.emu_start(a,0x2178c20,count=150)
        assert stopped==[0x2178c10 if keyed else 0x2178ab8],(fmt,start,end,stopped)
        if keyed:
            value=struct.unpack('<I',u.mem_read(0x200008,4))[0]
            assert value==expected,(fmt,value,expected)
print('Actual Leaf AArch64 partial-fill overlap and all five key constants passed')
