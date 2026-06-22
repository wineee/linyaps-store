/* SPDX-License-Identifier: MIT */
/* tests/test_id_set.c — Unit tests for IdSet */

#include "../ui/id_set.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_init_free(void) {
  IdSet set;
  id_set_init(&set);
  assert(set.keys == NULL);
  assert(set.capacity == 0);
  assert(set.count == 0);
  id_set_free(&set);
  printf("  ✓ init/free\n");
}

static void test_insert_contains(void) {
  IdSet set;
  id_set_init(&set);

  assert(id_set_insert(&set, "com.example.app1"));
  assert(id_set_insert(&set, "com.example.app2"));
  assert(id_set_insert(&set, "com.example.app3"));

  assert(id_set_contains(&set, "com.example.app1"));
  assert(id_set_contains(&set, "com.example.app2"));
  assert(id_set_contains(&set, "com.example.app3"));
  assert(!id_set_contains(&set, "com.example.app4"));
  assert(!id_set_contains(&set, ""));

  id_set_free(&set);
  printf("  ✓ insert/contains\n");
}

static void test_duplicate_insert(void) {
  IdSet set;
  id_set_init(&set);

  assert(id_set_insert(&set, "com.example.app1"));
  assert(!id_set_insert(&set, "com.example.app1")); /* duplicate */
  assert(set.count == 1);

  id_set_free(&set);
  printf("  ✓ duplicate insert\n");
}

static void test_clear(void) {
  IdSet set;
  id_set_init(&set);

  id_set_insert(&set, "com.example.app1");
  id_set_insert(&set, "com.example.app2");
  assert(set.count == 2);

  id_set_clear(&set);
  assert(set.count == 0);
  assert(!id_set_contains(&set, "com.example.app1"));

  /* Re-use after clear */
  id_set_insert(&set, "com.example.app3");
  assert(id_set_contains(&set, "com.example.app3"));

  id_set_free(&set);
  printf("  ✓ clear\n");
}

static void test_many_inserts(void) {
  IdSet set;
  id_set_init(&set);

  char buf[64];
  for (int i = 0; i < 1000; i++) {
    snprintf(buf, sizeof(buf), "com.test.app%d", i);
    assert(id_set_insert(&set, buf));
  }

  assert(set.count == 1000);

  for (int i = 0; i < 1000; i++) {
    snprintf(buf, sizeof(buf), "com.test.app%d", i);
    assert(id_set_contains(&set, buf));
  }

  assert(!id_set_contains(&set, "com.test.app1000"));

  id_set_free(&set);
  printf("  ✓ many inserts (1000)\n");
}

int main(void) {
  printf("Running IdSet tests:\n");
  test_init_free();
  test_insert_contains();
  test_duplicate_insert();
  test_clear();
  test_many_inserts();
  printf("All IdSet tests passed!\n");
  return 0;
}
