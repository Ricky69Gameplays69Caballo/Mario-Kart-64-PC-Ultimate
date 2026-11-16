#ifndef SPAGHETTIKART_NATIVE_FRAME_ACCELERATION_H
#define SPAGHETTIKART_NATIVE_FRAME_ACCELERATION_H

#include <math.h>
#include <float.h>
#include <stdbool.h>

// The experimental solver handles upward band crossings only. Negative or
// non-finite rates require a separate solver; callers should retain their
// existing behavior for those inputs.
static inline bool NativeFrameAcceleration_CanIntegrate(double speed, double topSpeed, const double rates[10], double ticks) {
    if (!isfinite(speed) || !isfinite(topSpeed) || !isfinite(ticks) || speed < 0.0 || topSpeed <= 0.0 || ticks <= 0.0)
        return false;
    for (int i = 0; i < 10; ++i) if (!isfinite(rates[i]) || rates[i] < 0.0) return false;
    return true;
}

// Integrate piecewise rates through speed-band boundaries. The original
// sequential checks grant a full increment from the next band when a step
// crosses a boundary; model that as a one-time crossing impulse.
static inline double NativeFrameAcceleration_Advance(double speed, double topSpeed, const double rates[10], double ticks) {
    if (speed < 0.0 || topSpeed <= 0.0 || ticks <= 0.0 || speed > topSpeed) return speed;
    for (int transitions = 0; transitions < 11 && ticks > 0.0 && speed <= topSpeed; ++transitions) {
        int band = (int)(speed * 10.0 / topSpeed);
        if (band > 9) band = 9;
        double rate = rates[band];
        if (rate <= 0.0) return speed + rate * ticks;
        // Callers store speed as float after each frame; allow for that
        // rounding when deciding whether a band boundary was reached.
        const double boundaryTolerance = (2.0 * FLT_EPSILON * topSpeed) / rate;
        if (band == 9) {
            const double toTop = (topSpeed - speed) / rate;
            if (toTop > ticks + boundaryTolerance) return speed + rate * ticks;
            // Preserve a single final-band overshoot even if the crossing
            // happens during a fractional update. The ordinary driving path
            // subsequently clamps to top speed as before.
            return topSpeed + rate;
        }
        double boundary = topSpeed * (band + 1) / 10.0;
        double toBoundary = (boundary - speed) / rate;
        if (toBoundary > ticks + boundaryTolerance) return speed + rate * ticks;
        ticks = toBoundary >= ticks ? 0.0 : ticks - toBoundary;
        speed = boundary + rates[band + 1];
    }
    return speed;
}

#endif
