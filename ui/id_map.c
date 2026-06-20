/* ui/id_map.c — Open-addressing hash map for app ID -> version mapping */

#include "id_map.h"

#include <stdlib.h>
#include <string.h>

/* FNV-1a hash for strings */
static uint64_t fnv1a(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ULL; /* offset basis */
    for (; *s; s++) {
        h ^= (uint64_t)(unsigned char)*s;
        h *= 0x100000001b3ULL;          /* FNV prime */
    }
    return h;
}

static void rehash(IdMap *map, size_t new_cap)
{
    IdMapEntry *old_entries = map->entries;
    size_t old_cap = map->capacity;

    map->entries  = calloc(new_cap, sizeof(IdMapEntry));
    map->capacity = new_cap;
    map->count    = 0;
    if (!map->entries) return;

    for (size_t i = 0; i < old_cap; i++) {
        if (old_entries[i].key) {
            /* Re-insert (we already own the strings) */
            size_t idx = fnv1a(old_entries[i].key) % new_cap;
            while (map->entries[idx].key) {
                idx++;
                if (idx >= new_cap) idx = 0;
            }
            map->entries[idx] = old_entries[i];
            map->count++;
        }
    }
    free(old_entries);
}

/* ------------------------------------------------------------------ */

void id_map_init(IdMap *map)
{
    map->entries  = NULL;
    map->capacity = 0;
    map->count    = 0;
}

void id_map_free(IdMap *map)
{
    if (map->entries) {
        for (size_t i = 0; i < map->capacity; i++) {
            free(map->entries[i].key);
            free(map->entries[i].value);
        }
        free(map->entries);
    }
    map->entries  = NULL;
    map->capacity = 0;
    map->count    = 0;
}

void id_map_clear(IdMap *map)
{
    if (map->entries) {
        for (size_t i = 0; i < map->capacity; i++) {
            free(map->entries[i].key);
            map->entries[i].key   = NULL;
            free(map->entries[i].value);
            map->entries[i].value = NULL;
        }
    }
    map->count = 0;
}

bool id_map_insert(IdMap *map, const char *key, const char *value)
{
    if (!key || !*key) return false;

    /* Grow if load factor > 0.75 */
    if (map->capacity == 0 || (map->count + 1) * 4 > map->capacity * 3) {
        size_t new_cap = map->capacity == 0 ? 16 : map->capacity * 2;
        rehash(map, new_cap);
        if (!map->entries) return false;
    }

    size_t idx = fnv1a(key) % map->capacity;
    for (;;) {
        if (!map->entries[idx].key) {
            /* Empty slot — insert new entry */
            map->entries[idx].key   = strdup(key);
            map->entries[idx].value = value ? strdup(value) : NULL;
            if (!map->entries[idx].key) return false;
            map->count++;
            return true;
        }
        if (strcmp(map->entries[idx].key, key) == 0) {
            /* Key exists — update value */
            free(map->entries[idx].value);
            map->entries[idx].value = value ? strdup(value) : NULL;
            return true;
        }
        idx++;
        if (idx >= map->capacity) idx = 0;
    }
}

const char *id_map_get(const IdMap *map, const char *key)
{
    if (!map->entries || !key || !*key) return NULL;

    size_t idx = fnv1a(key) % map->capacity;
    for (;;) {
        if (!map->entries[idx].key)
            return NULL;  /* Not found */
        if (strcmp(map->entries[idx].key, key) == 0)
            return map->entries[idx].value;  /* Found */
        idx++;
        if (idx >= map->capacity) idx = 0;
    }
}

void id_map_build_from_packages(IdMap *map,
                                struct LinyapsPackageInfo **list,
                                size_t count)
{
    id_map_clear(map);
    for (size_t i = 0; i < count; i++) {
        if (list[i] && list[i]->id && list[i]->version)
            id_map_insert(map, list[i]->id, list[i]->version);
    }
}
