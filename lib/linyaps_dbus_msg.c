/* SPDX-License-Identifier: MIT */

#include "linyaps_private.h"

/* ------------------------------------------------------------------ */
/* Message builders                                                    */
/* ------------------------------------------------------------------ */

int append_sv_string(sd_bus_message *m, const char *key, const char *value)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) return r;
    r = sd_bus_message_append_basic(m, 's', key);
    if (r < 0) return r;
    r = sd_bus_message_open_container(m, 'v', "s");
    if (r < 0) return r;
    r = sd_bus_message_append_basic(m, 's', value ? value : "");
    if (r < 0) return r;
    r = sd_bus_message_close_container(m);
    if (r < 0) return r;
    return sd_bus_message_close_container(m);
}

int append_sv_bool(sd_bus_message *m, const char *key, int value)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) return r;
    r = sd_bus_message_append_basic(m, 's', key);
    if (r < 0) return r;
    r = sd_bus_message_open_container(m, 'v', "b");
    if (r < 0) return r;
    r = sd_bus_message_append_basic(m, 'b', &value);
    if (r < 0) return r;
    r = sd_bus_message_close_container(m);
    if (r < 0) return r;
    return sd_bus_message_close_container(m);
}

int append_repos(sd_bus_message *m, const char *repos)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) return r;
    r = sd_bus_message_append_basic(m, 's', "repos");
    if (r < 0) return r;
    r = sd_bus_message_open_container(m, 'v', "as");
    if (r < 0) return r;
    r = sd_bus_message_open_container(m, 'a', "s");
    if (r < 0) return r;
    if (repos && *repos) {
        char *copy = dupstr(repos);
        char *save = NULL;
        for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            while (*tok == ' ' || *tok == '\t') tok++;
            if (*tok) {
                r = sd_bus_message_append_basic(m, 's', tok);
                if (r < 0) { free(copy); return r; }
            }
        }
        free(copy);
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) return r;
    r = sd_bus_message_close_container(m);
    if (r < 0) return r;
    return sd_bus_message_close_container(m);
}

int append_package(sd_bus_message *m,
                   const char *id,
                   const char *version,
                   const char *channel,
                   const char *module,
                   int include_module)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) return r;
    r = sd_bus_message_append_basic(m, 's', "package");
    if (r < 0) return r;
    r = sd_bus_message_open_container(m, 'v', "a{sv}");
    if (r < 0) return r;
    r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r < 0) return r;
    if ((r = append_sv_string(m, "id", id)) < 0) return r;
    if (version && *version && (r = append_sv_string(m, "version", version)) < 0) return r;
    if (channel && *channel && (r = append_sv_string(m, "channel", channel)) < 0) return r;
    if (include_module && module && *module &&
        (r = append_sv_string(m, "packageInfoV2Module", module)) < 0) return r;
    r = sd_bus_message_close_container(m);
    if (r < 0) return r;
    r = sd_bus_message_close_container(m);
    if (r < 0) return r;
    return sd_bus_message_close_container(m);
}

int append_options(sd_bus_message *m, int include_force, int force, int include_skip)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) return r;
    r = sd_bus_message_append_basic(m, 's', "options");
    if (r < 0) return r;
    r = sd_bus_message_open_container(m, 'v', "a{sv}");
    if (r < 0) return r;
    r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r < 0) return r;
    if (include_force && (r = append_sv_bool(m, "force", force)) < 0) return r;
    if (include_skip && (r = append_sv_bool(m, "skipInteraction", 0)) < 0) return r;
    r = sd_bus_message_close_container(m);
    if (r < 0) return r;
    r = sd_bus_message_close_container(m);
    if (r < 0) return r;
    return sd_bus_message_close_container(m);
}

/* ------------------------------------------------------------------ */
/* Reply parsers                                                        */
/* ------------------------------------------------------------------ */

int parse_common_result(sd_bus_message *reply, int64_t *code, char **message)
{
    int r;
    if (code)   *code = 0;
    if (message) *message = NULL;
    r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if (r < 0) return r;
    while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        r = sd_bus_message_read_basic(reply, 's', &key);
        if (r < 0) return r;
        r = sd_bus_message_peek_type(reply, &type, &sig);
        if (r < 0) return r;
        if (strcmp(key, "code") == 0 && sig && strcmp(sig, "x") == 0) {
            int64_t value = 0;
            if ((r = sd_bus_message_enter_container(reply, 'v', "x")) < 0) return r;
            if ((r = sd_bus_message_read_basic(reply, 'x', &value)) < 0) return r;
            if (code) *code = value;
            if ((r = sd_bus_message_exit_container(reply)) < 0) return r;
        } else if (strcmp(key, "message") == 0 && sig && strcmp(sig, "s") == 0) {
            const char *value = NULL;
            if ((r = sd_bus_message_enter_container(reply, 'v', "s")) < 0) return r;
            if ((r = sd_bus_message_read_basic(reply, 's', &value)) < 0) return r;
            if (message) *message = dupstr(value);
            if ((r = sd_bus_message_exit_container(reply)) < 0) return r;
        } else {
            if ((r = sd_bus_message_skip(reply, "v")) < 0) return r;
        }
        if ((r = sd_bus_message_exit_container(reply)) < 0) return r;
    }
    if (r < 0) return r;
    return sd_bus_message_exit_container(reply);
}

int parse_job_info(sd_bus_message *reply, int64_t *code, char **message, char **job_id)
{
    int r;
    if (code)    *code = 0;
    if (message) *message = NULL;
    if (job_id)  *job_id = NULL;
    r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if (r < 0) return r;
    while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        if ((r = sd_bus_message_read_basic(reply, 's', &key)) < 0) return r;
        if ((r = sd_bus_message_peek_type(reply, &type, &sig)) < 0) return r;
        if (strcmp(key, "id") == 0 && sig && strcmp(sig, "s") == 0) {
            const char *value = NULL;
            if ((r = sd_bus_message_enter_container(reply, 'v', "s")) < 0) return r;
            if ((r = sd_bus_message_read_basic(reply, 's', &value)) < 0) return r;
            if (job_id) *job_id = dupstr(value);
            if ((r = sd_bus_message_exit_container(reply)) < 0) return r;
        } else if (strcmp(key, "code") == 0 && sig && strcmp(sig, "x") == 0) {
            int64_t value = 0;
            if ((r = sd_bus_message_enter_container(reply, 'v', "x")) < 0) return r;
            if ((r = sd_bus_message_read_basic(reply, 'x', &value)) < 0) return r;
            if (code) *code = value;
            if ((r = sd_bus_message_exit_container(reply)) < 0) return r;
        } else if (strcmp(key, "message") == 0 && sig && strcmp(sig, "s") == 0) {
            const char *value = NULL;
            if ((r = sd_bus_message_enter_container(reply, 'v', "s")) < 0) return r;
            if ((r = sd_bus_message_read_basic(reply, 's', &value)) < 0) return r;
            if (message) *message = dupstr(value);
            if ((r = sd_bus_message_exit_container(reply)) < 0) return r;
        } else {
            if ((r = sd_bus_message_skip(reply, "v")) < 0) return r;
        }
        if ((r = sd_bus_message_exit_container(reply)) < 0) return r;
    }
    if (r < 0) return r;
    return sd_bus_message_exit_container(reply);
}

/* ------------------------------------------------------------------ */
/* Variant readers                                                      */
/* ------------------------------------------------------------------ */

int read_variant_string(sd_bus_message *m, const char *sig, char **out)
{
    if (!sig || strcmp(sig, "s") != 0) return sd_bus_message_skip(m, "v");
    const char *value = NULL;
    int r = sd_bus_message_enter_container(m, 'v', "s");
    if (r < 0) return r;
    r = sd_bus_message_read_basic(m, 's', &value);
    if (r >= 0) *out = dupstr(value);
    if (r < 0) return r;
    return sd_bus_message_exit_container(m);
}

int read_variant_int64(sd_bus_message *m, const char *sig, int64_t *out)
{
    if (!sig || strcmp(sig, "x") != 0) return sd_bus_message_skip(m, "v");
    int64_t value = 0;
    int r = sd_bus_message_enter_container(m, 'v', "x");
    if (r < 0) return r;
    r = sd_bus_message_read_basic(m, 'x', &value);
    if (r >= 0) *out = value;
    if (r < 0) return r;
    return sd_bus_message_exit_container(m);
}

int read_variant_first_string_array(sd_bus_message *m, const char *sig, char **out)
{
    if (!sig || strcmp(sig, "as") != 0) return sd_bus_message_skip(m, "v");
    int r = sd_bus_message_enter_container(m, 'v', "as");
    if (r < 0) return r;
    r = sd_bus_message_enter_container(m, 'a', "s");
    if (r < 0) return r;
    const char *value = NULL;
    r = sd_bus_message_read_basic(m, 's', &value);
    if (r > 0 && value) {
        *out = dupstr(value);
        while (sd_bus_message_read_basic(m, 's', &value) > 0) {}
    }
    sd_bus_message_exit_container(m);
    return sd_bus_message_exit_container(m);
}

/* ------------------------------------------------------------------ */
/* Package map parser                                                   */
/* ------------------------------------------------------------------ */

static LinyapsPackageInfo *package_info_new(void)
{
    return calloc(1, sizeof(LinyapsPackageInfo));
}

int parse_package_map(sd_bus_message *m, const char *repo, LinyapsPackageInfo **out_info)
{
    int r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) return r;
    LinyapsPackageInfo *info = package_info_new();
    if (!info) return -ENOMEM;
    info->repo = dupstr(repo);
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(m, 's', &key);
        sd_bus_message_peek_type(m, &type, &sig);
        if      (strcmp(key, "id") == 0)                  r = read_variant_string(m, sig, &info->id);
        else if (strcmp(key, "name") == 0)                r = read_variant_string(m, sig, &info->name);
        else if (strcmp(key, "version") == 0)             r = read_variant_string(m, sig, &info->version);
        else if (strcmp(key, "channel") == 0)             r = read_variant_string(m, sig, &info->channel);
        else if (strcmp(key, "kind") == 0)                r = read_variant_string(m, sig, &info->kind);
        else if (strcmp(key, "packageInfoV2Module") == 0) r = read_variant_string(m, sig, &info->module);
        else if (strcmp(key, "description") == 0)         r = read_variant_string(m, sig, &info->description);
        else if (strcmp(key, "base") == 0)                r = read_variant_string(m, sig, &info->base);
        else if (strcmp(key, "runtime") == 0)             r = read_variant_string(m, sig, &info->runtime);
        else if (strcmp(key, "schema_version") == 0)      r = read_variant_string(m, sig, &info->schema_version);
        else if (strcmp(key, "command") == 0)             r = read_variant_first_string_array(m, sig, &info->command);
        else if (strcmp(key, "arch") == 0)                r = read_variant_first_string_array(m, sig, &info->arch);
        else if (strcmp(key, "size") == 0)                r = read_variant_int64(m, sig, &info->size);
        else                                               r = sd_bus_message_skip(m, "v");
        if (r < 0) { linyaps_package_info_free(info); return r; }
        if ((r = sd_bus_message_exit_container(m)) < 0) { linyaps_package_info_free(info); return r; }
    }
    if (r < 0) { linyaps_package_info_free(info); return r; }
    r = sd_bus_message_exit_container(m);
    if (r < 0) { linyaps_package_info_free(info); return r; }
    *out_info = info;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Search reply parser                                                  */
/* ------------------------------------------------------------------ */

int parse_search_reply(sd_bus_message *reply,
                       LinyapsPackageInfo ***out_items,
                       size_t *out_count,
                       int64_t *out_code,
                       char **out_message)
{
    int r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if (r < 0) return r;
    LinyapsPackageInfo **items = NULL;
    size_t count = 0, cap = 0;
    int64_t code = 0;
    char *message = NULL;
    while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(reply, 's', &key);
        sd_bus_message_peek_type(reply, &type, &sig);
        if (strcmp(key, "code") == 0) {
            r = read_variant_int64(reply, sig, &code);
        } else if (strcmp(key, "message") == 0) {
            r = read_variant_string(reply, sig, &message);
        } else if (strcmp(key, "packages") == 0 && sig && strcmp(sig, "a{sv}") == 0) {
            r = sd_bus_message_enter_container(reply, 'v', "a{sv}");
            if (r >= 0) r = sd_bus_message_enter_container(reply, 'a', "{sv}");
            while (r >= 0 && (r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
                const char *repo = NULL;
                sd_bus_message_read_basic(reply, 's', &repo);
                r = sd_bus_message_enter_container(reply, 'v', "av");
                if (r >= 0) r = sd_bus_message_enter_container(reply, 'a', "v");
                while (r >= 0 && (r = sd_bus_message_enter_container(reply, 'v', "a{sv}")) > 0) {
                    LinyapsPackageInfo *info = NULL;
                    r = parse_package_map(reply, repo, &info);
                    if (r >= 0 && info) package_list_append(&items, &count, &cap, info);
                    if (r >= 0) r = sd_bus_message_exit_container(reply);
                }
                if (r == 0) r = sd_bus_message_exit_container(reply);
                if (r >= 0) r = sd_bus_message_exit_container(reply);
                if (r >= 0) r = sd_bus_message_exit_container(reply);
            }
            if (r == 0) r = sd_bus_message_exit_container(reply);
            if (r >= 0) r = sd_bus_message_exit_container(reply);
        } else {
            r = sd_bus_message_skip(reply, "v");
        }
        if (r < 0) { linyaps_package_info_list_free(items, count); free(message); return r; }
        if ((r = sd_bus_message_exit_container(reply)) < 0) {
            linyaps_package_info_list_free(items, count); free(message); return r;
        }
    }
    if (r < 0) { linyaps_package_info_list_free(items, count); free(message); return r; }
    r = sd_bus_message_exit_container(reply);
    if (r < 0) { linyaps_package_info_list_free(items, count); free(message); return r; }
    *out_items = items; *out_count = count; *out_code = code; *out_message = message;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Version comparison & search result deduplication                    */
/* ------------------------------------------------------------------ */

static int read_version_number(const char **p, long long *value)
{
    while (**p && (**p < '0' || **p > '9')) (*p)++;
    if (!**p) return 0;
    char *end = NULL;
    *value = strtoll(*p, &end, 10);
    *p = end;
    return 1;
}

static int compare_versions(const char *a, const char *b)
{
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    const char *pa = a, *pb = b;
    for (;;) {
        long long va = 0, vb = 0;
        int ha = read_version_number(&pa, &va);
        int hb = read_version_number(&pb, &vb);
        if (!ha || !hb) break;
        if (va < vb) return -1;
        if (va > vb) return 1;
    }
    return strcmp(a, b);
}

static bool same_package_slot(const LinyapsPackageInfo *a, const LinyapsPackageInfo *b)
{
    const char *ar = a->repo   ? a->repo   : "";
    const char *br = b->repo   ? b->repo   : "";
    const char *ai = a->id     ? a->id     : "";
    const char *bi = b->id     ? b->id     : "";
    const char *am = a->module ? a->module : "";
    const char *bm = b->module ? b->module : "";
    return strcmp(ar, br) == 0 && strcmp(ai, bi) == 0 && strcmp(am, bm) == 0;
}

void filter_store_search_results(LinyapsPackageInfo ***items, size_t *count)
{
    if (!items || !*items || !count) return;
    LinyapsPackageInfo **list = *items;
    size_t out = 0;
    for (size_t i = 0; i < *count; i++) {
        LinyapsPackageInfo *candidate = list[i];
        if (!candidate) continue;
        if (candidate->module && strcmp(candidate->module, "develop") == 0) {
            linyaps_package_info_free(candidate);
            continue;
        }
        size_t found = out;
        for (size_t j = 0; j < out; j++) {
            if (same_package_slot(list[j], candidate)) { found = j; break; }
        }
        if (found == out) { list[out++] = candidate; continue; }
        if (compare_versions(list[found]->version, candidate->version) < 0) {
            linyaps_package_info_free(list[found]);
            list[found] = candidate;
        } else {
            linyaps_package_info_free(candidate);
        }
    }
    *count = out;
}

/* ------------------------------------------------------------------ */
/* Configured repos query                                               */
/* ------------------------------------------------------------------ */

static int csv_append(char **csv, const char *repo)
{
    if (!repo || !*repo) return 0;
    size_t old_len = *csv ? strlen(*csv) : 0;
    size_t add_len = strlen(repo);
    char *next = realloc(*csv, old_len + add_len + (old_len ? 2 : 1));
    if (!next) return -ENOMEM;
    if (old_len) next[old_len++] = ',';
    memcpy(next + old_len, repo, add_len + 1);
    *csv = next;
    return 0;
}

static int parse_repo_variant(sd_bus_message *m, char **csv)
{
    int r = sd_bus_message_enter_container(m, 'v', "a{sv}");
    if (r < 0) return r;
    r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) return r;
    char *name = NULL, *alias = NULL;
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(m, 's', &key);
        sd_bus_message_peek_type(m, &type, &sig);
        if      (strcmp(key, "alias") == 0) r = read_variant_string(m, sig, &alias);
        else if (strcmp(key, "name") == 0)  r = read_variant_string(m, sig, &name);
        else                                r = sd_bus_message_skip(m, "v");
        if (r < 0) { free(name); free(alias); return r; }
        if ((r = sd_bus_message_exit_container(m)) < 0) { free(name); free(alias); return r; }
    }
    if (r < 0) { free(name); free(alias); return r; }
    r = sd_bus_message_exit_container(m);
    if (r >= 0) r = sd_bus_message_exit_container(m);
    if (r >= 0) r = csv_append(csv, alias ? alias : name);
    free(name); free(alias);
    return r;
}

char *configured_repos_csv(LinyapsContext *ctx)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    char *csv = NULL;
    int r = sd_bus_get_property(ctx->bus, PM_DEST, PM_PATH, PM_IFACE,
                                "Configuration", &err, &reply, "a{sv}");
    if (r < 0) { sd_bus_error_free(&err); sd_bus_message_unref(reply); return NULL; }
    r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    while (r >= 0 && (r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(reply, 's', &key);
        sd_bus_message_peek_type(reply, &type, &sig);
        if (strcmp(key, "repos") == 0 && sig && strcmp(sig, "av") == 0) {
            r = sd_bus_message_enter_container(reply, 'v', "av");
            if (r >= 0) r = sd_bus_message_enter_container(reply, 'a', "v");
            while (r >= 0 && (r = sd_bus_message_peek_type(reply, &type, &sig)) > 0) {
                if (type != 'v') break;
                r = parse_repo_variant(reply, &csv);
                if (r < 0) break;
            }
            if (r == 0) r = sd_bus_message_exit_container(reply);
            if (r >= 0) r = sd_bus_message_exit_container(reply);
        } else {
            r = sd_bus_message_skip(reply, "v");
        }
        if (r < 0) break;
        r = sd_bus_message_exit_container(reply);
    }
    if (r >= 0) sd_bus_message_exit_container(reply);
    if (r < 0) { free(csv); csv = NULL; }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return csv;
}
