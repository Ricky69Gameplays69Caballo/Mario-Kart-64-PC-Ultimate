#ifndef SPAGHETTIKART_NATIVE_FRAME_CLOCK_H
#define SPAGHETTIKART_NATIVE_FRAME_CLOCK_H

// Experimental timing foundation for a variable-rate simulation. This does
// not enable it: callers still need to separate continuous motion from legacy
// discrete 60 Hz events and integrate both into the game loop.
#include <stdint.h>

#define NATIVE_FRAME_NANOSECONDS_PER_SECOND UINT64_C(1000000000)
#define NATIVE_FRAME_LEGACY_TICKS_PER_SECOND UINT64_C(60)

// Number of discrete legacy tick boundaries in the current simulation step.
// Remains one in the normal game until the variable-rate loop is connected.
extern uint32_t gNativeSimulationLegacyTicks;
// 30 Hz menu/visual update boundaries crossed this presentation.
extern uint32_t gNativeVisualSteps;
extern float gNativeVisualPhase;

typedef struct NativeFrameClock {
    // Legacy-tick phase numerator; denominator is nanoseconds per second.
    uint64_t legacy_phase;
    // Elapsed time awaiting simulation after a long presentation frame.
    uint64_t pending_ns;
} NativeFrameClock;

typedef struct NativeFrameStep {
    // Continuous simulation duration in units of a legacy 60 Hz update.
    double legacy_tick_fraction;
    // Number of 60 Hz boundaries crossed; never silently skip timer events.
    uint64_t legacy_ticks;
} NativeFrameStep;

static inline void NativeFrameClock_Reset(NativeFrameClock* clock) {
    clock->legacy_phase = 0;
    clock->pending_ns = 0;
}

// Limit any one simulation step while retaining, rather than discarding,
// elapsed time. Overflow is saturated so a corrupt interval cannot wrap.
static inline uint64_t NativeFrameClock_BoundedElapsed(NativeFrameClock* clock, uint64_t elapsed_ns,
                                                       uint64_t max_step_ns) {
    if (UINT64_MAX - clock->pending_ns < elapsed_ns) clock->pending_ns = UINT64_MAX;
    else clock->pending_ns += elapsed_ns;
    const uint64_t step_ns = clock->pending_ns < max_step_ns ? clock->pending_ns : max_step_ns;
    clock->pending_ns -= step_ns;
    return step_ns;
}

static inline NativeFrameStep NativeFrameClock_Advance(NativeFrameClock* clock, uint64_t elapsed_ns) {
    const uint64_t seconds = elapsed_ns / NATIVE_FRAME_NANOSECONDS_PER_SECOND;
    const uint64_t nanos = elapsed_ns % NATIVE_FRAME_NANOSECONDS_PER_SECOND;
    const uint64_t phase = clock->legacy_phase + nanos * NATIVE_FRAME_LEGACY_TICKS_PER_SECOND;
    NativeFrameStep step;
    step.legacy_tick_fraction = (double)elapsed_ns *
                                ((double)NATIVE_FRAME_LEGACY_TICKS_PER_SECOND /
                                 (double)NATIVE_FRAME_NANOSECONDS_PER_SECOND);
    step.legacy_ticks = seconds * NATIVE_FRAME_LEGACY_TICKS_PER_SECOND +
                        phase / NATIVE_FRAME_NANOSECONDS_PER_SECOND;
    clock->legacy_phase = phase % NATIVE_FRAME_NANOSECONDS_PER_SECOND;
    return step;
}

#endif
