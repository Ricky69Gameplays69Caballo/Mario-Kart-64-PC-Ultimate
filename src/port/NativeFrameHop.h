#ifndef SPAGHETTIKART_NATIVE_FRAME_HOP_H
#define SPAGHETTIKART_NATIVE_FRAME_HOP_H

// Fractional extension of the legacy hop recurrence, before clamps/collisions:
// acceleration -= jerk; velocity += acceleration; displacement += velocity.
typedef struct NativeFrameHop {
    double acceleration, velocity, displacement;
} NativeFrameHop;

static inline NativeFrameHop NativeFrameHop_Advance(double acceleration, double velocity,
                                                   double jerk, double ticks) {
    NativeFrameHop result;
    result.acceleration = acceleration - jerk * ticks;
    result.velocity = velocity + acceleration * ticks - jerk * ticks * (ticks + 1.0) * 0.5;
    result.displacement = velocity * ticks + acceleration * ticks * (ticks + 1.0) * 0.5 -
                          jerk * ticks * (ticks + 1.0) * (ticks + 2.0) / 6.0;
    return result;
}

// Stop at the first crossing of zero upward velocity, rather than integrating
// a negative tail whose size depends on the length of the current frame.
// This handles the usual hop (nonnegative jerk); acceleration/velocity ceiling
// crossings require separate handling.
static inline NativeFrameHop NativeFrameHop_AdvanceToApex(double acceleration, double velocity,
                                                         double jerk, double ticks, int* stopped) {
    NativeFrameHop result = NativeFrameHop_Advance(acceleration, velocity, jerk, ticks);
    *stopped = 0;
    if (ticks > 0.0 && velocity <= 0.0 && result.velocity <= 0.0) {
        result.acceleration = result.velocity = result.displacement = 0.0;
        *stopped = 1;
    } else if (velocity > 0.0 && jerk >= 0.0 && result.velocity <= 0.0) {
        double low = 0.0, high = ticks;
        for (int i = 0; i < 48; ++i) {
            const double mid = (low + high) * 0.5;
            if (NativeFrameHop_Advance(acceleration, velocity, jerk, mid).velocity > 0.0) low = mid;
            else high = mid;
        }
        result = NativeFrameHop_Advance(acceleration, velocity, jerk, (low + high) * 0.5);
        result.acceleration = result.velocity = 0.0;
        *stopped = 1;
    }
    return result;
}

#endif
