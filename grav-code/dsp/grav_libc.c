/* The C library functions the DSP calls that the FM-1's link lacks: it has no libc, only felucca-bsp's memset, memcpy
 * and memcmp (chip/libc.c), and no libm (the compiler makes FPU instructions of the rest). The host and the web
 * link their own. */
#ifndef PLAT_HOST
#include <stddef.h>
#include <string.h>
#include "fastmath.h"

float fabsf(float x)                     /* Plaits' Virtual Analog, stmlib */
{
    return fm_fabsf(x);
}

float sqrtf(float x)                     /* stmlib's Sqrt (its portable code, -DTEST): Plaits' noise and others */
{
    return fm_sqrtf(x);                  /* (relative error 2e-7: include/fastmath.h) */
}

void *memmove(void *d, const void *s, size_t n)   /* Plaits' chords (its string synth oscillator) */
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (p <= q || p >= q + n)
        return memcpy(d, s, n);          /* forward is safe: felucca-bsp's memcpy goes forward */
    while (n--)
        p[n] = q[n];                     /* overlapping, the destination above: from the end */
    return d;
}
#endif
