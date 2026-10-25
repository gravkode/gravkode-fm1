/* SPDX-License-Identifier: MPL-2.0 */
/* Core identity without pulling the interrupt/exception implementation into DSP units. */
#pragma once
#include <stdint.h>
static inline uint32_t fm1_cnum(void)
{
    uint32_t v;
    __asm__ volatile("%0 = cnum" : "=r"(v));
    return v;
}
