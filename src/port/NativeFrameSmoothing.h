#ifndef SPAGHETTIKART_NATIVE_FRAME_SMOOTHING_H
#define SPAGHETTIKART_NATIVE_FRAME_SMOOTHING_H
#include <math.h>

// Convert a legacy per-tick exponential smoothing coefficient to a
// fractional timestep without changing the one-tick coefficient.
static inline double NativeFrameSmoothingWeight(double weight, double ticks) {
    if (ticks == 1.0) return weight;
    if (ticks <= 0.0 || weight <= 0.0) return 0.0;
    if (weight >= 1.0) return 1.0;
    return -expm1(log1p(-weight) * ticks);
}

#endif
