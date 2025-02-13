#include "threads/fixed-point.h"


fixedpoint_t convert_to_fixedpoint(int n) {
    return n * F;
}

int convert_to_integer_zero(fixedpoint_t x) {
    return x / F;
}

int convert_to_integer(fixedpoint_t x) {
    return x >= 0 ? (x + F / 2) / F : (x - F / 2) / F;
}

fixedpoint_t fp_add(fixedpoint_t x, fixedpoint_t y) {
    return x + y;
}
fixedpoint_t fp_sub(fixedpoint_t x, fixedpoint_t y) {
    return x - y;
}

fixedpoint_t fp_add_int(fixedpoint_t x, int n) {
    return x + n * F;
}

fixedpoint_t fp_sub_int(fixedpoint_t x, int n) {
    return x - n * F;
}

fixedpoint_t fp_mul(fixedpoint_t x, fixedpoint_t y) {
    return ((int64_t) x) * y / F;
}

fixedpoint_t fp_mul_int(fixedpoint_t x, int n) {
    return x * n;
}

fixedpoint_t fp_div(fixedpoint_t x, fixedpoint_t y) {
    return ((int64_t) x) * F / y;
}

fixedpoint_t fp_div_int(fixedpoint_t x, int n) {
    return x / n;
}