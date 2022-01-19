#pragma once
#include <stdint.h>

uint32_t popcount32(uintmax_t num) {
  uint32_t precision = 0;
  while (num != 0) {
    if (1 == (num & 1)) {
      precision++;
    }
    num >>= 1;
  }
  return precision;
}

uint32_t count_leading_zeros(const uint32_t x) {
  uint32_t y;
  // gcc or Vitis HLS only
  y = __builtin_clz(x);
  return (x == 0) ? 31 : y;
}
