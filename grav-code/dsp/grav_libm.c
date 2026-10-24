/* The C math library functions the DSP calls that the FM-1's link lacks (it has no libm: the compiler makes FPU
 * instructions of the rest). The host and the web link their own libm. */
#ifndef PLAT_HOST
#include "fastmath.h"

float fabsf(float x)                     /* Plaits' Virtual Analog, stmlib */
{
    return fm_fabsf(x);
}
#endif
