#ifndef SPAGHETTIKART_NATIVE_FRAME_CONTACT_H
#define SPAGHETTIKART_NATIVE_FRAME_CONTACT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct NativeFrameContact {
    const void* actor;
    size_t tick;
    bool valid;
} NativeFrameContact;

// First contact is immediate, including between legacy ticks. Repeated contact
// with the same actor emits at most once within each legacy tick.
static inline bool NativeFrameContact_Allow(NativeFrameContact* contact, const void* actor, size_t tick) {
    if (contact->valid && contact->actor == actor && contact->tick == tick) return false;
    contact->actor = actor;
    contact->tick = tick;
    contact->valid = true;
    return true;
}

#endif
