import argparse,hashlib,struct,json
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
parser=argparse.ArgumentParser();parser.add_argument('binary');args=parser.parse_args()
digest=hashlib.sha256(open(args.binary,'rb').read()).hexdigest()
assert digest=='da588b82782669ca6170c0a139bbb649d9214779d80bd9b4619e1fba84bd60dc', 'This inspector requires the supplied Leaf 1.1.5 arm64 binary'
f=open(args.binary,'rb');e=ELFFile(f)
assert e['e_machine']=='EM_AARCH64'
u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0,0x10000000)
for seg in e.iter_segments():
 if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
rel=e.get_section_by_name('.rela.dyn')
for r in rel.iter_relocations():
 if r['r_info_type']==1027:u.mem_write(r['r_offset'],struct.pack('<Q',r['r_addend']))
u.mem_map(0x10000000,0x2000000);u.mem_map(0x20000000,0x400000);u.mem_map(0x30000000,0x1000)
u.reg_write(UC_ARM64_REG_SP,0x203ff000);u.reg_write(UC_ARM64_REG_TPIDR_EL0,0x10000000);u.reg_write(UC_ARM64_REG_LR,0x30000000)
heap=0x10001000
symbols=e.get_section_by_name('.dynsym');plt=e.get_section_by_name('.plt');imports={}
for i,r in enumerate(e.get_section_by_name('.rela.plt').iter_relocations()):imports[plt['sh_addr']+32+i*16]=symbols.get_symbol(r['r_info_sym']).name
def cstr(a):
 out=bytearray()
 while True:
  b=u.mem_read(a+len(out),1)
  if b==b'\0':return bytes(out)
  out.extend(b)
def hook(uc,address,size,data):
 global heap
 name=imports.get(address)
 if name is None:return
 a=uc.reg_read(UC_ARM64_REG_X0);b=uc.reg_read(UC_ARM64_REG_X1);n=uc.reg_read(UC_ARM64_REG_X2);result=a
 if name in ['_Znwm','malloc','_Znam']:
  result=heap;heap+=(a+31)&~31
 elif name in ['_ZdlPv','_ZdaPv','free']:pass
 elif name in ['memcpy','memmove']:uc.mem_write(a,bytes(uc.mem_read(b,n)))
 elif name=='memset':uc.mem_write(a,bytes([b&255])*n)
 elif name=='strlen':result=len(cstr(a))
 elif name=='memcmp':
  x=bytes(uc.mem_read(a,n));y=bytes(uc.mem_read(b,n));result=(x>y)-(x<y)
 else:raise RuntimeError((hex(address),name,hex(uc.reg_read(UC_ARM64_REG_LR))))
 uc.reg_write(UC_ARM64_REG_X0,result&0xffffffffffffffff);uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_LR))
u.hook_add(UC_HOOK_CODE,hook,begin=plt['sh_addr'],end=plt['sh_addr']+plt['sh_size'])
result=0x10000100;u.reg_write(UC_ARM64_REG_X8,result)
u.emu_start(0x20c4e50,0x30000000,count=10000000)
assert u.reg_read(UC_ARM64_REG_PC)==0x30000000
a,b,cap=struct.unpack('<QQQ',u.mem_read(result,24))
assert (b-a)//0x120==14
def string(a):
 b=bytes(u.mem_read(a,24))
 if b[0]&1:
  size,pointer=struct.unpack_from('<QQ',b,8);return bytes(u.mem_read(pointer,size)).decode()
 return b[1:1+b[0]//2].decode()
def strings(p):
 a,b,_=struct.unpack('<QQQ',u.mem_read(p,24))
 return [string(x) for x in range(a,b,24)] if a else []
output=[]
for i in range(14):
 p=a+i*0x120
 start,end,_=struct.unpack('<QQQ',u.mem_read(p+0x30,24))
 regions=[]
 for q in range(start,end,0x120):
  raw=bytes(u.mem_read(q,0x120))
  if string(q) not in {'choice_bubble','title_press','title_menu','letter_preview','map_show','tab_screen','hud_tabs','hud_map'}:continue
  regions.append({'name':string(q),'source':struct.unpack_from('<4f',raw,0x18),'target':struct.unpack_from('<2f',raw,0x28),'origin':struct.unpack_from('<2f',raw,0x30),'offset':struct.unpack_from('<2f',raw,0x38),'scale':struct.unpack_from('<f',raw,0x40)[0]})
 output.append({'name':string(p),'requires_any':strings(p+0x18),'requires_all':strings(p+0x50),'regions':regions})

print(json.dumps({"binary_sha256":digest,"builder":"0x20c4e50","scenes":output},indent=2))
