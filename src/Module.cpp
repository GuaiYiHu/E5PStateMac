// SPDX-License-Identifier: BSD-2-Clause
#include <Headers/plugin_start.hpp>
#include <mach/mach_types.h>
#include <mach/kmod.h>
extern "C" {
extern kern_return_t ADDPR(kern_start)(kmod_info_t *,void *);
extern kern_return_t ADDPR(kern_stop)(kmod_info_t *,void *);
extern kern_return_t _start(kmod_info_t *,void *);
extern kern_return_t _stop(kmod_info_t *,void *);
KMOD_EXPLICIT_DECL(local.experimental.E5PStateMac,xStringify(MODULE_VERSION),_start,_stop)
__private_extern__ kmod_start_func_t *_realmain=ADDPR(kern_start);
__private_extern__ kmod_stop_func_t *_antimain=ADDPR(kern_stop);
__private_extern__ int _kext_apple_cc=__APPLE_CC__;
}
