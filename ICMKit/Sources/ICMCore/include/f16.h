#ifndef ICMCORE_F16_H
#define ICMCORE_F16_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IEEE 754 binary16 conversion in plain C (round to nearest even). */
uint16_t f16_from_float(float f);
float f16_to_float(uint16_t h);

#ifdef __cplusplus
}
#endif

#endif
