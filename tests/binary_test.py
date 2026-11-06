#!/usr/bin/env python3
"""Static binary checks. This does not establish runtime safety or frequency restoration."""
from pathlib import Path
import json
import re
import struct
import subprocess
import sys
import plistlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from targets import select
bundle=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/E5PStateMac.kext'
binary=bundle/'Contents/MacOS/E5PStateMac'
header=struct.unpack('<8I',binary.read_bytes()[:32]);assert header[0]==0xfeedfacf and header[1]==0x01000007 and header[3]==11
assembly=subprocess.check_output(['xcrun','llvm-objdump','--disassemble',str(binary)],text=True)
assert not re.search(r'\b(?:xmm|ymm|zmm)\d+\b',assembly)
imports=subprocess.check_output(['nm','-u',str(binary)],text=True)
symbols=subprocess.check_output(['nm','-an',str(binary)],text=True)
assert 'urgencyHook' not in symbols and 'originalUrgency' not in symbols,'Shared urgency function must never be routed'
assert 'deadlineHook' not in symbols and 'originalDeadline' not in symbols,'Native deadlines must remain unchanged'
for forbidden in ('_pmKextRegister','_pmUnRegister','_mp_rendezvous','_wrmsr','___udivti3'):
    assert forbidden not in imports,forbidden
assert '_rdmsr_carefully' in imports and 'routeMultipleShort' in imports and '_lilu' in imports
assert not re.search(r'\bwrmsr\b',assembly),'Only the original native writer may issue the hardware write'
subprocess.run(['/usr/bin/codesign','--verify','--strict',str(bundle)],check=True)
info=plistlib.loads((bundle/'Contents/Info.plist').read_bytes())
shadow=info.get('E5ShadowOnlyBuild') is True
guarded=info.get('E5GuardedControlBuild') is True
if 'E5TargetProfile' in info:
    _,target=select(info['E5TargetProfile'])
    assert info['E5TargetBuild']==target['build']
    for obj in ('adapter.o','module.o','plugin.o'):
        path=bundle.parent/obj
        if path.exists():
            commands=subprocess.check_output(['xcrun','otool','-l',str(path)],text=True)
            assert re.search(r'\bminos\s+'+re.escape(target['deployment'])+r'\s',commands)
    assert info['E5TargetBuild'].encode()+b'\0' in binary.read_bytes()
assert shadow != guarded
assert 'verifyWrite' in symbols if guarded else 'verifyWrite' not in symbols
if shadow:
    demangled=subprocess.check_output(['xcrun','llvm-objdump','--disassemble','--demangle',str(binary)],text=True)
    # Optimized shadow build has exactly one original-writer invocation, in
    # the native writer's passthrough hook; the extra control path is absent.
    calls=[line for line in demangled.splitlines() if re.search(r'\bcallq\b',line) and '<adapter::originalWriter>' in line]
    assert len(calls)==1,calls
    assert not re.search(r'\b(?:callq|jmpq)\b.*<adapter::(?:packageLock|packageUnlock|resync)>',demangled)
print(json.dumps({'mach_o':'x86_64 KEXT_BUNDLE','simd_registers':0,'pm_dispatch_reregistration':False,
    'periodic_cross_cpu_rendezvous':False,'direct_wrmsr_instructions':len(re.findall(r'\bwrmsr\b',assembly)),
    'shadow_only':shadow,'guarded_control_build':guarded,'native_deadlines_unchanged':True,
    'hardware_control_path':'Native writer passthrough only; extra writer and lock paths compiled out.' if shadow else 'Guarded native writer and local timer updates with request readback.',
    'runtime_tested_by_this_check':False},indent=2))
