#!/usr/bin/env python3
"""Portable policy tests, with optional exact-kernel and Linux reference checks.

Never installs/loads a kext or modifies EFI. Kernel bytes stay in a temporary
directory on the computer that supplied them and must not be committed.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from targets import TARGETS, select


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel-profile', action='store_true',
                        help='Require the exact reviewed macOS kernel/KC and run native-body checks')
    parser.add_argument('--linux-reference', type=Path,
                        help='Optional locally supplied Linux intel_pstate.c for differential checks')
    parser.add_argument('--binary', type=Path,
                        help='Optional built .kext to check with macOS tools')
    parser.add_argument('--target',choices=TARGETS,
                        help='Target for optional exact-kernel checks; inferred from running build')
    args = parser.parse_args()
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        parser.error('A C++17 compiler is required')
    results = {}
    from targets_test import check
    results['targets'] = check(compiler)
    with tempfile.TemporaryDirectory(prefix='e5pstate-tests-') as temp:
        temp = Path(temp)
        cases = ['identity', 'adapter', 'governor', 'control_safety']
        fixture = None
        if args.kernel_profile:
            target_name,target=select(args.target)
            subprocess.run([sys.executable, str(ROOT/'tests/profile_test.py'),'--target',target_name], check=True)
            sys.path.insert(0, str(ROOT))
            from kc_profile import CACHE, MachO
            profile = json.loads(target['boot'].read_text())
            cache = MachO(CACHE.read_bytes())
            kernel = MachO(cache.data, cache.entries['com.apple.kernel'][1])
            function = profile['functions']['urgency']
            fixture = temp/'native-urgency.bin'
            fixture.write_bytes(kernel.read(int(function['address'], 0), function['length']))
            cases.append('native_state')
        for name in cases:
            binary = temp/(name+'-test')
            definitions=['-D'+d for d in target['defines']] if name=='native_state' else []
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            *definitions,str(ROOT/'tests'/(name+'_test.cpp')), '-o', str(binary)], check=True)
            command = [str(binary)]
            if name == 'native_state':
                command.append(str(fixture))
            results[name] = subprocess.check_output(command, text=True).strip()
    if not args.kernel_profile:
        results['native_kernel'] = 'SKIPPED: use --kernel-profile on the exact reviewed macOS build'
    if args.linux_reference:
        subprocess.run([sys.executable, str(ROOT/'tests/differential.py'),
                        str(args.linux_reference.resolve())], check=True)
        results['linux_differential'] = 'PASS'
    if args.binary:
        subprocess.run([sys.executable, str(ROOT/'tests/binary_test.py'),
                        str(args.binary.resolve())], check=True)
        results['binary'] = 'PASS'
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
