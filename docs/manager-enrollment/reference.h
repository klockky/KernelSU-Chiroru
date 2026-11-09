/* SPDX-License-Identifier: GPL-2.0 */
/* Delliaf: host-only enrollment policy example, NOT a kernel implementation. */
#ifndef KSU_ENROLLMENT_REFERENCE_H
#define KSU_ENROLLMENT_REFERENCE_H

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>

#define REF_USER_RANGE UINT32_C(100000)
#define REF_FIRST_APP_ID UINT32_C(10000)
#define REF_LAST_APP_ID UINT32_C(19999)
#define REF_NO_MANAGER UINT32_MAX

struct ref_caller {
    uint32_t real_uid;
    uint32_t effective_uid;
};

struct ref_enrollment {
    uint64_t generation;
    uint32_t app_id;
    bool pending;
    bool exhausted;
};

static inline bool ref_privileged(struct ref_caller caller)
{
    /* Production must read credentials from current, never from the request. */
    return caller.real_uid == 0 && caller.effective_uid == 0;
}

static inline void ref_init(struct ref_enrollment *state)
{
    *state = (struct ref_enrollment){ .app_id = REF_NO_MANAGER };
}

/* Called for a relevant package change or module reset, under one owner/lock.
 * This model does NOT detect events or serialize actual kernel callbacks. */
static inline int ref_invalidate(struct ref_enrollment *state)
{
    state->app_id = REF_NO_MANAGER;
    state->pending = false;
    if (state->exhausted || state->generation == UINT64_MAX) {
        state->exhausted = true;
        return -EOVERFLOW;
    }
    ++state->generation;
    return 0;
}

/* Begin BEFORE the trusted loader reads and authenticates the package.
 * An intervening package event must invalidate the returned ticket. */
static inline int ref_begin(struct ref_enrollment *state,
                            struct ref_caller caller, uint64_t *ticket)
{
    int ret;
    if (!ref_privileged(caller))
        return -EPERM;
    ret = ref_invalidate(state);
    if (ret)
        return ret;
    state->pending = true;
    *ticket = state->generation;
    return 0;
}

/* verified_uid is a RESULT of trusted-loader validation, not authentication
 * material. No signature check is implemented here. All calls are serialized.
 * No corresponding begin/commit UAPI is claimed to exist in KernelSU. */
static inline int ref_commit(struct ref_enrollment *state,
                             struct ref_caller caller, uint64_t ticket,
                             uint32_t verified_uid)
{
    uint32_t app_id;
    if (!ref_privileged(caller))
        return -EPERM;
    if (state->exhausted || ticket != state->generation || !state->pending)
        return -ESTALE;
    /* Every matching transaction has one attempt, including bad input. */
    state->pending = false;
    app_id = verified_uid % REF_USER_RANGE;
    if (verified_uid > INT32_MAX || app_id < REF_FIRST_APP_ID ||
        app_id > REF_LAST_APP_ID)
        return -EINVAL;
    state->app_id = app_id;
    return 0;
}

static inline bool ref_is_manager(const struct ref_enrollment *state,
                                  uint32_t uid)
{
    return uid <= INT32_MAX && state->app_id != REF_NO_MANAGER &&
           state->app_id == uid % REF_USER_RANGE;
}

#endif
