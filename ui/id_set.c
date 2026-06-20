/* ui/id_set.c — Open-addressing hash set for app IDs */

#include "id_set.h"

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

static void rehash(IdSet *set, size_t new_cap)
{
    char **old_keys = set->keys;
    size_t old_cap  = set->capacity;

    set->keys      = calloc(new_cap, sizeof(char *));
    set->capacity  = new_cap;
    set->count     = 0;
    if (!set->keys) return;

    for (size_t i = 0; i < old_cap; i++) {
        if (old_keys[i]) {
            /* Re-insert (we already own the strings) */
            size_t idx = fnv1a(old_keys[i]) % new_cap;
            while (set->keys[idx]) {
                idx++;
                if (idx >= new_cap) idx = 0;
            }
            set->keys[idx] = old_keys[i];
            set->count++;
        }
    }
    free(old_keys);
}

/* ------------------------------------------------------------------ */

void id_set_init(IdSet *set)
{
    set->keys     = NULL;
    set->capacity = 0;
    set->count    = 0;
}

void id_set_free(IdSet *set)
{
    if (set->keys) {
        for (size_t i = 0; i < set->capacity; i++)
            free(set->keys[i]);
        free(set->keys);
    }
    set->keys     = NULL;
    set->capacity = 0;
    set->count    = 0;
}

void id_set_clear(IdSet *set)
{
    if (set->keys) {
        for (size_t i = 0; i < set->capacity; i++) {
            free(set->keys[i]);
            set->keys[i] = NULL;
        }
    }
    set->count = 0;
}

bool id_set_insert(IdSet *set, const char *key)
{
    if (!key || !*key) return false;

    /* Grow if load factor > 0.75 */
    if (set->capacity == 0 || (set->count + 1) * 4 > set->capacity * 3) {
        size_t new_cap = set->capacity == 0 ? 16 : set->capacity * 2;
        rehash(set, new_cap);
        if (!set->keys) return false;
    }

    size_t idx = fnv1a(key) % set->capacity;
    for (;;) {
        if (!set->keys[idx]) {
            set->keys[idx] = strdup(key);
            if (!set->keys[idx]) return false;
            set->count++;
            return true;
        }
        if (strcmp(set->keys[idx], key) == 0)
            return false; /* already present */
        idx++;
        if (idx >= set->capacity) idx = 0;
    }
}

bool id_set_contains(const IdSet *set, const char *key)
{
    if (!set->keys || !key || !*key) return false;

    size_t idx = fnv1a(key) % set->capacity;
    for (;;) {
        if (!set->keys[idx])
            return false;
        if (strcmp(set->keys[idx], key) == 0)
            return true;
        idx++;
        if (idx >= set->capacity) idx = 0;
    }
}

void id_set_build_from_packages(IdSet *set,
                                LinyapsPackageInfo **list,
                                size_t count)
{
    id_set_clear(set);
    for (size_t i = 0; i < count; i++) {
        if (list[i] && list[i]->id)
            id_set_insert(set, list[i]->id);
    }
}
