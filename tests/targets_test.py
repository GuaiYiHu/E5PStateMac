#!/usr/bin/env python3
"""Check both compiled profile definitions without requiring either live kernel."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from targets import TARGETS, select


def check(compiler):
    for name, target in TARGETS.items():
        boot = json.loads(target['boot'].read_text())
        standalone = json.loads(target['standalone'].read_text())
        assert boot['kernel_uuid'] == standalone['kernel_uuid']
        assert boot['kernel_sha256'] == standalone['kernel_sha256']
        uuid = bytes.fromhex(boot['kernel_uuid'])
        assertions = [f'static_assert(profile::KernelMajor=={target["darwin"][0]});',
                      f'static_assert(profile::KernelMinor=={target["darwin"][1]});',
                      f'static_assert(profile::WriterOffsetFromAnchor=={target["writer_offset"]});']
        assertions += [f'static_assert(profile::UUID[{i}]=={b});' for i, b in enumerate(uuid)]
        for key, record in boot['functions'].items():
            assert record['length'] == standalone['functions'][key]['length']
            assertions += [f'static_assert(profile::{key.capitalize()}Size=={record["length"]});',
                           f'static_assert(profile::{key.capitalize()}Fingerprint=={record["fnv1a64"]}ULL);']
        with tempfile.TemporaryDirectory(prefix='e5-target-') as temp:
            source = Path(temp) / 'check.cpp'
            source.write_text('#include "Profile.hpp"\n#include <cstring>\n' + '\n'.join(assertions) +
                              '\nint main(){return std::strcmp(profile::Build,"' + target['build'] +
                              '") | std::strcmp(profile::Target,"' + name +
                              '") | std::strcmp(profile::BootKCSHA256,"' + boot['boot_kc_sha256'] + '");}\n')
            binary = Path(temp) / 'check'
            command = [compiler, '-std=c++17', '-I'+str(ROOT/'src'), str(source)]
            definitions = ['-D'+d for d in target['defines']]
            subprocess.run([*command, *definitions, '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    for definitions in ([], ['-D'+d for t in TARGETS.values() for d in t['defines']]):
        rejected = subprocess.run([compiler, '-std=c++17', '-x', 'c++', '-fsyntax-only',
                                   '-I'+str(ROOT/'src'), *definitions, '-'],
                                  input='#include "Profile.hpp"\n', text=True, capture_output=True)
        assert rejected.returncode != 0 and 'error:' in rejected.stderr
    try:
        select('unreviewed-build')
    except ValueError:
        pass
    else:
        raise AssertionError('An unreviewed target must be rejected')
    return 'PASS: both compiled profiles match independent JSON; missing, conflicting and unknown targets rejected'


if __name__ == '__main__':
    import shutil
    print(check(shutil.which('clang++') or shutil.which('g++')))
