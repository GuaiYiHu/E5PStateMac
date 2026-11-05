// SPDX-License-Identifier: BSD-2-Clause
#include "MacAdapter.hpp"
#include "AdapterPolicy.hpp"
#include "Profile.hpp"
#include "Identity.hpp"
#include "NativeState.hpp"
#include "ControlSafety.hpp"
#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <i386/proc_reg.h>
#include <i386/machine_routines.h>
#include <mach-o/loader.h>
#include <kern/clock.h>

extern "C" int cpu_number(void);
OSDefineMetaClassAndStructors(E5PStateMac,IOService)

namespace adapter {
#if defined(E5_SHADOW_ONLY) == defined(E5_GUARDED_CONTROL)
#error Select exactly one reviewed observation or guarded-control build.
#endif
constexpr unsigned MaxCpus=e5mac::ExpectedCpus;
// Keep the raw integer return register intact, including for native call
// sites that ignore it. The private writer's source-level type is unpublished.
using Writer=uint64_t (*)(uint32_t);
using Timer=void (*)(void *);
using TicketLock=void (*)(void *);
static Writer originalWriter=nullptr;
static Timer originalTimer=nullptr;
static TicketLock packageLock=nullptr,packageUnlock=nullptr;
static void *packageBase=nullptr;
static volatile int *xcpmMode=nullptr,*hwp=nullptr,*paused=nullptr,*assertions=nullptr,*configured=nullptr;
static volatile uint32_t *appleMinimum=nullptr,*appleMaximum=nullptr;
static const uint8_t *nativeCpuBase=nullptr;
static e5::Limits hardware={};
struct Diagnostics {
    int kernelMajor,kernelMinor;
    uint32_t signature,leaf6a,leaf6c,extendedMaximum,extended7d,operatingMinimum,allCoreTurbo;
    char brand[49];
    uint64_t msrs[4],fingerprints[4];
    int errors[4];
    bool identityAccepted;
    bool urgencyBodyVerified,urgencyEntryRouted;
    uint64_t urgencyNormalized;
};
static Diagnostics diagnostics={};
static volatile uint32_t mode=0,hooks=0,serviceReady=0,suspended=1,epoch=1,fault=0;
static volatile uint64_t warmupStartNs=0;
static const char * volatile status="not-requested";
static void setStatus(const char *value) {__atomic_store_n(&status,value,__ATOMIC_RELEASE);}

struct Cpu {
    e5::GovernorState governor;
    e5mac::Readiness readiness;
    uint32_t epoch;
    bool noTurbo,thermalLimited;
    uint64_t lastReadNs;
    volatile uint32_t active,appleRatio,policyRatio,appliedRatio,busyQ8,mhz,nativeClass;
    volatile uint32_t requestRatio,decisionValid,qualifiedEpoch,goodSamples,thermalFlags;
    volatile uint64_t samples,writes,writerCallbacks,timerCallbacks,nativeStateReads;
    volatile uint64_t lastSampleNs,lastPolicyNs,invalidSamples,gapResets,readbackWrites,fallbackWrites;
};
static Cpu cpus[MaxCpus];
template <class T> static T load(const volatile T &value) {return __atomic_load_n(&value,__ATOMIC_ACQUIRE);}
template <class T> static void store(volatile T &destination,T value) {__atomic_store_n(&destination,value,__ATOMIC_RELEASE);}
static void increment(volatile uint64_t &value) {__atomic_fetch_add(&value,1,__ATOMIC_RELAXED);}

static uint64_t nowNs() {
    uint64_t absolute,ns;
    clock_get_uptime(&absolute);absolutetime_to_nanoseconds(absolute,&ns);return ns;
}
static bool readMsr(uint32_t address,uint64_t &value) {
    uint32_t lo,hi;
    if (rdmsr_carefully(address,&lo,&hi)) return false;
    value=(static_cast<uint64_t>(hi)<<32)|lo;return true;
}
static void cpuid(uint32_t leaf,uint32_t sub,uint32_t &a,uint32_t &b,uint32_t &c,uint32_t &d) {
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(leaf),"c"(sub));
}
static bool identify() {
    uint32_t a,b,c,d;
    cpuid(0,0,a,b,c,d);
    if (a<0xB || b!=0x756e6547 || d!=0x49656e69 || c!=0x6c65746e) {setStatus("cpuid-vendor-or-level-mismatch");return false;}
    cpuid(1,0,a,b,c,d);
    diagnostics.signature=a;
    if (((a>>8)&15)!=6 || (((a>>4)&15)|((a>>12)&0xf0))!=0x3f || (c&(1U<<31))) {setStatus("cpuid-model-or-hypervisor-mismatch");return false;}
    cpuid(6,0,a,b,c,d);
    diagnostics.leaf6a=a;diagnostics.leaf6c=c;
    if (!(c&1) || (a&(1U<<7))) {setStatus("cpuid-aperf-or-hwp-mismatch");return false;}
    cpuid(0x80000000,0,a,b,c,d);diagnostics.extendedMaximum=a;
    if (a<0x80000007) {setStatus("cpuid-extended-leaf-unavailable");return false;}
    cpuid(0x80000007,0,a,b,c,d);diagnostics.extended7d=d;
    if (!(d&(1U<<8))) {setStatus("cpuid-invariant-tsc-unavailable");return false;}
    auto *brand=diagnostics.brand;
    for (unsigned i=0;i<3;++i) {
        cpuid(0x80000002+i,0,a,b,c,d);
        memcpy(brand+16*i,&a,4);memcpy(brand+16*i+4,&b,4);
        memcpy(brand+16*i+8,&c,4);memcpy(brand+16*i+12,&d,4);
    }
    bool named=false;
    for (unsigned i=0;i+10<sizeof(diagnostics.brand);++i) if (!memcmp(brand+i,"E5-2666 v3",10)) named=true;
    if (!named) {setStatus("cpu-brand-mismatch");return false;}
    constexpr uint32_t addresses[]={0x35,0xCE,0x1AD,0x1AE};
    const char *failures[]={"msr-35-read-failed","msr-ce-read-failed","msr-1ad-read-failed","msr-1ae-read-failed"};
    for (unsigned i=0;i<4;++i) {
        uint32_t lo=0,hi=0;
        diagnostics.errors[i]=rdmsr_carefully(addresses[i],&lo,&hi);
        if (!diagnostics.errors[i]) diagnostics.msrs[i]=(static_cast<uint64_t>(hi)<<32)|lo;
    }
    for (unsigned i=0;i<4;++i) if (diagnostics.errors[i]) {setStatus(failures[i]);return false;}
    const auto counts=diagnostics.msrs[0],platform=diagnostics.msrs[1];
    const auto turbo0=diagnostics.msrs[2],turbo1=diagnostics.msrs[3];
    const auto decoded=e5mac::decodePlatform(platform,turbo0);
    hardware=decoded.linuxLimits;diagnostics.operatingMinimum=decoded.operatingMinimum;
    diagnostics.allCoreTurbo=e5::turboRatioForCores(turbo0,turbo1,10);
    if ((counts&0xffff)!=20 || ((counts>>16)&0xffff)!=10) {setStatus("msr-core-thread-count-mismatch");return false;}
    if (!e5mac::stock2666Platform(decoded)) {setStatus("platform-ratio-mismatch");return false;}
    if (diagnostics.allCoreTurbo!=32) {setStatus("stock-all-core-turbo-mismatch");return false;}
    diagnostics.identityAccepted=true;setStatus("identity-accepted");return true;
}

static e5mac::Gate gate() {
    return {load(mode),bool(load(hooks)),bool(load(serviceReady)),bool(load(suspended)),
        bool(load(fault)),xcpmMode && *xcpmMode==1,!hwp || bool(*hwp),
        !paused || bool(*paused),!assertions || bool(*assertions),configured && *configured==1};
}
static bool enabled() {return e5mac::canSample(gate());}
static uint32_t readyMask() {
    const uint32_t generation=load(epoch);uint32_t mask=0;
    for (unsigned i=0;i<MaxCpus;++i)
        if (load(cpus[i].qualifiedEpoch)==generation) mask|=1U<<i;
    return load(epoch)==generation?mask:0;
}
static bool control() {
#ifdef E5_SHADOW_ONLY
    return false;
#else
    return e5mac::canControl(gate()) &&
        e5mac::warmupReady(nowNs(),load(warmupStartNs),readyMask());
#endif
}
static bool cpuIndex(void *lcpu,unsigned &index) {
    if (!lcpu) return false;
    index=*reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(lcpu)+profile::LcpuNumberOffset);
    const int local=cpu_number();
    return local>=0 && index<MaxCpus && index==static_cast<unsigned>(local);
}
static unsigned localCpu() {const int i=cpu_number();return i<0?MaxCpus:static_cast<unsigned>(i);}
static bool refreshNativeState(unsigned index) {
    size_t offset;
    if (!nativeCpuBase || !e5mac::nativeCpuOffset(index,offset)) return false;
    const auto *source=reinterpret_cast<const volatile uint32_t *>(nativeCpuBase+offset);
    const uint32_t nativeClass=__atomic_load_n(source,__ATOMIC_ACQUIRE);
    store(cpus[index].nativeClass,nativeClass);
    increment(cpus[index].nativeStateReads);return true;
}

// Caller has disabled interrupts. No allocations, logging, blocking locks or IPIs here.
static uint32_t select(Cpu &cpu,unsigned index,uint32_t apple) {
    if (!e5mac::sampleLocal(gate(),index) || !refreshNativeState(index)) return apple;
    const uint32_t generation=load(epoch);
    if (cpu.epoch!=generation) {
        cpu.governor={};cpu.epoch=generation;store(cpu.lastSampleNs,uint64_t(0));
        store(cpu.policyRatio,uint32_t(0));store(cpu.mhz,uint32_t(0));
        cpu.readiness={};store(cpu.qualifiedEpoch,uint32_t(0));store(cpu.goodSamples,uint32_t(0));
        store(cpu.decisionValid,uint32_t(0));store(cpu.lastPolicyNs,uint64_t(0));
        cpu.lastReadNs=0;cpu.noTurbo=false;cpu.thermalLimited=false;
    }
    const uint64_t ns=nowNs();
    // No native-class/idle gate: the old gate suppressed busy-CPU samples.
    // Limit reads before RDMSR and use only the existing local XCPM callbacks.
    if (!cpu.lastReadNs || ns<cpu.lastReadNs || ns-cpu.lastReadNs>=e5mac::IntervalNs) {
        cpu.lastReadNs=ns;
        uint64_t aperf,mperf,misc,thermal,request;
        if (!readMsr(0xE8,aperf) || !readMsr(0xE7,mperf) || !readMsr(0x1A0,misc) ||
            !readMsr(0x19C,thermal) || !readMsr(0x199,request)) {
            store(fault,uint32_t(1));return apple;
        }
        store(cpu.requestRatio,static_cast<uint32_t>((request>>8)&255));
        cpu.noTurbo=(misc&(1ULL<<38))!=0;
        // Current thermal status, PROCHOT/FORCEPR and critical temperature.
        store(cpu.thermalFlags,static_cast<uint32_t>(thermal&0x15));
        cpu.thermalLimited=(thermal&0x15)!=0;
        const auto caps=e5mac::combineCaps(hardware,*appleMinimum,*appleMaximum,cpu.noTurbo);
        if (!caps.valid || cpu.thermalLimited) {
            store(cpu.decisionValid,uint32_t(0));e5mac::noteSample(cpu.readiness,ns,false);
            store(cpu.goodSamples,uint32_t(0));return apple;
        }
        if (cpu.governor.primed && (ns<cpu.governor.lastSampleNs ||
            ns-cpu.governor.lastSampleNs>e5mac::MaximumSampleGapNs)) {
            cpu.governor={};cpu.readiness={};increment(cpu.gapResets);
        }
        const e5::Counters now={rdtsc64(),aperf,mperf};
        const auto previous=cpu.governor.previous;
        const bool primed=cpu.governor.primed;
        const e5::GovernorConfig config={hardware,caps.minimum,caps.maximum,4000000,cpu.noTurbo};
        // No verified macOS SCHED_CPUFREQ_IOWAIT equivalent is available here.
        const auto decision=e5::updateNonHwp(cpu.governor,config,now,ns,false,index,index);
        store(cpu.lastSampleNs,cpu.governor.lastSampleNs);
        const auto metrics=primed?e5::measure(previous,now,hardware):e5::Metrics{};
        const bool valid=decision.sampled && metrics.valid && metrics.frequencyValid;
        store(cpu.decisionValid,uint32_t(valid));
        if (e5mac::noteSample(cpu.readiness,ns,valid)) store(cpu.qualifiedEpoch,generation);
        store(cpu.goodSamples,cpu.readiness.good);
        if (valid) {
            increment(cpu.samples);store(cpu.busyQ8,decision.busyQ8);store(cpu.policyRatio,decision.ratio);
            store(cpu.mhz,metrics.busyMHzEstimate);store(cpu.lastPolicyNs,ns);
            store(cpu.active,uint32_t(decision.busyQ8!=0));
        } else {increment(cpu.invalidSamples);store(cpu.active,uint32_t(0));}
    }
    const auto caps=e5mac::combineCaps(hardware,*appleMinimum,*appleMaximum,cpu.noTurbo);
    return e5mac::selectRequest(apple,cpu.governor.currentRequest,caps,control(),
        e5mac::freshDecision(ns,load(cpu.lastPolicyNs),load(cpu.decisionValid),cpu.thermalLimited));
}

// IA32_PERF_CTL is a request, not a frequency measurement. Verify the request
// after an overridden write; a failed readback latches pass-through globally.
static bool verifyWrite(Cpu &cpu,uint32_t selected) {
    uint64_t request;
    if (!readMsr(0x199,request)) {store(fault,uint32_t(1));return false;}
    store(cpu.requestRatio,static_cast<uint32_t>((request>>8)&255));
    if (load(cpu.requestRatio)!=selected) {store(fault,uint32_t(2));return false;}
    increment(cpu.readbackWrites);return true;
}

static uint64_t writeHook(uint32_t ratio) {
    const boolean_t interrupts=ml_set_interrupts_enabled(false);
    const unsigned index=localCpu();
    uint32_t selected=ratio;
    if (index<MaxCpus) {
        auto &cpu=cpus[index];increment(cpu.writerCallbacks);
        store(cpu.appleRatio,ratio);
        selected=select(cpu,index,ratio);
        if (selected!=ratio) increment(cpu.writes);
        store(cpu.appliedRatio,selected);
    }
    // Sole normal XCPM frequency-write entry for this exact kernel profile.
    // Keep Apple's original hardware write; change only the ratio argument.
    uint64_t result=originalWriter(selected);
    if (index<MaxCpus && selected!=ratio && !verifyWrite(cpus[index],selected)) {
        result=originalWriter(ratio);increment(cpus[index].fallbackWrites);
        store(cpus[index].appliedRatio,ratio);
    }
    ml_set_interrupts_enabled(interrupts);
    return result;
}
static void applyLocal() {
    const boolean_t interrupts=ml_set_interrupts_enabled(false);
    const unsigned index=localCpu();
    if (index<MaxCpus && enabled()) {
        const uint64_t ns=nowNs();
        if (cpus[index].epoch==load(epoch) && cpus[index].lastReadNs && ns>=cpus[index].lastReadNs &&
            ns-cpus[index].lastReadNs<e5mac::IntervalNs) {
            ml_set_interrupts_enabled(interrupts);return;
        }
        uint64_t current;
        if (readMsr(0x199,current)) {
            const uint32_t observed=static_cast<uint32_t>((current>>8)&255);
            const uint32_t selected=select(cpus[index],index,observed);
            if (control() && selected!=observed) {
                // Native callers serialize this writer with the package ticket
                // lock. The extra local tick must obey that same contract.
                packageLock(packageBase);
                const auto caps=e5mac::combineCaps(hardware,*appleMinimum,*appleMaximum,cpus[index].noTurbo);
                const uint32_t rechecked=e5mac::boundedRatio(selected,caps);
                if (control() && rechecked && rechecked!=observed &&
                    e5mac::freshDecision(nowNs(),load(cpus[index].lastPolicyNs),
                        load(cpus[index].decisionValid),cpus[index].thermalLimited)) {
                    originalWriter(rechecked);increment(cpus[index].writes);
                    store(cpus[index].appliedRatio,rechecked);
                    if (!verifyWrite(cpus[index],rechecked)) {
                        originalWriter(observed);increment(cpus[index].fallbackWrites);
                        store(cpus[index].appliedRatio,observed);
                    }
                }
                packageUnlock(packageBase);
            }
        } else store(fault,uint32_t(1));
    }
    ml_set_interrupts_enabled(interrupts);
}
static void timerHook(void *lcpu) {
    originalTimer(lcpu);
    unsigned index;
    if (cpuIndex(lcpu,index)) {
        increment(cpus[index].timerCallbacks);applyLocal();
    }
}

static bool uuidMatches(uintptr_t address) {
    const auto *header=reinterpret_cast<const mach_header_64 *>(address);
    if (!header || header->magic!=MH_MAGIC_64 || header->cputype!=CPU_TYPE_X86_64 ||
        header->ncmds>512 || header->sizeofcmds>65536) return false;
    const uint8_t *cursor=reinterpret_cast<const uint8_t *>(header+1),*end=cursor+header->sizeofcmds;
    for (uint32_t i=0;i<header->ncmds;++i) {
        if (cursor+sizeof(load_command)>end) return false;
        const auto *command=reinterpret_cast<const load_command *>(cursor);
        if (command->cmdsize<sizeof(load_command) || command->cmdsize>static_cast<size_t>(end-cursor)) return false;
        if (command->cmd==LC_UUID && command->cmdsize>=sizeof(uuid_command))
            return !memcmp(reinterpret_cast<const uuid_command *>(command)->uuid,profile::UUID,16);
        cursor+=command->cmdsize;
    }
    return false;
}
static void patch(void *,KernelPatcher &patcher) {
    diagnostics.kernelMajor=getKernelVersion();diagnostics.kernelMinor=getKernelMinorVersion();
    for (auto &error:diagnostics.errors) error=-1;
    if (diagnostics.kernelMajor!=KernelVersion::Tahoe || diagnostics.kernelMinor!=6) {
        setStatus("unsupported-kernel-version");return;
    }
    if (!identify()) return;
    auto solve=[&](const char *name) {auto result=patcher.solveSymbol(KernelPatcher::KernelID,name);patcher.clearError();return result;};
    const uintptr_t header=solve("__mh_execute_header"),anchor=solve("_xcpm_enable_pstate_ctrs");
    const uintptr_t writer=anchor+profile::WriterOffsetFromAnchor;
    const uintptr_t timer=solve("_xcpm_timer"),urgency=solve("_xcpm_urgency"),deadline=solve("_xcpm_get_deadline");
    if (!header || !anchor || !timer || !urgency || !deadline) {setStatus("missing-profile-symbol");return;}
    if (!uuidMatches(header)) {setStatus("kernel-uuid-mismatch");return;}
    const uintptr_t addresses[]={writer,timer,urgency,deadline};
    const uint32_t lengths[]={profile::WriterSize,profile::TimerSize,profile::UrgencySize,profile::DeadlineSize};
    const uint64_t expected[]={profile::WriterFingerprint,profile::TimerFingerprint,profile::UrgencyFingerprint,profile::DeadlineFingerprint};
    const char *mismatches[]={"writer-fingerprint-mismatch","timer-fingerprint-mismatch","urgency-fingerprint-mismatch","deadline-fingerprint-mismatch"};
    for (unsigned i=0;i<4;++i)
        diagnostics.fingerprints[i]=e5mac::fingerprint(reinterpret_cast<void *>(addresses[i]),lengths[i]);
    for (unsigned i=0;i<4;++i) {
        if (i==2) continue; // Shared urgency entry is read-only and never routed.
        if (diagnostics.fingerprints[i]!=expected[i]) {setStatus(mismatches[i]);return;}
    }
    const auto urgencyBody=e5mac::validateUrgencyBody(reinterpret_cast<const void *>(urgency),profile::UrgencySize,profile::UrgencyFingerprint);
    diagnostics.urgencyBodyVerified=urgencyBody.valid;diagnostics.urgencyEntryRouted=urgencyBody.routed;
    diagnostics.urgencyNormalized=urgencyBody.normalized;
    if (!urgencyBody.valid) {setStatus("urgency-native-body-mismatch");return;}
    const auto package=solve("_xcpm_pkg");
    xcpmMode=reinterpret_cast<volatile int *>(solve("_xcpm_mode"));
    hwp=reinterpret_cast<volatile int *>(solve("_xcpm_hwp_enabled"));
    paused=reinterpret_cast<volatile int *>(solve("_xcpm_dvfs_paused"));
    configured=reinterpret_cast<volatile int *>(solve("_xcpm_dvfs_configured"));
    assertions=reinterpret_cast<volatile int *>(solve("_xcpm_assert_enable"));
    nativeCpuBase=reinterpret_cast<const uint8_t *>(solve("_xcpm_cpus"));
    packageLock=reinterpret_cast<TicketLock>(solve("_lck_ticket_lock"));
    packageUnlock=reinterpret_cast<TicketLock>(solve("_lck_ticket_unlock"));
    if (!package || !xcpmMode || !hwp || !paused || !configured || !assertions || !nativeCpuBase || !packageLock || !packageUnlock) {
        setStatus("missing-kernel-symbol");return;
    }
    packageBase=reinterpret_cast<void *>(package);
    appleMinimum=reinterpret_cast<volatile uint32_t *>(package+profile::PackageMinimumOffset);
    appleMaximum=reinterpret_cast<volatile uint32_t *>(package+profile::PackageMaximumOffset);
    KernelPatcher::RouteRequest routes[]={
        {nullptr,writeHook,originalWriter},{"_xcpm_timer",timerHook,originalTimer}};
    routes[0].from=writer;routes[1].from=timer;
    for (const auto &route:routes) if (!e5mac::shortJumpFits(route.from,route.to)) {
        setStatus("short-route-out-of-range");return;
    }
    // Short routing copies only the verified non-RIP-relative prologues.
    // Long routing would copy a RIP-relative instruction in the writer prologue.
    if (!patcher.routeMultipleShort(KernelPatcher::KernelID,routes,2)) {
        patcher.clearError();setStatus("route-failed-pass-through");return;
    }
    store(hooks,uint32_t(1));setStatus(load(serviceReady)?"observing":"hooks-installed-awaiting-service");
}
} // namespace adapter

static const char *offArguments[]={"-e5pmoff"};
static const char *debugArguments[]={"-e5pmdbg"};
PluginConfiguration ADDPR(config)={
    "E5PStateMac",parseModuleVersion(xStringify(MODULE_VERSION)),LiluAPI::AllowNormal,
    offArguments,1,debugArguments,1,nullptr,0,KernelVersion::Tahoe,KernelVersion::Tahoe,
    []() {
#ifdef E5_SHADOW_ONLY
        int requested=1;
#else
        int requested=2;
#endif
        lilu_get_boot_args("e5pm",&requested,sizeof(requested));
#ifdef E5_SHADOW_ONLY
        if (requested==2) requested=1;
#endif
        if (requested!=1 && requested!=2) {
            adapter::setStatus("disabled-no-e5pm-argument");ADDPR(startSuccess)=false;return;
        }
        adapter::store(adapter::mode,static_cast<uint32_t>(requested));
        if (lilu.onPatcherLoad(adapter::patch)!=LiluAPI::Error::NoError) adapter::setStatus("patcher-registration-failed");
    }};

bool E5PStateMac::init(OSDictionary *dictionary) {
    if (!IOService::init(dictionary)) return false;
    loop_=nullptr;timer_=nullptr;notifier_=nullptr;timerAdded_=false;return true;
}
bool E5PStateMac::start(IOService *provider) {
    if (!ADDPR(startSuccess) || !adapter::load(adapter::mode)) return false;
    if (!IOService::start(provider)) return false;
    loop_=IOWorkLoop::workLoop();
    timer_=IOTimerEventSource::timerEventSource(this,poll);
    if (!loop_ || !timer_) {cleanup();return false;}
    if (loop_->addEventSource(timer_)!=kIOReturnSuccess) {cleanup();return false;}
    timerAdded_=true;
    notifier_=registerPrioritySleepWakeInterest(power,this);
    if (!notifier_) {cleanup();return false;}
    adapter::store(adapter::suspended,uint32_t(0));adapter::store(adapter::serviceReady,uint32_t(1));
    adapter::store(adapter::warmupStartNs,adapter::nowNs());
    if (adapter::load(adapter::hooks)) adapter::setStatus("observing");
    setProperty("Version",xStringify(MODULE_VERSION));setProperty("SupportedBuild",profile::Build);
    setProperty("ProfileSource","BootKernelExtensions.kc");
    setProperty("BootKCSHA256Expected","96c12f421a8e99580b5015dfd39877df38ecde9b9e056b8ccd28f15847bca35c");
#ifdef E5_SHADOW_ONLY
    setProperty("ShadowOnlyBuild",kOSBooleanTrue);
#else
    setProperty("ShadowOnlyBuild",kOSBooleanFalse);
#endif
    setProperty("NoHotUnload",kOSBooleanTrue);
    setProperty("LinuxIOWaitSignalAvailable",kOSBooleanFalse);
    setProperty("UrgencySource","native-xcpm-class-diagnostic-only");
    setProperty("UrgencyHookInstalled",kOSBooleanFalse);
    setProperty("NativeClassDiagnosticOnly",kOSBooleanTrue);
    setProperty("NativeDeadlinesUnchanged",kOSBooleanTrue);
    setProperty("DeadlineHookInstalled",kOSBooleanFalse);
    setProperty("FrequencyControlCompiledIn",bool(
#ifdef E5_GUARDED_CONTROL
        true
#else
        false
#endif
    ));
    publish();registerService();timer_->setTimeoutMS(1000);return true;
}
static void dictionaryNumber(OSDictionary *dictionary,const char *name,uint64_t value) {
    auto *number=OSNumber::withNumber(value,64);
    if (number) {dictionary->setObject(name,number);number->release();}
}
void E5PStateMac::publish() {
    setProperty("PublishedUptimeNs",adapter::nowNs(),64);
    setProperty("Mode",adapter::load(adapter::mode),32);
    setProperty("CounterEpoch",adapter::load(adapter::epoch),32);
    const bool armed=adapter::control();
    const char *state=adapter::load(adapter::status);
    if (adapter::load(adapter::hooks)) {
        if (adapter::load(adapter::fault)) state="fault-native-pass-through";
        else if (!adapter::enabled()) state="paused-native-pass-through";
        else if (adapter::load(adapter::mode)==1) state="observing";
        else state=armed?"controlling":"selftest-native-pass-through";
    }
    setProperty("Status",state);
    setProperty("HooksInstalled",bool(adapter::load(adapter::hooks)));
    setProperty("ControlActive",armed);
    setProperty("SelfTestReadyMask",adapter::readyMask(),32);
    setProperty("SelfTestRequiredMask",e5mac::AllCpusMask,32);
    setProperty("SelfTestSamplesPerCPU",e5mac::RequiredGoodSamples,32);
    setProperty("WarmupMinimumSeconds",e5mac::WarmupNs/1000000000,64);
    setProperty("WarmupStartNs",adapter::load(adapter::warmupStartNs),64);
    setProperty("Suspended",bool(adapter::load(adapter::suspended)));
    setProperty("Fault",adapter::load(adapter::fault),32);
    setProperty("KernelMajorObserved",static_cast<uint32_t>(adapter::diagnostics.kernelMajor),32);
    setProperty("KernelMinorObserved",static_cast<uint32_t>(adapter::diagnostics.kernelMinor),32);
    setProperty("CPUSignatureRaw",adapter::diagnostics.signature,32);
    setProperty("CPUID6EAX",adapter::diagnostics.leaf6a,32);
    setProperty("CPUID6ECX",adapter::diagnostics.leaf6c,32);
    setProperty("CPUIDExtendedMaximum",adapter::diagnostics.extendedMaximum,32);
    setProperty("CPUIDExtended7EDX",adapter::diagnostics.extended7d,32);
    setProperty("CPUBrandObserved",adapter::diagnostics.brand);
    setProperty("IdentityAccepted",adapter::diagnostics.identityAccepted);
    setProperty("UrgencyBodyVerified",adapter::diagnostics.urgencyBodyVerified);
    setProperty("ExistingUrgencyEntryRouted",adapter::diagnostics.urgencyEntryRouted);
    setProperty("UrgencyNormalizedFingerprint",adapter::diagnostics.urgencyNormalized,64);
    setProperty("MinimumOperatingRatio",adapter::diagnostics.operatingMinimum,32);
    setProperty("LinuxMinimumRatio",adapter::hardware.minimum,32);
    setProperty("NominalRatio",adapter::hardware.nominal,32);
    setProperty("OneCoreTurboRatio",adapter::hardware.turbo,32);
    setProperty("AllCoreTurboRatio",adapter::diagnostics.allCoreTurbo,32);
    const char *rawNames[]={"MSR35Raw","MSRCERaw","MSR1ADRaw","MSR1AERaw"};
    const char *errorNames[]={"MSR35ReadError","MSRCEReadError","MSR1ADReadError","MSR1AEReadError"};
    const char *fingerprintNames[]={"WriterFingerprintObserved","TimerFingerprintObserved","UrgencyFingerprintObserved","DeadlineFingerprintObserved"};
    for (unsigned i=0;i<4;++i) {
        setProperty(rawNames[i],adapter::diagnostics.msrs[i],64);
        setProperty(errorNames[i],static_cast<uint32_t>(adapter::diagnostics.errors[i]),32);
        setProperty(fingerprintNames[i],adapter::diagnostics.fingerprints[i],64);
    }
    if (adapter::assertions) setProperty("XCPMAssertionsEnabled",bool(*adapter::assertions));
    if (adapter::configured) setProperty("XCPMConfigured",bool(*adapter::configured));
    if (adapter::appleMinimum) setProperty("AppleMinimumRatio",*adapter::appleMinimum,32);
    if (adapter::appleMaximum) setProperty("AppleMaximumRatio",*adapter::appleMaximum,32);
    auto *rows=OSArray::withCapacity(20);if (!rows) return;
    for (unsigned i=0;i<adapter::MaxCpus;++i) {
        auto &cpu=adapter::cpus[i];
        if (!adapter::load(cpu.writerCallbacks) && !adapter::load(cpu.timerCallbacks)) continue;
        auto *row=OSDictionary::withCapacity(12);if (!row) break;
        dictionaryNumber(row,"cpu",i);dictionaryNumber(row,"active",adapter::load(cpu.active));
        dictionaryNumber(row,"apple_ratio",adapter::load(cpu.appleRatio));
        dictionaryNumber(row,"policy_ratio",adapter::load(cpu.policyRatio));
        dictionaryNumber(row,"applied_argument_ratio",adapter::load(cpu.appliedRatio));
        dictionaryNumber(row,"busy_q8",adapter::load(cpu.busyQ8));
        dictionaryNumber(row,"busy_mhz_estimate",adapter::load(cpu.mhz));
        dictionaryNumber(row,"samples",adapter::load(cpu.samples));dictionaryNumber(row,"controlled_writes",adapter::load(cpu.writes));
        dictionaryNumber(row,"last_sample_ns",adapter::load(cpu.lastSampleNs));
        dictionaryNumber(row,"writer_callbacks",adapter::load(cpu.writerCallbacks));
        dictionaryNumber(row,"timer_callbacks",adapter::load(cpu.timerCallbacks));
        dictionaryNumber(row,"native_class",adapter::load(cpu.nativeClass));
        dictionaryNumber(row,"native_state_reads",adapter::load(cpu.nativeStateReads));
        dictionaryNumber(row,"request_ratio",adapter::load(cpu.requestRatio));
        dictionaryNumber(row,"decision_valid",adapter::load(cpu.decisionValid));
        dictionaryNumber(row,"good_samples",adapter::load(cpu.goodSamples));
        dictionaryNumber(row,"qualified_epoch",adapter::load(cpu.qualifiedEpoch));
        dictionaryNumber(row,"thermal_flags",adapter::load(cpu.thermalFlags));
        dictionaryNumber(row,"last_policy_ns",adapter::load(cpu.lastPolicyNs));
        dictionaryNumber(row,"invalid_samples",adapter::load(cpu.invalidSamples));
        dictionaryNumber(row,"gap_resets",adapter::load(cpu.gapResets));
        dictionaryNumber(row,"readback_verified_writes",adapter::load(cpu.readbackWrites));
        dictionaryNumber(row,"fallback_writes",adapter::load(cpu.fallbackWrites));
        rows->setObject(row);row->release();
    }
    setProperty("CPUs",rows);rows->release();
}
void E5PStateMac::poll(OSObject *owner,IOTimerEventSource *sender) {
    auto *self=OSDynamicCast(E5PStateMac,owner);
    if (self) {self->publish();sender->setTimeoutMS(1000);}
}
IOReturn E5PStateMac::power(void *,void *,UInt32 message,IOService *,void *argument,vm_size_t) {
    if (message==kIOMessageSystemWillSleep || message==kIOMessageSystemWillPowerOff || message==kIOMessageSystemWillRestart) {
        adapter::store(adapter::suspended,uint32_t(1));
        for (auto &cpu:adapter::cpus) adapter::store(cpu.active,uint32_t(0));
        if (message==kIOMessageSystemWillSleep && argument) {
            auto *note=static_cast<sleepWakeNote *>(argument);note->returnValue=0;
            acknowledgeSleepWakeNotification(note->powerRef);
        }
    } else if (message==kIOMessageSystemHasPoweredOn || message==kIOMessageSystemWillNotSleep) {
        __atomic_fetch_add(&adapter::epoch,1,__ATOMIC_ACQ_REL);
        adapter::store(adapter::warmupStartNs,adapter::nowNs());
        adapter::store(adapter::suspended,uint32_t(0));
    }
    return kIOReturnSuccess;
}
void E5PStateMac::cleanup() {
    adapter::store(adapter::serviceReady,uint32_t(0));adapter::store(adapter::suspended,uint32_t(1));
    if (notifier_) {notifier_->remove();notifier_=nullptr;}
    if (timer_) {timer_->disable();timer_->cancelTimeout();}
    if (loop_ && timer_ && timerAdded_) loop_->removeEventSource(timer_);
    timerAdded_=false;
    if (timer_) {timer_->release();timer_=nullptr;}
    if (loop_) {loop_->release();loop_=nullptr;}
}
void E5PStateMac::stop(IOService *provider) {cleanup();IOService::stop(provider);}
void E5PStateMac::free() {cleanup();IOService::free();}
