/* SPDX-License-Identifier: GPL-2.0 */
/* Delliaf: host-only reference-policy tests. No device/kernel access. */
#include <assert.h>
#include <stdio.h>
#include "reference.h"

static const struct ref_caller root = {0, 0};
static const struct ref_caller app = {10033, 10033};
static unsigned int checks;
#define PASS(name) do { ++checks; puts("PASS: " name); } while (0)

int main(void)
{
    struct ref_enrollment state;
    uint64_t first = 0, second = 0;
    ref_init(&state);
    assert(!ref_is_manager(&state, 10058));
    assert(!ref_is_manager(&state, 10033));
    assert(!ref_is_manager(&state, 0));
    PASS("initial state has no trusted manager");

    assert(ref_begin(&state, app, &first) == -EPERM);
    assert(state.generation == 0 && !state.pending);
    assert(ref_commit(&state, app, 0, 10033) == -EPERM);
    PASS("unprivileged caller cannot begin or commit");

    assert(ref_begin(&state, (struct ref_caller){0, 10033}, &first) == -EPERM);
    assert(ref_begin(&state, (struct ref_caller){10033, 0}, &first) == -EPERM);
    assert(ref_commit(&state, (struct ref_caller){0, 10033}, 0, 10033) == -EPERM);
    assert(ref_commit(&state, (struct ref_caller){10033, 0}, 0, 10033) == -EPERM);
    PASS("mixed real/effective credentials are denied");

    assert(ref_commit(&state, root, 0, 10033) == -ESTALE);
    PASS("commit without a transaction is denied");

    assert(ref_begin(&state, root, &first) == 0);
    assert(ref_commit(&state, root, first, 10033) == 0);
    assert(ref_is_manager(&state, 10033));
    assert(!ref_is_manager(&state, 10058));
    PASS("verified loader result selects the actual AppID");

    assert(ref_commit(&state, root, first, 10058) == -ESTALE);
    assert(ref_is_manager(&state, 10033));
    assert(!ref_is_manager(&state, 10058));
    PASS("a committed ticket cannot be replayed");

    assert(ref_begin(&state, root, &first) == 0);
    assert(!ref_is_manager(&state, 10033));
    assert(!ref_is_manager(&state, 10058));
    PASS("revalidation starts with old trust revoked");

    /* No commit: missing manager, rejected signer or failed PM query must
     * cause the loader to abort explicitly via invalidation. A failed external
     * check does not automatically cancel this model's pending ticket. */
    assert(state.pending && state.app_id == REF_NO_MANAGER);
    assert(ref_invalidate(&state) == 0);
    assert(!state.pending && state.app_id == REF_NO_MANAGER);
    assert(ref_commit(&state, root, first, 10033) == -ESTALE);
    PASS("explicit loader abort revokes its pending enrollment ticket");

    assert(ref_begin(&state, root, &first) == 0);
    assert(ref_invalidate(&state) == 0);
    assert(ref_commit(&state, root, first, 10033) == -ESTALE);
    assert(!ref_is_manager(&state, 10033));
    PASS("package event rejects an in-flight stale result");

    assert(ref_begin(&state, root, &first) == 0);
    assert(ref_begin(&state, root, &second) == 0);
    assert(ref_commit(&state, root, second, 10044) == 0);
    assert(ref_commit(&state, root, first, 10033) == -ESTALE);
    assert(ref_is_manager(&state, 10044));
    assert(!ref_is_manager(&state, 10033));
    PASS("an older result cannot replace a newer registration");

    assert(ref_invalidate(&state) == 0);
    assert(!ref_is_manager(&state, 10044));
    assert(ref_begin(&state, root, &first) == 0);
    assert(ref_commit(&state, root, first, 10045) == 0);
    assert(!ref_is_manager(&state, 10044));
    assert(ref_is_manager(&state, 10045));
    PASS("reinstall can move identity without retaining the old AppID");

    const uint32_t invalid[] = {0, 1000, 9999, 20000, 90000, 99000,
                                UINT32_MAX, UINT32_C(2147483648)};
    for (unsigned int i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(ref_begin(&state, root, &first) == 0);
        assert(ref_commit(&state, root, first, invalid[i]) == -EINVAL);
        assert(state.app_id == REF_NO_MANAGER);
        assert(ref_commit(&state, root, first, 10033) == -ESTALE);
    }
    PASS("non-application and oversized UIDs fail closed and consume tickets");

    assert(ref_begin(&state, root, &first) == 0);
    assert(ref_commit(&state, root, first, 110033) == 0);
    assert(state.app_id == 10033);
    assert(ref_is_manager(&state, 10033));
    assert(ref_is_manager(&state, 110033));
    assert(!ref_is_manager(&state, 110058));
    assert(!ref_is_manager(&state, UINT32_MAX));
    PASS("reference preserves existing per-user AppID comparison semantics");

    for (uint32_t id = REF_FIRST_APP_ID; id <= REF_LAST_APP_ID; ++id) {
        assert(ref_begin(&state, root, &first) == 0);
        assert(ref_commit(&state, root, first, id) == 0);
        assert(ref_is_manager(&state, id));
        assert(!ref_is_manager(&state, id == REF_LAST_APP_ID ? id - 1 : id + 1));
    }
    PASS("all normal-application AppIDs are accepted without a device seed");

    state.generation = UINT64_MAX;
    assert(ref_begin(&state, root, &first) == -EOVERFLOW);
    assert(state.exhausted && !state.pending && state.app_id == REF_NO_MANAGER);
    assert(ref_invalidate(&state) == -EOVERFLOW);
    assert(ref_commit(&state, root, UINT64_MAX, 10033) == -ESTALE);
    PASS("generation exhaustion never wraps into a reusable ticket");

    ref_init(&state);
    assert(state.generation == 0 && !state.pending && !state.exhausted);
    assert(!ref_is_manager(&state, 10033));
    PASS("module-reset model starts without persisted numeric trust");

    printf("REFERENCE_CHECKS=%u\n", checks);
    return 0;
}
