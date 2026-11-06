#!/usr/bin/env python3
"""Build the version-pinned experimental macOS adapter. Never installs or loads it."""
from pathlib import Path
import argparse
import plistlib
import shutil
import subprocess
from targets import TARGETS, select

ROOT=Path(__file__).resolve().parent

def run(args):
    print(' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),check=True,cwd=ROOT)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    modes=parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--shadow-only',action='store_true',help='Compile out frequency control; default mode 1')
    modes.add_argument('--guarded-control',action='store_true',help='Enable guarded dynamic control; default mode 2')
    parser.add_argument('--target',choices=TARGETS,help='Reviewed kernel target; defaults to the exact running OS build')
    args=parser.parse_args()
    target_name,target=select(args.target)
    version='0.5.0'
    sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
    headers=Path(sdk)/'System/Library/Frameworks/Kernel.framework/Headers'
    build=ROOT/('build-'+target_name+('-shadow' if args.shadow_only else '-control'));build.mkdir(exist_ok=True)
    flags=['xcrun','clang++','-arch','x86_64','-isysroot',sdk,'-mmacosx-version-min='+target['deployment'],
        '-std=c++17','-mkernel','-DKERNEL','-DKERNEL_PRIVATE','-fapple-kext','-fno-exceptions',
        '-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mno-sse','-mno-mmx',
        '-msoft-float','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
        '-Wno-deprecated-declarations','-DPRODUCT_NAME=E5PStateMac','-DMODULE_VERSION='+version,
        '-DLILU_CUSTOM_IOKIT_INIT=1','-isystem',str(ROOT/'vendor/MacKernelSDK/Headers'),
        '-isystem',str(headers),'-isystem',str(ROOT/'vendor/LiluSDK'),'-I'+str(ROOT/'src')]
    flags += ['-D'+definition for definition in target['defines']]
    if args.shadow_only:flags.append('-DE5_SHADOW_ONLY=1')
    if args.guarded_control:flags.append('-DE5_GUARDED_CONTROL=1')
    objects=[]
    for name,source in [('adapter',ROOT/'src/MacAdapter.cpp'),('module',ROOT/'src/Module.cpp'),
                        ('plugin',ROOT/'vendor/LiluSDK/Library/plugin_start.cpp')]:
        obj=build/(name+'.o');run([*flags,'-c',source,'-o',obj]);objects.append(obj)
    bundle=build/'E5PStateMac.kext';(bundle/'Contents/MacOS').mkdir(parents=True,exist_ok=True)
    info=plistlib.loads((ROOT/'Info.plist').read_bytes())
    info['CFBundleVersion']=version;info['CFBundleShortVersionString']=version
    info['E5ShadowOnlyBuild']=args.shadow_only
    info['E5GuardedControlBuild']=args.guarded_control
    info['E5TargetProfile']=target_name
    info['E5TargetBuild']=target['build']
    plistlib.dump(info,(bundle/'Contents/Info.plist').open('wb'))
    run(['xcrun','clang++','-arch','x86_64','-isysroot',sdk,'-mmacosx-version-min='+target['deployment'],
         '-nostdlib','-Xlinker','-kext','-Xlinker','-undefined','-Xlinker','dynamic_lookup',
         '-Xlinker','-no_fixup_chains',*objects,'-L'+sdk+'/usr/lib','-lkmod','-o',bundle/'Contents/MacOS/E5PStateMac'])
    run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',bundle])
    run(['/usr/bin/plutil','-lint',bundle/'Contents/Info.plist'])
    print('Built experimental adapter:',bundle)
    print('Not installed or loaded. Source-level control path is unverified in a running kernel.')

if __name__=='__main__':main()
