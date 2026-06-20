/* SPDX-License-Identifier: MIT */
/* ui/id_map.h — Open-addressing hash map for app ID -> version mapping */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A minimal open-addressing hash map for C strings.
 * Maps string keys (app IDs) to string values (versions).
 * Uses linear probing with a load factor cap of 0.75. */
typedef struct IdMapEntry {
    char *key;    /* NULL means empty slot */
    char *value;  /* associated value (version string) */
} IdMapEntry;

typedef struct IdMap {
    IdMapEntry *entries;
    size_t      capacity;
    size_t      count;
} IdMap;

/* Initialize an empty map. */
void id_map_init(IdMap *map);

/* Free all memory owned by the map (but not the map struct itself). */
void id_map_free(IdMap *map);

/* Remove all entries, keeping the allocation. */
void id_map_clear(IdMap *map);

/* Insert or update a key-value pair. Both key and value are duplicated internally.
 * Returns true on success. */
bool id_map_insert(IdMap *map, const char *key, const char *value);

/* Look up a key. Returns the associated value, or NULL if not found.
 * The returned pointer is owned by the map and valid until the entry is removed. */
const char *id_map_get(const IdMap *map, const char *key);

/* Build a map from an array of LinyapsPackageInfo, mapping id -> version. */
#include "../lib/linyaps_types.h"
void id_map_build_from_packages(IdMap *map,
                                struct LinyapsPackageInfo **list,
                                size_t count);

#ifdef __cplusplus
}
#endif
