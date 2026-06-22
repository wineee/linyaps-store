/* SPDX-License-Identifier: MIT */

/*
 * test_filter.c — Unit tests for filter_store_search_results /
 * compare_versions.
 *
 * Includes linyaps_dbus_msg.c to reach the static functions directly.
 * Requires linking against libsystemd (for sd-bus headers).
 */

#include "linyaps_dbus_msg.c"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static LinyapsPackageInfo *make_pkg(const char *repo, const char *id,
                                    const char *module, const char *version) {
  LinyapsPackageInfo *p = calloc(1, sizeof(*p));
  assert(p);
  p->repo = repo ? strdup(repo) : NULL;
  p->id = id ? strdup(id) : NULL;
  p->module = module ? strdup(module) : NULL;
  p->version = version ? strdup(version) : NULL;
  return p;
}

/* Count: pass packages followed by NULL sentinel. Caller owns the array. */
static LinyapsPackageInfo **pack_list(size_t *out_count, ...) {
  /* First pass: count */
  va_list ap;
  va_start(ap, out_count);
  size_t n = 0;
  while (va_arg(ap, LinyapsPackageInfo *) != NULL) {
    n++;
  }
  va_end(ap);

  LinyapsPackageInfo **list = calloc(n ? n : 1, sizeof(*list));
  assert(list);

  /* Second pass: fill */
  va_start(ap, out_count);
  for (size_t i = 0; i < n; i++) {
    list[i] = va_arg(ap, LinyapsPackageInfo *);
  }
  va_end(ap);

  *out_count = n;
  return list;
}

/* ------------------------------------------------------------------ */
/* compare_versions tests                                               */
/* ------------------------------------------------------------------ */

static void test_compare_versions_basic(void) {
  assert(compare_versions("1.0.0", "1.0.0") == 0);
  assert(compare_versions("1.0.0", "2.0.0") < 0);
  assert(compare_versions("2.0.0", "1.0.0") > 0);
  printf("  [PASS] compare_versions_basic\n");
}

static void test_compare_versions_prefix(void) {
  /* "1.0" vs "1.0.1": 1.0 < 1.0.1 because after equal 1.0, b still has .1 */
  assert(compare_versions("1.0", "1.0.1") < 0);
  assert(compare_versions("1.0.1", "1.0") > 0);
  printf("  [PASS] compare_versions_prefix\n");
}

static void test_compare_versions_different_lengths(void) {
  assert(compare_versions("1.0.0", "1.0.0.1") < 0);
  assert(compare_versions("1.0.0.1", "1.0.0") > 0);
  printf("  [PASS] compare_versions_different_lengths\n");
}

static void test_compare_versions_null(void) {
  assert(compare_versions(NULL, NULL) == 0);
  assert(compare_versions(NULL, "1.0.0") < 0);
  assert(compare_versions("1.0.0", NULL) > 0);
  printf("  [PASS] compare_versions_null\n");
}

static void test_compare_versions_single_number(void) {
  assert(compare_versions("3", "2") > 0);
  assert(compare_versions("2", "3") < 0);
  assert(compare_versions("5", "5") == 0);
  printf("  [PASS] compare_versions_single_number\n");
}

static void test_compare_versions_large_numbers(void) {
  assert(compare_versions("10.20.30", "10.20.31") < 0);
  assert(compare_versions("10.20.31", "10.20.30") > 0);
  assert(compare_versions("999.999.999", "999.999.999") == 0);
  printf("  [PASS] compare_versions_large_numbers\n");
}

/* ------------------------------------------------------------------ */
/* filter_store_search_results tests                                    */
/* ------------------------------------------------------------------ */

static void test_filter_null_safety(void) {
  /* NULL items pointer */
  filter_store_search_results(NULL, &(size_t){1});

  /* NULL count pointer */
  LinyapsPackageInfo **list = calloc(1, sizeof(*list));
  filter_store_search_results(&list, NULL);

  /* NULL *items */
  LinyapsPackageInfo **null_list = NULL;
  size_t cnt = 1;
  filter_store_search_results(&null_list, &cnt);

  printf("  [PASS] filter_null_safety\n");
}

static void test_filter_empty(void) {
  LinyapsPackageInfo **list = calloc(1, sizeof(*list));
  size_t count = 0;
  filter_store_search_results(&list, &count);
  assert(count == 0);
  free(list);
  printf("  [PASS] filter_empty\n");
}

static void test_filter_removes_develop(void) {
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg("stable", "org.app", "binary", "1.0.0"),
                make_pkg("stable", "org.app", "develop", "1.0.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 1);
  assert(list[0]->module == NULL || strcmp(list[0]->module, "develop") != 0);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_removes_develop\n");
}

static void test_filter_dedup_same_version(void) {
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg("stable", "org.app", "binary", "1.0.0"),
                make_pkg("stable", "org.app", "binary", "1.0.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 1);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_dedup_same_version\n");
}

static void test_filter_dedup_keeps_higher(void) {
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg("stable", "org.app", "binary", "1.0.0"),
                make_pkg("stable", "org.app", "binary", "2.0.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 1);
  assert(strcmp(list[0]->version, "2.0.0") == 0);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_dedup_keeps_higher\n");
}

static void test_filter_different_repos_kept(void) {
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg("stable", "org.app", "binary", "1.0.0"),
                make_pkg("testing", "org.app", "binary", "1.0.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 2);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_different_repos_kept\n");
}

static void test_filter_different_ids_kept(void) {
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg("stable", "org.app1", "binary", "1.0.0"),
                make_pkg("stable", "org.app2", "binary", "1.0.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 2);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_different_ids_kept\n");
}

static void test_filter_different_modules_kept(void) {
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg("stable", "org.app", "binary", "1.0.0"),
                make_pkg("stable", "org.app", "runtime", "1.0.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 2);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_different_modules_kept\n");
}

static void test_filter_mixed_scenario(void) {
  /* Scenario: 6 packages
   * - org.app binary 1.0.0 (stable)  ← duplicate, lower
   * - org.app binary 2.0.0 (stable)  ← keep (higher version)
   * - org.app develop 2.0.0 (stable) ← remove (develop)
   * - org.app binary 1.0.0 (testing) ← keep (different repo)
   * - org.other binary 1.0.0 (stable)← keep (different id)
   * - org.other binary 1.0.0 (stable)← duplicate
   */
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg("stable", "org.app", "binary", "1.0.0"),
                make_pkg("stable", "org.app", "binary", "2.0.0"),
                make_pkg("stable", "org.app", "develop", "2.0.0"),
                make_pkg("testing", "org.app", "binary", "1.0.0"),
                make_pkg("stable", "org.other", "binary", "1.0.0"),
                make_pkg("stable", "org.other", "binary", "1.0.0"), NULL);

  filter_store_search_results(&list, &count);
  /* Expected: org.app/binary/stable(2.0.0), org.app/binary/testing(1.0.0),
   * org.other/binary/stable(1.0.0) */
  assert(count == 3);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_mixed_scenario\n");
}

static void test_filter_null_repo_treated_as_empty(void) {
  /* Both have NULL repo + same id + same module → should dedup */
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg(NULL, "org.app", "binary", "1.0.0"),
                make_pkg(NULL, "org.app", "binary", "2.0.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 1);
  assert(strcmp(list[0]->version, "2.0.0") == 0);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_null_repo_treated_as_empty\n");
}

static void test_filter_null_module_treated_as_empty(void) {
  /* Both have NULL module + same repo + same id → should dedup */
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg("stable", "org.app", NULL, "1.0.0"),
                make_pkg("stable", "org.app", NULL, "3.0.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 1);
  assert(strcmp(list[0]->version, "3.0.0") == 0);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_null_module_treated_as_empty\n");
}

static void test_filter_keeps_single_entry(void) {
  size_t count = 0;
  LinyapsPackageInfo **list = pack_list(
      &count, make_pkg("stable", "org.lonely", "binary", "0.1.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 1);
  assert(strcmp(list[0]->id, "org.lonely") == 0);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_keeps_single_entry\n");
}

static void test_filter_only_develop_removes_all(void) {
  size_t count = 0;
  LinyapsPackageInfo **list =
      pack_list(&count, make_pkg("stable", "org.app", "develop", "1.0.0"),
                make_pkg("stable", "org.app", "develop", "2.0.0"), NULL);

  filter_store_search_results(&list, &count);
  assert(count == 0);
  linyaps_package_info_list_free(list, count);
  printf("  [PASS] filter_only_develop_removes_all\n");
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
  printf("=== test_filter ===\n");

  test_compare_versions_basic();
  test_compare_versions_prefix();
  test_compare_versions_different_lengths();
  test_compare_versions_null();
  test_compare_versions_single_number();
  test_compare_versions_large_numbers();

  test_filter_null_safety();
  test_filter_empty();
  test_filter_removes_develop();
  test_filter_dedup_same_version();
  test_filter_dedup_keeps_higher();
  test_filter_different_repos_kept();
  test_filter_different_ids_kept();
  test_filter_different_modules_kept();
  test_filter_mixed_scenario();
  test_filter_null_repo_treated_as_empty();
  test_filter_null_module_treated_as_empty();
  test_filter_keeps_single_entry();
  test_filter_only_develop_removes_all();

  printf("test_filter: all passed\n");
  return 0;
}
