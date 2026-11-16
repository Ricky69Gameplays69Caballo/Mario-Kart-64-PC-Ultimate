#ifndef SPAGHETTIKART_NATIVE_FRAME_COLLISION_H
#define SPAGHETTIKART_NATIVE_FRAME_COLLISION_H

#include <stdbool.h>

// A near-horizontal floor and close contact indicate the small negative
// pitch observed from sub-frame ground jitter, not a downhill course slope.
static inline bool NativeFrameCollision_FlatGroundContact(int enabled, int slope,
                                                         float normalY, float distance) {
    return enabled && slope > -182 && normalY >= 0.99999f && distance >= -0.25f && distance <= 0.25f;
}

#endif
