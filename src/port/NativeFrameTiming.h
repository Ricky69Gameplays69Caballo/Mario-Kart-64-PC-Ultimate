#ifndef SPAGHETTIKART_NATIVE_FRAME_TIMING_H
#define SPAGHETTIKART_NATIVE_FRAME_TIMING_H

// Estimate a line crossing within the current simulation step. Distances are
// measured on opposite sides of the line; preserving the legacy expression
// at scale 1 avoids changing current race timing.
static inline float NativeFrameCrossingTime(float raceTime, float stepSeconds, float scale,
                                            float afterLine, float beforeLine) {
    if (scale == 1.0f)
        return raceTime - ((stepSeconds * afterLine) / (afterLine + beforeLine));
    return raceTime - (((stepSeconds * scale) * afterLine) / (afterLine + beforeLine));
}

#endif
