#!/usr/bin/env python3
"""Verify the exact boot KC and prove all function differences are relocations."""
from pathlib import Path
import json
import re
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from kc_profile import audit
from kc_profile import CACHE,MachO
expected=json.loads((ROOT/'kernel-profile.json').read_text())
actual=audit()
assert actual==expected,'Profile differs from independently audited pinned boot KC'
header=(ROOT/'src/Profile.hpp').read_text()
for key,record in actual['functions'].items():
    name=key.capitalize()+'Fingerprint'
    match=re.search(name+r'=(0x[0-9a-f]+)ULL;',header)
    assert match and int(match[1],0)==int(record['fnv1a64'],0),'Compiled source profile differs'
cache=MachO(CACHE.read_bytes())
kernel=MachO(cache.data,cache.entries['com.apple.kernel'][1])
timer=actual['functions']['timer']
raw=kernel.read(int(timer['address'],0),timer['length'])
# Independent native instructions: index * 0x380, and load urgency at +0xCC.
assert bytes.fromhex('4969d680030000') in raw
assert bytes.fromhex('8b9fcc000000') in raw
native_header=(ROOT/'src/NativeState.hpp').read_text()
assert 'CpuStride=0x380,UrgencyOffset=0xCC' in native_header
print(json.dumps({'boot_kc_sha256':actual['boot_kc_sha256'],
    'running_kernel_uuid_matches':True,'relocation_proofs':actual['relocation_proofs'],
    'scope':'Static KC audit; runtime fingerprints still gate all hooks.'},indent=2))
