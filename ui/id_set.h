/* SPDX-License-Identifier: MIT */
/* ui/id_set.h — Simple open-addressing hash set for app IDs */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A minimal open-addressing hash set for C strings (app IDs).
 * Uses linear probing with a load factor cap of 0.75. */
typedef struct IdSet {
  char **keys; /* NULL means empty slot */
  size_t capacity;
  size_t count;
} IdSet;

/* Initialize an empty set. */
void id_set_init(IdSet *set);

/* Free all memory owned by the set (but not the set struct itself). */
void id_set_free(IdSet *set);

/* Remove all entries, keeping the allocation. */
void id_set_clear(IdSet *set);

/* Insert a key (duplicated internally).  Returns true if newly inserted. */
bool id_set_insert(IdSet *set, const char *key);

/* Check membership. */
bool id_set_contains(const IdSet *set, const char *key);

/* Build a set from an array of LinyapsPackageInfo. */
#include "../lib/linyaps_types.h"
void id_set_build_from_packages(IdSet *set, LinyapsPackageInfo **list,
                                size_t count);

#ifdef __cplusplus
}
#endif
