#ifndef SPAGHETTIKART_NATIVE_FRAME_TIMERS_H
#define SPAGHETTIKART_NATIVE_FRAME_TIMERS_H

#include <math.h>

// Return true for a periodic button pulse when a step is inside its pulse
// window or crossed the start of a window. The latter avoids losing an item
// press when a frame is longer than the pulse window.
static inline int NativeFramePeriodicPulse(double previous, double current, double offset,
                                           double period, double width) {
    double shifted = current + offset;
    double phase = fmod(shifted, period);
    return phase < width || floor(shifted / period) > floor((previous + offset) / period);
}

#endif
