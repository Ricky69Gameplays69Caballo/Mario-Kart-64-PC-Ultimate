#ifndef SPAGHETTIKART_NATIVE_FRAME_SPLINE_H
#define SPAGHETTIKART_NATIVE_FRAME_SPLINE_H

#include <math.h>

// Fractional extension of p_next = p + (second - first) * p + first.
// This handles progress within one segment; callers must split a step at
// segment and cutscene-event boundaries rather than extrapolate across them.
static inline double NativeFrameSpline_Advance(double progress, double first, double second, double ticks) {
    const double difference = second - first;
    if (ticks == 0.0) return progress;
    if (ticks == 1.0) return progress + difference * progress + first;
    if (difference == 0.0) return progress + first * ticks;
    const double growth = expm1(log1p(difference) * ticks);
    return progress + growth * (progress + first / difference);
}

// Advance at most to the end of the current segment. Return the portion of
// the supplied time left for the next segment. Infinite-time endpoints (for
// example, a speed that approaches zero) never falsely trigger a crossing.
static inline double NativeFrameSpline_AdvanceSegment(double* progress, double first, double second, double ticks) {
    if (ticks <= 0.0) return 0.0;
    if (*progress >= 1.0) { *progress = 1.0; return ticks; }
    const double next = NativeFrameSpline_Advance(*progress, first, second, ticks);
    if (next < 1.0) { *progress = next; return 0.0; }
    const double difference = second - first;
    const double rate = difference * *progress + first;
    if (rate <= 0.0) return 0.0;
    double toBoundary;
    if (difference == 0.0) toBoundary = (1.0 - *progress) / rate;
    else toBoundary = log1p(difference * (1.0 - *progress) / rate) / log1p(difference);
    if (!isfinite(toBoundary) || toBoundary > ticks) { *progress = next; return 0.0; }
    if (toBoundary < 0.0) toBoundary = 0.0;
    *progress = 1.0;
    return ticks - toBoundary;
}

#endif
