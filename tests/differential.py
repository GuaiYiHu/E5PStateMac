#!/usr/bin/env python3
"""Compare arithmetic against functions extracted from a supplied Linux source.

The caller supplies the GPL-licensed upstream reference. Extracted reference
functions are built privately in a temporary directory, not bundled in releases.
No hardware access or CPU load beyond short integer calculations.
"""
import ctypes
import hashlib
import json
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def function(source,name):
    match=re.search(r'(?m)^static[^\n]*\b'+re.escape(name)+r'\(',source)
    if not match:raise ValueError('Missing reference function '+name)
    opening=source.index('{',match.start());depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]

def main():
    if len(sys.argv)!=2:raise SystemExit('Usage: python3 differential.py /path/to/Linux/drivers/cpufreq/intel_pstate.c')
    path=Path(sys.argv[1]);source=path.read_text()
    reference='''#include <stdint.h>
typedef uint64_t u64; typedef int64_t s64;
#define READ_ONCE(x) (x)
#define FRAC_BITS 8
#define EXT_FRAC_BITS 14
#define max(a,b) ((a)>(b)?(a):(b))
#define clamp_t(type,value,lo,hi) ((type)((value)<(lo)?(lo):((value)>(hi)?(hi):(value))))
static int64_t div64_s64(int64_t x,int64_t y){return x/y;}
static uint64_t div64_u64(uint64_t x,uint64_t y){return x/y;}
struct sample {u64 aperf,mperf,tsc,core_avg_perf;int32_t busy_scaled;};
struct cpudata {sample sample;struct {int min_pstate,max_pstate,max_pstate_physical,turbo_pstate;} pstate;
unsigned iowait_boost;int aperf_mperf_shift,min_perf_ratio,max_perf_ratio;};
static struct {int no_turbo,turbo_disabled;} global;
'''
    for name in ('mul_fp','div_fp','mul_ext_fp','div_ext_fp','get_avg_pstate','get_target_pstate','intel_pstate_prepare_request'):
        reference+='\n'+function(source,name)+'\n'
    reference+='''extern "C" int reference(uint64_t a,uint64_t m,uint64_t t,int minimum,int nominal,int turbo,int lo,int hi,int boost,int noTurbo){
struct cpudata cpu={};cpu.sample.aperf=a;cpu.sample.mperf=m;cpu.sample.tsc=t;
cpu.sample.core_avg_perf=div_ext_fp(a,m);cpu.iowait_boost=boost;
cpu.pstate.min_pstate=minimum;cpu.pstate.max_pstate=nominal;cpu.pstate.max_pstate_physical=nominal;
cpu.pstate.turbo_pstate=turbo;cpu.min_perf_ratio=lo;cpu.max_perf_ratio=hi;
global.no_turbo=noTurbo&1;global.turbo_disabled=(noTurbo>>1)&1;
return intel_pstate_prepare_request(&cpu,get_target_pstate(&cpu));}
'''
    implementation='''#include "NonHwpGovernor.hpp"
extern "C" int implementation(uint64_t a,uint64_t m,uint64_t t,int minimum,int nominal,int turbo,int lo,int hi,int boost,int noTurbo){
e5::GovernorConfig c{{(uint32_t)minimum,(uint32_t)nominal,(uint32_t)turbo},(uint32_t)lo,(uint32_t)hi,4000000,(bool)noTurbo};
auto d=e5::chooseNonHwp({t,a,m},c,(uint32_t)boost,0);return d.sampled?(int)d.ratio:-1;}
'''
    compiler=shutil.which('clang++') or shutil.which('g++')
    if not compiler:raise SystemExit('A C++ compiler is required')
    with tempfile.TemporaryDirectory() as temporary:
        directory=Path(temporary)
        functions=[]
        for name,body in (('reference',reference),('implementation',implementation)):
            file=directory/(name+'.cpp');file.write_text(body)
            binary=directory/(name+('.dylib' if sys.platform=='darwin' else '.so'))
            subprocess.run([compiler,'-std=c++17','-O2','-fPIC',
                '-dynamiclib' if sys.platform=='darwin' else '-shared','-I'+str(ROOT/'src'),
                str(file),'-o',str(binary)],check=True)
            f=getattr(ctypes.CDLL(str(binary)),name)
            f.argtypes=[ctypes.c_uint64]*3+[ctypes.c_int]*7;f.restype=ctypes.c_int
            functions.append(f)
        rng=random.Random(26663)
        tested=0
        for _ in range(20000):
            duration=rng.randrange(29000000,2900000001)
            mperf=rng.randrange(1,duration+1)
            ratio=rng.randrange(8000,35001)
            aperf=mperf*ratio//29000
            noTurbo=rng.randrange(4)
            cap=29 if noTurbo else 35
            low=rng.randrange(8,cap+1);high=rng.randrange(low,cap+1)
            args=(aperf,mperf,duration,8,29,35,low,high,rng.choice([0,32,64,128,256]),noTurbo)
            expected=functions[0](*args);actual=functions[1](*args)
            if actual!=expected:raise AssertionError((args,expected,actual))
            tested+=1
        print(json.dumps({'reference_file':str(path),'reference_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
              'cases':tested,'mismatches':0,'scope':'non-HWP target selection arithmetic only; not scheduler/MSR/runtime parity'},indent=2))

if __name__=='__main__':main()
