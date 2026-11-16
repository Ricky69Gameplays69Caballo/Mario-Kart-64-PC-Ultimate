#ifndef SPAGHETTIKART_NATIVE_FRAME_MOTION_H
#define SPAGHETTIKART_NATIVE_FRAME_MOTION_H

#include <math.h>

// Experimental primitive for a constant-force, linear-drag component of
// the legacy integrator. Callers must separately handle collisions, changing
// forces and integer state machines; this is not yet used by the game loop.
// Legacy step: position += velocity; velocity = drag * velocity + impulse.
typedef struct NativeFrameMotion {
    double position;
    double velocity;
} NativeFrameMotion;

// A fractional explicit-Euler step can move upwards under downward gravity
// immediately after contact. Limit only this grounded numerical artifact;
// retain normal airborne, jump, wall and ceiling motion.
static inline double NativeFrameMotion_GroundedVerticalDisplacement(double displacement, float groundDistance,
                                                                     float normalY, float oldVelocity,
                                                                     float newVelocity, float hopVelocity) {
    if (groundDistance <= 0.0f && normalY > 0.5f && oldVelocity <= 0.0f && newVelocity <= 0.0f &&
        hopVelocity == 0.0f && displacement > 0.0) return 0.0;
    return displacement;
}

static inline NativeFrameMotion NativeFrameMotion_Advance(NativeFrameMotion state, double drag,
                                                         double impulse, double ticks) {
    const double e = drag - 1.0;
    double geometric, weighted;
    if (fabs(e) < 0.00001) {
        // Binomial expansion avoids cancellation when drag is close to one,
        // as it usually is in the original kart physics.
        const double c2 = ticks * (ticks - 1.0) * 0.5;
        const double c3 = c2 * (ticks - 2.0) / 3.0;
        const double c4 = c3 * (ticks - 3.0) / 4.0;
        const double c5 = c4 * (ticks - 4.0) / 5.0;
        geometric = ticks + e * (c2 + e * (c3 + e * (c4 + e * c5)));
        weighted = c2 + e * (c3 + e * (c4 + e * c5));
    } else {
        // Require positive drag: negative/zero factors do not have a real,
        // continuously divisible extension and need their own discrete path.
        geometric = expm1(ticks * log(drag)) / e;
        weighted = (geometric - ticks) / e;
    }
    state.position += state.velocity * geometric + impulse * weighted;
    state.velocity += e * state.velocity * geometric + impulse * geometric;
    return state;
}

#endif
