#include "f16.h"
#include <string.h>

static uint32_t bits_of(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float float_of(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

uint16_t f16_from_float(float f)
{
    uint32_t x = bits_of(f);
    uint32_t sign = (x >> 16) & 0x8000u;
    uint32_t exp = (x >> 23) & 0xffu;
    uint32_t man = x & 0x7fffffu;

    if (exp == 0xffu)                                   /* inf or nan */
        return (uint16_t)(sign | 0x7c00u | (man ? 0x200u : 0u));

    int e = (int)exp - 127 + 15;
    if (e >= 31) return (uint16_t)(sign | 0x7c00u);     /* overflow to inf */

    if (e <= 0) {                                       /* subnormal or zero */
        if (e < -10) return (uint16_t)sign;
        man |= 0x800000u;
        int shift = 14 - e;
        uint32_t half = 1u << (shift - 1);
        uint32_t rem = man & ((1u << shift) - 1u);
        uint32_t r = man >> shift;
        if (rem > half || (rem == half && (r & 1u))) r++;
        return (uint16_t)(sign | r);
    }

    uint32_t r = ((uint32_t)e << 10) | (man >> 13);
    uint32_t rem = man & 0x1fffu;
    if (rem > 0x1000u || (rem == 0x1000u && (r & 1u))) r++;   /* may carry into exponent: fine */
    return (uint16_t)(sign | r);
}

float f16_to_float(uint16_t h)
{
    uint32_t sign = ((uint32_t)h & 0x8000u) << 16;
    uint32_t exp = ((uint32_t)h >> 10) & 0x1fu;
    uint32_t man = (uint32_t)h & 0x3ffu;

    if (exp == 0x1fu) return float_of(sign | 0x7f800000u | (man << 13));
    if (exp == 0) {
        if (man == 0) return float_of(sign);
        /* subnormal: value = man * 2^-24 */
        float v = (float)man * (1.0f / 16777216.0f);
        return sign ? -v : v;
    }
    return float_of(sign | ((exp - 15 + 127) << 23) | (man << 13));
}
