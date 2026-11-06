#!/usr/bin/env python3
"""Read-only audit of the pinned boot KC. Does not modify EFI or kernel caches."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess
from targets import TARGETS, select

ROOT=Path(__file__).resolve().parent
KERNEL=Path('/System/Library/Kernels/kernel')
CACHE=Path('/System/Library/KernelCollections/BootKernelExtensions.kc')

class MachO:
    def __init__(self,data,base=0):
        self.data=data;self.base=base;self.segments={};self.uuid=None;self.symtab=None;self.entries={};self.function_starts=None
        magic,cpu,_,self.filetype,count,size,_,_=struct.unpack_from('<8I',data,base)
        assert magic==0xfeedfacf and cpu==0x1000007
        offset=base+32;end=offset+size
        assert end<=len(data)
        for _ in range(count):
            command,length=struct.unpack_from('<II',data,offset)
            assert length>=8 and offset+length<=end
            if command==0x19:
                name,vm,sz,fo,fs=struct.unpack_from('<16s4Q',data,offset+8)
                assert fo+fs<=len(data)
                self.segments[name.split(b'\0')[0].decode()]=(vm,sz,fo,fs)
            elif command==0x1b:self.uuid=data[offset+8:offset+24].hex()
            elif command==2:self.symtab=struct.unpack_from('<4I',data,offset+8)
            elif command==0x26:self.function_starts=struct.unpack_from('<II',data,offset+8)
            elif command==0x80000035:
                vm,fo,string,_=struct.unpack_from('<QQII',data,offset+8)
                assert 32<=string<length
                name=data[offset+string:offset+length].split(b'\0')[0].decode()
                self.entries[name]=(vm,fo)
            offset+=length
        assert offset==end

    def read(self,address,length):
        for vm,_,fo,fs in self.segments.values():
            if vm<=address and address+length<=vm+fs:
                return self.data[fo+address-vm:fo+address-vm+length]
        raise ValueError(hex(address))

    def symbols(self,wanted):
        offset,count,strings,string_size=self.symtab
        assert offset+count*16<=len(self.data) and strings+string_size<=len(self.data)
        result={}
        for i in range(count):
            index,_,_,_,value=struct.unpack_from('<IBBHQ',self.data,offset+i*16)
            assert index<string_size
            end=self.data.find(b'\0',strings+index,strings+string_size)
            assert end>=0
            name=self.data[strings+index:end].decode(errors='replace')
            if name in wanted:result[name]=value
        assert set(result)==set(wanted)
        return result

    def translate(self,address,target):
        for name,(vm,size,_,_) in self.segments.items():
            if vm<=address<vm+size:
                other,other_size,_,_=target.segments[name]
                assert address-vm<other_size
                return other+address-vm
        raise ValueError(hex(address))

    def function_sizes(self):
        offset,size=self.function_starts
        assert offset+size<=len(self.data)
        end=offset+size;address=self.segments['__TEXT'][0];starts=[]
        while offset<end:
            value=0;shift=0
            while True:
                assert offset<end and shift<64
                byte=self.data[offset];offset+=1;value|=(byte&127)<<shift;shift+=7
                if byte<128:break
            if not value:break
            address+=value;starts.append(address)
        return {a:b-a for a,b in zip(starts,starts[1:])}

def fnv(raw):
    value=14695981039346656037
    for byte in raw:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value

def prove_relocations(standalone,embedded,old_address,new_address,length,kernel_path=KERNEL):
    original=standalone.read(old_address,length);predicted=bytearray(original)
    assembly=subprocess.check_output(['xcrun','llvm-objdump','--disassemble',
        '--start-address='+hex(old_address),'--stop-address='+hex(old_address+length),str(kernel_path)],text=True)
    covered=0;changed=[]
    for line in assembly.splitlines():
        match=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s*(.*)$',line)
        if not match:continue
        address=int(match[1],16);raw=bytes.fromhex(match[2]);instruction=match[3]
        assert address==old_address+covered and original[covered:covered+len(raw)]==raw
        target=None
        rip=re.search(r'(-?0x[0-9a-f]+|-?\d+)\(%rip\)',instruction)
        branch=re.match(r'(?:callq|j\w+)\s+(0x[0-9a-f]+)',instruction)
        if rip:target=(address+len(raw)+int(rip[1],0))&0xffffffffffffffff
        elif branch:target=int(branch[1],16)
        if target is not None:
            translated=standalone.translate(target,embedded)
            delta=translated-target-(new_address-old_address)
            if delta:
                old_disp=target-(address+len(raw))
                # All cross-segment PC-relative operands in these four
                # functions are 32-bit displacements, independently decoded.
                pattern=struct.pack('<i',old_disp)
                positions=[i for i in range(len(raw)-3) if raw[i:i+4]==pattern]
                assert len(positions)==1,(line,positions)
                pos=positions[0];assert covered+pos>=5
                predicted[covered+pos:covered+pos+4]=struct.pack('<i',old_disp+delta)
                changed.append({'instruction_offset':covered,'displacement_offset':covered+pos,'kind':'rip' if rip else 'relative-branch'})
        covered+=len(raw)
    assert covered==length
    actual=embedded.read(new_address,length)
    assert bytes(predicted)==actual,'KC differs by more than independently explained address relocations'
    assert original[:7]==actual[:7],'Short trampoline prologue changed'
    return {'only_explained_address_relocations':True,'relocated_operands':len(changed),
            'changed_bytes':sum(a!=b for a,b in zip(original,actual)),'unchanged_short_prologue':True}

def audit(target=None):
    _,selected=select(target)
    reference=json.loads(selected['standalone'].read_text())
    expected=json.loads(selected['boot'].read_text())
    raw=KERNEL.read_bytes();cache=CACHE.read_bytes()
    assert hashlib.sha256(raw).hexdigest()==reference['kernel_sha256']
    assert hashlib.sha256(cache).hexdigest()==expected['boot_kc_sha256'],'Different KC: explicit re-audit required'
    outer=MachO(cache);assert outer.filetype==12
    _,entry=outer.entries['com.apple.kernel']
    standalone=MachO(raw);embedded=MachO(cache,entry)
    assert standalone.uuid==embedded.uuid==reference['kernel_uuid']
    live=subprocess.check_output(['/usr/sbin/sysctl','-n','kern.uuid'],text=True).strip().replace('-','').lower()
    assert live==embedded.uuid
    build=subprocess.check_output(['/usr/bin/sw_vers','-buildVersion'],text=True).strip()
    assert build==selected['build'],'Different OS build: explicit re-audit required'
    mapping={'writer':'_xcpm_enable_pstate_ctrs','timer':'_xcpm_timer','urgency':'_xcpm_urgency','deadline':'_xcpm_get_deadline'}
    symbols=embedded.symbols(mapping.values());functions={};proofs={}
    old_sizes=standalone.function_sizes();new_sizes=embedded.function_sizes()
    for name,symbol in mapping.items():
        address=symbols[symbol]+(selected['writer_offset'] if name=='writer' else 0)
        old=reference['functions'][name];length=old['length'];data=embedded.read(address,length)
        assert old_sizes[int(old['address'],0)]==new_sizes[address]==length,'Reviewed function boundary changed'
        proofs[name]=prove_relocations(standalone,embedded,int(old['address'],0),address,length)
        functions[name]={'address':hex(address),'length':length,'sha256':hashlib.sha256(data).hexdigest(),'fnv1a64':hex(fnv(data))}
    return {'kernel_uuid':embedded.uuid,'kernel_sha256':reference['kernel_sha256'],
        'boot_kc_sha256':expected['boot_kc_sha256'],'boot_kc_uuid':outer.uuid,'kernel_fileset_offset':entry,
        'functions':functions,'relocation_proofs':proofs}

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',choices=TARGETS)
    print(json.dumps(audit(parser.parse_args().target),indent=2))
