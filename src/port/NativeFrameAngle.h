#ifndef SPAGHETTIKART_NATIVE_FRAME_ANGLE_H
#define SPAGHETTIKART_NATIVE_FRAME_ANGLE_H

#include <stdint.h>

// Angles are stored as integer units. Carry the fractional remainder so
// repeated small simulation steps don't lose slow rotations to truncation.
static inline int32_t NativeFrameAngleStep(int32_t delta, double ticks, double* remainder) {
    const double value = delta * ticks + *remainder;
    const int32_t whole = (int32_t)value;
    *remainder = value - whole;
    return whole;
}

#endif
