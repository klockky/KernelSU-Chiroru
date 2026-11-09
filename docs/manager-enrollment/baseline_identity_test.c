/* SPDX-License-Identifier: GPL-2.0 */
/* Host-only checks of the real manager_identity.h; kernel services are mocked. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "manager_identity.h"

uid_t ksu_manager_appid = INITIAL_MANAGER_APPID;
struct fake_kuid test_current_uid;

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--regression")) {
        /* A newly loaded module must not trust an unverified numeric AppID. */
        if (ksu_is_manager_appid_valid() || is_uid_manager(10058)) {
            fprintf(stderr, "FAIL: fresh module trusts unverified AppID %u\n",
                    ksu_get_manager_appid());
            return 1;
        }
        puts("PASS: no manager is trusted before enrollment");
        return 0;
    }

    /* Characterization, NOT proof of a production enrollment fix. */
    ksu_set_manager_appid(10058);
    assert(is_uid_manager(10058));
    assert(!is_uid_manager(10033));
    puts("PASS: reproduces old-AppID trust and real-manager rejection");

    ksu_invalidate_manager_uid();
    assert(!ksu_is_manager_appid_valid());
    assert(!is_uid_manager(10058));
    assert(!is_uid_manager(10033));
    assert(!is_uid_manager(0));
    puts("PASS: invalid identity denies ordinary manager checks");

    ksu_set_manager_appid(10033);
    assert(is_uid_manager(10033));
    assert(!is_uid_manager(10058));
    test_current_uid.val = 10033;
    assert(is_manager());
    puts("PASS: runtime AppID correction changes manager recognition");

    assert(is_uid_manager(110033));
    assert(!is_uid_manager(110058));
    test_current_uid.val = 110033;
    assert(is_manager());
    puts("PASS: manager comparison uses per-user AppID semantics");

    ksu_invalidate_manager_uid();
    assert(!is_uid_manager(110033));
    assert(!is_manager());
    puts("PASS: invalidation revokes subsequent identity checks");

    ksu_set_manager_appid(110033);
    assert(!is_uid_manager(110033));
    puts("PASS: setter expects AppID, not an unnormalized secondary-user UID");
    return 0;
}
