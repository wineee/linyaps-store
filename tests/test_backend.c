/* SPDX-License-Identifier: LGPL-3.0-or-later */

#include "linyaps_backend.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_context_lifecycle(void)
{
    LinyapsContext *ctx = linyaps_context_new();
    if (!ctx) {
        puts("system D-Bus unavailable; skipping context lifecycle integration check");
        return;
    }
    assert(linyaps_process(ctx) >= 0);
    linyaps_context_free(ctx);
}

static void test_state_string(void)
{
    assert(strcmp(linyaps_task_state_string(LINYAPS_TASK_STATE_UNKNOWN), "Unknown") == 0);
    assert(strcmp(linyaps_task_state_string(LINYAPS_TASK_STATE_SUCCEED), "Succeed") == 0);
    assert(strcmp(linyaps_task_state_string(LINYAPS_TASK_STATE_CANCELED), "Canceled") == 0);
}

static void test_optional_service_checks(void)
{
    LinyapsContext *ctx = linyaps_context_new();
    if (!ctx) {
        puts("system D-Bus unavailable; skipping service/list integration checks");
        return;
    }

    bool available = linyaps_is_service_available(ctx);
    printf("PackageManager1 service: %s\n", available ? "available" : "unavailable");

    size_t count = 0;
    LinyapsPackageInfo **installed = linyaps_list_installed(ctx, &count);
    printf("installed packages parsed: %zu\n", count);
    if (installed) {
        assert(count > 0);
        assert(installed[0] != NULL);
        assert(installed[0]->id != NULL);
        linyaps_package_info_list_free(installed, count);
    }

    LinyapsPackageInfo *info = linyaps_info(ctx, "org.deepin.draw");
    if (info) {
        assert(info->id != NULL);
        assert(strcmp(info->id, "org.deepin.draw") == 0);
        assert(info->version != NULL);
        linyaps_package_info_free(info);
    } else {
        puts("org.deepin.draw not installed; skipping info check");
    }

    linyaps_context_free(ctx);
}

int main(void)
{
    test_state_string();
    test_context_lifecycle();
    test_optional_service_checks();
    puts("test_backend: ok");
    return 0;
}
