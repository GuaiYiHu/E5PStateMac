// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#if defined(E5_PROFILE_SEQUOIA_24H232) && defined(E5_PROFILE_TAHOE_25G332)
#error "Select exactly one reviewed kernel profile"
#elif defined(E5_PROFILE_SEQUOIA_24H232)
#include "profiles/Sequoia24H232.hpp"
#elif defined(E5_PROFILE_TAHOE_25G332)
#include "profiles/Tahoe25G332.hpp"
#else
#error "No reviewed kernel profile selected"
#endif
