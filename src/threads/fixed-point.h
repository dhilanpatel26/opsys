#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <debug.h>
#include <stdint.h>
#include <stddef.h>

#define P 17
#define Q 14
#define F (1 << Q)

typedef int32_t fixedpoint_t; // signed 2's complement

fixedpoint_t convert_to_fixedpoint(int n);
int convert_to_integer_zero(fixedpoint_t x);
int convert_to_integer(fixedpoint_t x);
fixedpoint_t fp_add(fixedpoint_t x, fixedpoint_t y);
fixedpoint_t fp_sub(fixedpoint_t x, fixedpoint_t y);
fixedpoint_t fp_add_int(fixedpoint_t x, int n);
fixedpoint_t fp_sub_int(fixedpoint_t x, int n);
fixedpoint_t fp_mul(fixedpoint_t x, fixedpoint_t y);
fixedpoint_t fp_mul_int(fixedpoint_t x, int n);
fixedpoint_t fp_div(fixedpoint_t x, fixedpoint_t y);
fixedpoint_t fp_div_int(fixedpoint_t x, int n);

#endif /* threads/fixed-point.h */