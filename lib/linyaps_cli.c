/* SPDX-License-Identifier: LGPL-3.0-or-later */

#include "linyaps_private.h"

#include <cJSON.h>

/* ------------------------------------------------------------------ */
/* JSON helpers                                                         */
/* ------------------------------------------------------------------ */

static char *json_dup_string(const cJSON *obj, const char *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
    if (!cJSON_IsString(item) || !item->valuestring) return NULL;
    return dupstr(item->valuestring);
}

static int64_t json_int64(const cJSON *obj, const char *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
    if (!cJSON_IsNumber(item)) return 0;
    return (int64_t)item->valuedouble;
}

static char *json_first_string(const cJSON *obj, const char *field)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(obj, field);
    const cJSON *item = cJSON_GetArrayItem(array, 0);
    if (!cJSON_IsString(item) || !item->valuestring) return NULL;
    return dupstr(item->valuestring);
}

static LinyapsPackageInfo *package_info_from_json_object(const cJSON *obj, const char *repo)
{
    LinyapsPackageInfo *info = calloc(1, sizeof(*info));
    if (!info) return NULL;
    info->id            = json_dup_string(obj, "id");
    info->name          = json_dup_string(obj, "name");
    info->version       = json_dup_string(obj, "version");
    info->channel       = json_dup_string(obj, "channel");
    info->description   = json_dup_string(obj, "description");
    info->kind          = json_dup_string(obj, "kind");
    info->module        = json_dup_string(obj, "module");
    info->arch          = json_first_string(obj, "arch");
    info->base          = json_dup_string(obj, "base");
    info->runtime       = json_dup_string(obj, "runtime");
    info->schema_version = json_dup_string(obj, "schema_version");
    info->command       = json_first_string(obj, "command");
    info->repo          = dupstr(repo);
    info->size          = json_int64(obj, "size");
    if (!info->id) { linyaps_package_info_free(info); return NULL; }
    return info;
}

/* ------------------------------------------------------------------ */
/* Process capture                                                      */
/* ------------------------------------------------------------------ */

static char *run_command_capture(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    size_t capbuf = 8192, len = 0;
    char *buf = malloc(capbuf);
    if (!buf) { pclose(fp); return NULL; }
    for (;;) {
        if (len + 4096 + 1 > capbuf) {
            capbuf *= 2;
            char *tmp = realloc(buf, capbuf);
            if (!tmp) { free(buf); pclose(fp); return NULL; }
            buf = tmp;
        }
        size_t n = fread(buf + len, 1, 4096, fp);
        len += n;
        if (n < 4096) {
            if (feof(fp)) break;
            if (ferror(fp)) { free(buf); pclose(fp); return NULL; }
        }
    }
    pclose(fp);
    buf[len] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ */
/* linyaps_list_installed                                               */
/* ------------------------------------------------------------------ */

LinyapsPackageInfo **linyaps_list_installed(LinyapsContext *ctx, size_t *out_count)
{
    (void)ctx;
    if (out_count) *out_count = 0;
    char *buf = run_command_capture("ll-cli list --json");
    if (!buf) return NULL;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); return NULL; }
    LinyapsPackageInfo **items = NULL;
    size_t count = 0, cap = 0;
    const cJSON *obj = NULL;
    cJSON_ArrayForEach(obj, root) {
        if (cJSON_IsObject(obj)) {
            LinyapsPackageInfo *info = package_info_from_json_object(obj, "local");
            if (info) package_list_append(&items, &count, &cap, info);
        }
    }
    cJSON_Delete(root);
    if (out_count) *out_count = count;
    return items;
}

/* ------------------------------------------------------------------ */
/* linyaps_info                                                         */
/* ------------------------------------------------------------------ */

static int shell_safe_app_id(const char *app_id)
{
    if (!app_id || !*app_id) return 0;
    for (const char *p = app_id; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '.' || *p == '-' || *p == '_' || *p == '/') continue;
        return 0;
    }
    return 1;
}

LinyapsPackageInfo *linyaps_info(LinyapsContext *ctx, const char *app_id)
{
    (void)ctx;
    if (!shell_safe_app_id(app_id)) return NULL;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ll-cli --json info %s", app_id);
    char *buf = run_command_capture(cmd);
    if (!buf) return NULL;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return NULL; }
    LinyapsPackageInfo *info = package_info_from_json_object(root, "local");
    cJSON_Delete(root);
    return info;
}
