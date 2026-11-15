import struct,json,hashlib
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *

class Native:
 def __init__(self,path):
  self.u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);self.heap=0x60000000
  self.f=open(path,'rb');self.e=ELFFile(self.f)
  for s in self.e.iter_segments():
   if s['p_type']!='PT_LOAD':continue
   a=s['p_vaddr']&~4095;size=(s['p_vaddr']+s['p_memsz']+4095&~4095)-a
   self.u.mem_map(a,size);self.u.mem_write(s['p_vaddr'],s.data())
  for a,size in ((0x40000000,4096),(0x50000000,0x200000),(0x60000000,0x8000000),(0x70000000,4096)):self.u.mem_map(a,size)
  self.u.reg_write(UC_ARM64_REG_TPIDR_EL0,0x40000000)
  self.u.reg_write(UC_ARM64_REG_CPACR_EL1,3<<20)
  rela=self.e.get_section_by_name('.rela.dyn');sy=self.e.get_section(rela['sh_link'])
  for r in rela.iter_relocations():
   t=r['r_info_type']
   if t==1027:self.w64(r['r_offset'],r['r_addend'])
   elif t in (1025,1026,257):
    s=sy.get_symbol(r['r_info_sym'])
    if s['st_value']:self.w64(r['r_offset'],s['st_value']+r['r_addend'])
  rela=self.e.get_section_by_name('.rela.plt');sy=self.e.get_section(rela['sh_link']);plt=self.e.get_section_by_name('.plt')['sh_addr']
  self.plt={plt+32+i*16:(sy.get_symbol(r['r_info_sym']).name,sy.get_symbol(r['r_info_sym'])['st_value']) for i,r in enumerate(rela.iter_relocations())}
  self.u.hook_add(UC_HOOK_CODE,self.hook,begin=plt,end=plt+self.e.get_section_by_name('.plt')['sh_size'])
 def alloc(self,n):
  n=max(16,n);a=self.heap;self.heap=(a+n+15)&~15
  if self.heap>=0x68000000:raise RuntimeError('heap limit')
  return a
 def w64(self,a,v):self.u.mem_write(a,struct.pack('<Q',v))
 def q(self,a):return struct.unpack('<Q',self.u.mem_read(a,8))[0]
 def cstr(self,a):
  b=bytearray()
  while True:
   c=self.u.mem_read(a+len(b),1)
   if c==b'\0':return bytes(b)
   b+=c
 def string(self,s):
  b=s.encode();a=self.alloc(24)
  if len(b)<23:self.u.mem_write(a,bytes([len(b)*2])+b+b'\0')
  else:
   p=self.alloc(len(b)+1);self.u.mem_write(p,b+b'\0');self.u.mem_write(a,struct.pack('<QQQ',(len(b)+16&~15)|1,len(b),p))
  return a
 def read_string(self,a):
  n=self.u.mem_read(a,1)[0]
  return bytes(self.u.mem_read(self.q(a+16),self.q(a+8))) if n&1 else bytes(self.u.mem_read(a+1,n>>1))
 def hook(self,u,a,size,data):
  if a not in self.plt:raise RuntimeError('unknown plt '+hex(a))
  name,target=self.plt[a];x=[u.reg_read(UC_ARM64_REG_X0+i) for i in range(4)];ret=0
  if name in ('_Znwm','malloc') or name.startswith('_Znwm'):ret=self.alloc(x[0])
  elif name.startswith('_ZdlPv') or name in ('free','__cxa_atexit','__cxa_finalize'):pass
  elif name in ('memcpy','memmove','__memcpy_chk','__memmove_chk'):
   if x[2]:u.mem_write(x[0],bytes(u.mem_read(x[1],x[2])))
   ret=x[0]
  elif name in ('memset','__memset_chk'):
   if x[2]:u.mem_write(x[0],bytes([x[1]&255])*x[2])
   ret=x[0]
  elif name in ('strlen','__strlen_chk'):ret=len(self.cstr(x[0]))
  elif name=='memchr':
   pos=bytes(u.mem_read(x[0],x[2])).find(bytes([x[1]&255]));ret=x[0]+pos if pos>=0 else 0
  elif name=='strnlen':ret=len(bytes(u.mem_read(x[0],x[1])).split(b'\0')[0])
  elif name=='memcmp':
   l=bytes(u.mem_read(x[0],x[2]));r=bytes(u.mem_read(x[1],x[2]));ret=(l>r)-(l<r)
  elif name=='strcmp':
   l=self.cstr(x[0]);r=self.cstr(x[1]);ret=(l>r)-(l<r)
  elif name=='__cxa_guard_acquire':ret=1
  elif name=='__cxa_guard_release':u.mem_write(x[0],b'\1')
  elif target:u.reg_write(UC_ARM64_REG_PC,target);return
  else:raise RuntimeError('unhandled '+name+' '+hex(a))
  u.reg_write(UC_ARM64_REG_X0,ret&0xffffffffffffffff);u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
 def call(self,a,args=(),result=None,floats=()):
  self.u.reg_write(UC_ARM64_REG_SP,0x501ff000);self.u.reg_write(UC_ARM64_REG_LR,0x70000000)
  for i,x in enumerate(args):self.u.reg_write(UC_ARM64_REG_X0+i,x)
  for i,x in enumerate(floats):self.u.reg_write(UC_ARM64_REG_S0+i,struct.unpack('<I',struct.pack('<f',x))[0])
  if result:self.u.reg_write(UC_ARM64_REG_X8,result)
  try:self.u.emu_start(a,0x70000000,count=10000000)
  except Exception as ex:raise RuntimeError(str(ex)+' at '+hex(self.u.reg_read(UC_ARM64_REG_PC))) from ex
  if self.u.reg_read(UC_ARM64_REG_PC)!=0x70000000:raise RuntimeError('instruction limit')
  return self.u.reg_read(UC_ARM64_REG_X0)
 def vector(self,b):
  p=self.alloc(len(b));self.u.mem_write(p,b);v=self.alloc(24);self.u.mem_write(v,struct.pack('<QQQ',p,p+len(b),p+len(b)));return v
 def entries(self,v):
  return [(self.read_string(a).decode(),bytes(self.u.mem_read(self.q(a+24),self.q(a+32)-self.q(a+24)))) for a in range(self.q(v),self.q(v+8),48)]

if __name__=='__main__':
 import argparse,zipfile
 parser=argparse.ArgumentParser()
 parser.add_argument('native_library');parser.add_argument('layout_zip');parser.add_argument('output')
 args=parser.parse_args()
 if hashlib.sha256(Path(args.native_library).read_bytes()).hexdigest()!='da588b82782669ca6170c0a139bbb649d9214779d80bd9b4619e1fba84bd60dc':raise RuntimeError('unsupported Leaf native library version')
 n=Native(args.native_library);n.call(0x20bb05c)
 first,last=n.q(0xa403ad8),n.q(0xa403ae0)
 if last<first or (last-first)%0xa0 or last-first>64*0xa0:raise RuntimeError('unexpected patch table')
 specs=[a for a in range(first,last,0xa0) if n.cstr(n.q(a))==b'Layout/Title/opng_titl.arc']
 if len(specs)!=1:raise RuntimeError('missing title patch')
 spec=specs[0];graft=n.q(spec+0x28)+8;wrap=n.q(spec+0x58)
 with zipfile.ZipFile(args.layout_zip) as z:
  def read(name):
   result=n.alloc(24);n.call(0x20aae34,[n.vector(z.read(name))],result=result);return result
  target=read('Title/opng_titl.arc');donor=read('selWin/cmn_slct_win.arc')
 if n.call(0x20ad060,[target,donor,graft])!=1 or n.call(0x20b0428,[target,wrap])!=1:raise RuntimeError('native title patch failed')
 output=Path(args.output);output.mkdir(parents=True,exist_ok=True)
 files=n.entries(target)
 for name,data in files:
  relative=Path(name)
  if relative.is_absolute() or '..' in relative.parts:raise RuntimeError('invalid native path')
  path=output/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
 print(json.dumps({'library_sha256':hashlib.sha256(Path(args.native_library).read_bytes()).hexdigest(),
  'graft_strings':[n.read_string(graft+24*i).decode() for i in range(5)],
  'geometry':struct.unpack('<6f',n.u.mem_read(graft+120,24)),
  'wrapper_strings':[n.read_string(wrap+24*i).decode() for i in range(5)],'files':len(files)}))
