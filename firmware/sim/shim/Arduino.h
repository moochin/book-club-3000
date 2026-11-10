// Minimal Arduino shim for the host UI simulator.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
uint32_t millis();
template <class T, class L, class H> auto constrain(T x, L lo, H hi) -> T {
  return x < (T)lo ? (T)lo : (x > (T)hi ? (T)hi : x);
}
using std::max;
using std::min;
struct SimEsp { uint32_t getFreeHeap() { return 182 * 1024; } };
extern SimEsp ESP;
