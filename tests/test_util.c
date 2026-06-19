/* SPDX-License-Identifier: LGPL-3.0-or-later */

/*
 * test_util.c — Unit tests for public utility / memory-management APIs.
 *
 * These tests do NOT require D-Bus or any running service.
 */

#include "linyaps_backend.h"
#include "linyaps_remote.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

#define ASSERT_NULL(p)    assert((p) == NULL)
#define ASSERT_NOT_NULL(p) assert((p) != NULL)
#define ASSERT_STR_EQ(a, b) assert(strcmp((a), (b)) == 0)
#define ASSERT_INT_EQ(a, b) assert((a) == (b))

/* ------------------------------------------------------------------ */
/* linyaps_task_state_string                                            */
/* ------------------------------------------------------------------ */

static void test_state_string_all_values(void)
{
    ASSERT_STR_EQ(linyaps_task_state_string(LINYAPS_TASK_STATE_UNKNOWN),    "Unknown");
    ASSERT_STR_EQ(linyaps_task_state_string(LINYAPS_TASK_STATE_QUEUED),     "Queued");
    ASSERT_STR_EQ(linyaps_task_state_string(LINYAPS_TASK_STATE_PENDING),    "Pending");
    ASSERT_STR_EQ(linyaps_task_state_string(LINYAPS_TASK_STATE_PROCESSING), "Processing");
    ASSERT_STR_EQ(linyaps_task_state_string(LINYAPS_TASK_STATE_SUCCEED),    "Succeed");
    ASSERT_STR_EQ(linyaps_task_state_string(LINYAPS_TASK_STATE_FAILED),     "Failed");
    ASSERT_STR_EQ(linyaps_task_state_string(LINYAPS_TASK_STATE_CANCELED),   "Canceled");
    printf("  [PASS] state_string_all_values\n");
}

static void test_state_string_out_of_range(void)
{
    /* Out-of-range values should return "Invalid" */
    ASSERT_STR_EQ(linyaps_task_state_string((LinyapsTaskState)99),  "Invalid");
    ASSERT_STR_EQ(linyaps_task_state_string((LinyapsTaskState)-1),  "Invalid");
    ASSERT_STR_EQ(linyaps_task_state_string((LinyapsTaskState)100), "Invalid");
    printf("  [PASS] state_string_out_of_range\n");
}

static void test_state_string_returns_static(void)
{
    /* Two calls should return the same pointer (static string) */
    const char *a = linyaps_task_state_string(LINYAPS_TASK_STATE_SUCCEED);
    const char *b = linyaps_task_state_string(LINYAPS_TASK_STATE_SUCCEED);
    assert(a == b);
    printf("  [PASS] state_string_returns_static\n");
}

/* ------------------------------------------------------------------ */
/* linyaps_package_info_free                                            */
/* ------------------------------------------------------------------ */

static void test_package_info_free_null(void)
{
    /* Should be a no-op, no crash */
    linyaps_package_info_free(NULL);
    printf("  [PASS] package_info_free_null\n");
}

static void test_package_info_free_zeroed(void)
{
    /* calloc + zero fields: should be a safe no-op free */
    LinyapsPackageInfo *info = calloc(1, sizeof(*info));
    ASSERT_NOT_NULL(info);
    /* All fields are already zeroed by calloc */
    linyaps_package_info_free(info);
    printf("  [PASS] package_info_free_zeroed\n");
}

static void test_package_info_free_partial(void)
{
    LinyapsPackageInfo *info = calloc(1, sizeof(*info));
    ASSERT_NOT_NULL(info);
    info->id = strdup("org.test.app");
    info->name = strdup("Test App");
    /* Leave other fields NULL */
    linyaps_package_info_free(info);
    printf("  [PASS] package_info_free_partial\n");
}

static void test_package_info_free_full(void)
{
    LinyapsPackageInfo *info = calloc(1, sizeof(*info));
    ASSERT_NOT_NULL(info);
    info->id             = strdup("id");
    info->name           = strdup("name");
    info->version        = strdup("1.0.0");
    info->arch           = strdup("x86_64");
    info->channel        = strdup("main");
    info->repo           = strdup("stable");
    info->description    = strdup("desc");
    info->kind           = strdup("app");
    info->module         = strdup("binary");
    info->base           = strdup("base");
    info->runtime        = strdup("runtime");
    info->schema_version = strdup("1");
    info->command        = strdup("cmd");
    info->create_time    = strdup("2025-01-01");
    info->size           = 12345;
    info->download_count = 99;
    linyaps_package_info_free(info);
    printf("  [PASS] package_info_free_full\n");
}

/* ------------------------------------------------------------------ */
/* linyaps_package_info_list_free                                       */
/* ------------------------------------------------------------------ */

static void test_package_info_list_free_null(void)
{
    linyaps_package_info_list_free(NULL, 0);
    linyaps_package_info_list_free(NULL, 10);
    printf("  [PASS] package_info_list_free_null\n");
}

static void test_package_info_list_free_empty(void)
{
    /* A non-NULL pointer with count 0: should free the array only */
    LinyapsPackageInfo **list = malloc(sizeof(*list));
    ASSERT_NOT_NULL(list);
    linyaps_package_info_list_free(list, 0);
    printf("  [PASS] package_info_list_free_empty\n");
}

static void test_package_info_list_free_multiple(void)
{
    const size_t N = 5;
    LinyapsPackageInfo **list = calloc(N, sizeof(*list));
    ASSERT_NOT_NULL(list);
    for (size_t i = 0; i < N; i++) {
        list[i] = calloc(1, sizeof(LinyapsPackageInfo));
        ASSERT_NOT_NULL(list[i]);
        list[i]->id = strdup("test");
    }
    linyaps_package_info_list_free(list, N);
    printf("  [PASS] package_info_list_free_multiple\n");
}

static void test_package_info_list_free_sparse_nulls(void)
{
    /* Some entries NULL, some valid — should handle gracefully */
    LinyapsPackageInfo **list = calloc(3, sizeof(*list));
    ASSERT_NOT_NULL(list);
    list[0] = calloc(1, sizeof(LinyapsPackageInfo));
    list[0]->id = strdup("a");
    /* list[1] is NULL */
    list[2] = calloc(1, sizeof(LinyapsPackageInfo));
    list[2]->id = strdup("b");
    linyaps_package_info_list_free(list, 3);
    printf("  [PASS] package_info_list_free_sparse_nulls\n");
}

/* ------------------------------------------------------------------ */
/* linyaps_remote_app_info_free                                         */
/* ------------------------------------------------------------------ */

static void test_remote_app_info_free_null(void)
{
    linyaps_remote_app_info_free(NULL);
    printf("  [PASS] remote_app_info_free_null\n");
}

static void test_remote_app_info_free_full(void)
{
    LinyapsRemoteAppInfo *info = calloc(1, sizeof(*info));
    ASSERT_NOT_NULL(info);
    info->base.id          = strdup("org.test");
    info->base.name        = strdup("Test");
    info->base.version     = strdup("1.0.0");
    info->base.arch        = strdup("x86_64");
    info->base.channel     = strdup("main");
    info->base.repo        = strdup("stable");
    info->base.description = strdup("A test app");
    info->base.kind        = strdup("app");
    info->base.module      = strdup("binary");
    info->base.runtime     = strdup("runtime");
    info->base.create_time = strdup("2025-01-01");
    info->icon_url         = strdup("https://example.com/icon.png");
    info->zh_name          = strdup("测试应用");
    info->category_id      = strdup("01");
    info->category_name    = strdup("网络应用");
    info->base.size        = 1024;
    info->base.download_count = 500;
    linyaps_remote_app_info_free(info);
    printf("  [PASS] remote_app_info_free_full\n");
}

/* ------------------------------------------------------------------ */
/* linyaps_remote_app_info_list_free                                    */
/* ------------------------------------------------------------------ */

static void test_remote_app_info_list_free_null(void)
{
    linyaps_remote_app_info_list_free(NULL, 0);
    linyaps_remote_app_info_list_free(NULL, 5);
    printf("  [PASS] remote_app_info_list_free_null\n");
}

static void test_remote_app_info_list_free_multiple(void)
{
    const size_t N = 3;
    LinyapsRemoteAppInfo **list = calloc(N, sizeof(*list));
    ASSERT_NOT_NULL(list);
    for (size_t i = 0; i < N; i++) {
        list[i] = calloc(1, sizeof(LinyapsRemoteAppInfo));
        ASSERT_NOT_NULL(list[i]);
        list[i]->base.id = strdup("test");
        list[i]->icon_url = strdup("http://example.com");
    }
    linyaps_remote_app_info_list_free(list, N);
    printf("  [PASS] remote_app_info_list_free_multiple\n");
}

/* ------------------------------------------------------------------ */
/* linyaps_remote_app_info_to_package_info                              */
/* ------------------------------------------------------------------ */

static void test_to_package_info_null(void)
{
    LinyapsPackageInfo *p = linyaps_remote_app_info_to_package_info(NULL);
    ASSERT_NULL(p);
    printf("  [PASS] to_package_info_null\n");
}

static void test_to_package_info_full(void)
{
    LinyapsRemoteAppInfo *remote = calloc(1, sizeof(*remote));
    ASSERT_NOT_NULL(remote);
    remote->base.id          = strdup("org.test.full");
    remote->base.name        = strdup("Full Test");
    remote->base.version     = strdup("2.3.4");
    remote->base.arch        = strdup("x86_64");
    remote->base.channel     = strdup("main");
    remote->base.repo        = strdup("stable");
    remote->base.description = strdup("A full test app");
    remote->base.kind        = strdup("app");
    remote->base.module      = strdup("binary");
    remote->base.runtime     = strdup("org.deepin.runtime");
    remote->base.schema_version = strdup("1");
    remote->base.command     = strdup("full-test");
    remote->base.create_time = strdup("2025-06-01");
    remote->base.size        = 4096;
    remote->base.download_count = 100;
    remote->icon_url         = strdup("http://example.com/icon.png");
    remote->zh_name          = strdup("完整测试");
    remote->category_id      = strdup("02");
    remote->category_name    = strdup("开发工具");

    LinyapsPackageInfo *p = linyaps_remote_app_info_to_package_info(remote);
    ASSERT_NOT_NULL(p);

    /* Verify all string fields are independently copied */
    ASSERT_STR_EQ(p->id,             "org.test.full");
    ASSERT_STR_EQ(p->name,           "Full Test");
    ASSERT_STR_EQ(p->version,        "2.3.4");
    ASSERT_STR_EQ(p->arch,           "x86_64");
    ASSERT_STR_EQ(p->channel,        "main");
    ASSERT_STR_EQ(p->repo,           "stable");
    ASSERT_STR_EQ(p->description,    "A full test app");
    ASSERT_STR_EQ(p->kind,           "app");
    ASSERT_STR_EQ(p->module,         "binary");
    ASSERT_STR_EQ(p->runtime,        "org.deepin.runtime");
    ASSERT_STR_EQ(p->schema_version, "1");
    ASSERT_STR_EQ(p->command,        "full-test");
    ASSERT_STR_EQ(p->create_time,    "2025-06-01");
    ASSERT_INT_EQ(p->size,           4096);
    ASSERT_INT_EQ(p->download_count, 100);

    /* Strings should be independent (different pointers) */
    assert(p->id != remote->base.id);
    assert(p->name != remote->base.name);
    assert(p->version != remote->base.version);

    /* icon_url, zh_name, category_* should NOT be copied (not in LinyapsPackageInfo) */
    /* They are not accessible via p, so just free both */
    linyaps_package_info_free(p);
    linyaps_remote_app_info_free(remote);
    printf("  [PASS] to_package_info_full\n");
}

static void test_to_package_info_partial(void)
{
    /* Only id and version set, everything else NULL */
    LinyapsRemoteAppInfo *remote = calloc(1, sizeof(*remote));
    ASSERT_NOT_NULL(remote);
    remote->base.id      = strdup("org.partial");
    remote->base.version = strdup("0.1.0");

    LinyapsPackageInfo *p = linyaps_remote_app_info_to_package_info(remote);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->id,      "org.partial");
    ASSERT_STR_EQ(p->version, "0.1.0");
    ASSERT_NULL(p->name);
    ASSERT_NULL(p->arch);
    ASSERT_NULL(p->channel);
    ASSERT_NULL(p->description);
    ASSERT_INT_EQ(p->size, 0);

    linyaps_package_info_free(p);
    linyaps_remote_app_info_free(remote);
    printf("  [PASS] to_package_info_partial\n");
}

static void test_to_package_info_independence(void)
{
    /* Verify that modifying remote after conversion doesn't affect the copy */
    LinyapsRemoteAppInfo *remote = calloc(1, sizeof(*remote));
    ASSERT_NOT_NULL(remote);
    remote->base.id      = strdup("org.indep");
    remote->base.name    = strdup("Original");
    remote->base.version = strdup("1.0.0");

    LinyapsPackageInfo *p = linyaps_remote_app_info_to_package_info(remote);
    ASSERT_NOT_NULL(p);

    /* Free the remote first */
    linyaps_remote_app_info_free(remote);

    /* p should still be valid */
    ASSERT_STR_EQ(p->id,      "org.indep");
    ASSERT_STR_EQ(p->name,    "Original");
    ASSERT_STR_EQ(p->version, "1.0.0");

    linyaps_package_info_free(p);
    printf("  [PASS] to_package_info_independence\n");
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== test_util ===\n");

    test_state_string_all_values();
    test_state_string_out_of_range();
    test_state_string_returns_static();

    test_package_info_free_null();
    test_package_info_free_zeroed();
    test_package_info_free_partial();
    test_package_info_free_full();

    test_package_info_list_free_null();
    test_package_info_list_free_empty();
    test_package_info_list_free_multiple();
    test_package_info_list_free_sparse_nulls();

    test_remote_app_info_free_null();
    test_remote_app_info_free_full();
    test_remote_app_info_list_free_null();
    test_remote_app_info_list_free_multiple();

    test_to_package_info_null();
    test_to_package_info_full();
    test_to_package_info_partial();
    test_to_package_info_independence();

    printf("test_util: all passed\n");
    return 0;
}
