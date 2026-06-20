/* tests/test_perf_id_set.c — Performance comparison for IdSet vs linear scan */

#include "../ui/id_set.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Generate N random app IDs */
static char **generate_ids(int n)
{
    char **ids = calloc(n, sizeof(char *));
    for (int i = 0; i < n; i++) {
        ids[i] = malloc(64);
        snprintf(ids[i], 64, "com.test.app%06d", i);
    }
    return ids;
}

static void free_ids(char **ids, int n)
{
    for (int i = 0; i < n; i++) free(ids[i]);
    free(ids);
}

/* Linear scan lookup (old approach) */
static int linear_scan_find(char **installed, int n_installed, const char *key)
{
    for (int i = 0; i < n_installed; i++) {
        if (strcmp(installed[i], key) == 0) return 1;
    }
    return 0;
}

int main(void)
{
    const int N_INSTALLED = 1000;  /* 已安装应用数量 */
    const int N_SEARCH    = 1000;  /* 搜索结果数量 */
    const int N_QUERIES   = 10000; /* 查询次数 */

    printf("Performance comparison: IdSet vs Linear Scan\n");
    printf("============================================\n");
    printf("Installed apps: %d\n", N_INSTALLED);
    printf("Search results: %d\n", N_SEARCH);
    printf("Queries:        %d\n\n", N_QUERIES);

    /* Generate test data */
    char **installed = generate_ids(N_INSTALLED);
    char **search    = generate_ids(N_SEARCH);

    /* Build IdSet */
    IdSet set;
    id_set_init(&set);
    for (int i = 0; i < N_INSTALLED; i++)
        id_set_insert(&set, installed[i]);

    /* Warm up */
    for (int i = 0; i < N_QUERIES; i++)
        id_set_contains(&set, search[i % N_SEARCH]);

    /* Benchmark IdSet */
    clock_t start = clock();
    int found_set = 0;
    for (int i = 0; i < N_QUERIES; i++) {
        if (id_set_contains(&set, search[i % N_SEARCH]))
            found_set++;
    }
    clock_t end = clock();
    double time_set = (double)(end - start) / CLOCKS_PER_SEC;

    /* Benchmark linear scan */
    start = clock();
    int found_linear = 0;
    for (int i = 0; i < N_QUERIES; i++) {
        if (linear_scan_find(installed, N_INSTALLED, search[i % N_SEARCH]))
            found_linear++;
    }
    end = clock();
    double time_linear = (double)(end - start) / CLOCKS_PER_SEC;

    /* Results */
    printf("Results:\n");
    printf("  IdSet:      %.3f ms (%d found)\n", time_set * 1000, found_set);
    printf("  Linear:     %.3f ms (%d found)\n", time_linear * 1000, found_linear);
    printf("  Speedup:    %.1fx faster\n\n", time_linear / time_set);

    /* Complexity analysis */
    printf("Complexity analysis:\n");
    printf("  Old (linear): O(n × m) = %d × %d = %d operations per frame\n",
           N_SEARCH, N_INSTALLED, N_SEARCH * N_INSTALLED);
    printf("  New (hash):   O(n) = %d operations per frame\n", N_SEARCH);
    printf("  Reduction:    %.1fx fewer operations\n\n",
           (double)(N_SEARCH * N_INSTALLED) / N_SEARCH);

    /* Cleanup */
    id_set_free(&set);
    free_ids(installed, N_INSTALLED);
    free_ids(search, N_SEARCH);

    printf("✓ Performance test completed\n");
    return 0;
}
