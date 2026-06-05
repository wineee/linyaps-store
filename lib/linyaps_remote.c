/* SPDX-License-Identifier: LGPL-3.0-or-later */

#include "linyaps_remote.h"
#include "linyaps_log.h"

#include <cJSON.h>
#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 内部：curl 写回调                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} CurlBuf;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    CurlBuf *cb  = userdata;
    size_t   n   = size * nmemb;
    if (cb->len + n + 1 > cb->cap) {
        size_t newcap = cb->cap == 0 ? 4096 : cb->cap * 2;
        while (newcap < cb->len + n + 1) newcap *= 2;
        char *tmp = realloc(cb->buf, newcap);
        if (!tmp) return 0;
        cb->buf = tmp;
        cb->cap = newcap;
    }
    memcpy(cb->buf + cb->len, ptr, n);
    cb->len += n;
    cb->buf[cb->len] = '\0';
    return n;
}

/* ------------------------------------------------------------------ */
/* 内部：HTTP POST JSON                                                 */
/* ------------------------------------------------------------------ */

static char *http_post_json(const char *url, const char *body)
{
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    CurlBuf resp = { NULL, 0, 0 };

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    hdrs = curl_slist_append(hdrs, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, LINYAPS_REMOTE_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    /* Don't verify SSL for dev/offline environments — change in production */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        LOG_ERR("remote", "curl error: %s", curl_easy_strerror(rc));
        free(resp.buf);
        resp.buf = NULL;
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return resp.buf;
}

/* ------------------------------------------------------------------ */
/* 内部：JSON 字符串辅助                                                */
/* ------------------------------------------------------------------ */

static char *jdup(const cJSON *obj, const char *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
    if (!cJSON_IsString(item) || !item->valuestring) return NULL;
    return strdup(item->valuestring);
}

static int64_t jint64(const cJSON *obj, const char *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
    if (cJSON_IsNumber(item)) return (int64_t)item->valuedouble;
    /* size may be a string in API response */
    if (cJSON_IsString(item) && item->valuestring)
        return (int64_t)atoll(item->valuestring);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 内部：解析单条记录                                                   */
/* ------------------------------------------------------------------ */

static LinyapsRemoteAppInfo *parse_record(const cJSON *obj)
{
    LinyapsRemoteAppInfo *info = calloc(1, sizeof(*info));
    if (!info) return NULL;

    /* base fields — map API field names to LinyapsPackageInfo */
    info->base.id          = jdup(obj, "appId");
    info->base.name        = jdup(obj, "name");
    info->base.version     = jdup(obj, "version");
    info->base.arch        = jdup(obj, "arch");
    info->base.channel     = jdup(obj, "channel");
    info->base.repo        = jdup(obj, "repoName");
    info->base.description = jdup(obj, "description");
    info->base.kind        = jdup(obj, "kind");
    info->base.module      = jdup(obj, "module");
    info->base.runtime     = jdup(obj, "runtime");
    info->base.size        = jint64(obj, "size");

    /* remote-only fields */
    info->icon_url      = jdup(obj, "icon");
    info->zh_name       = jdup(obj, "zhName");
    info->category_id   = jdup(obj, "categoryId");
    info->category_name = jdup(obj, "categoryName");

    /* Prefer zh_name as the display name when available */
    if (info->zh_name && *info->zh_name && !info->base.name) {
        info->base.name = strdup(info->zh_name);
    }

    if (!info->base.id) {
        linyaps_remote_app_info_free(info);
        return NULL;
    }
    return info;
}

/* ------------------------------------------------------------------ */
/* 公开：linyaps_remote_fetch_apps                                      */
/* ------------------------------------------------------------------ */

LinyapsRemoteAppInfo **linyaps_remote_fetch_apps(
    const char *keyword,
    const char *category_id,
    int         page,
    int         page_size,
    size_t     *out_count,
    long       *out_total)
{
    if (out_count) *out_count = 0;
    if (out_total) *out_total = 0;

    /* Build JSON request body */
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "keyword",  keyword ? keyword : "");
    cJSON_AddStringToObject(req, "repoName", LINYAPS_REMOTE_REPO_NAME);
    cJSON_AddNumberToObject(req, "pageNo",   page);
    cJSON_AddNumberToObject(req, "pageSize", page_size > 0 ? page_size : LINYAPS_REMOTE_PAGE_SIZE);
    /* arch: detect from uname -m, default x86_64 */
    {
        const char *arch = "x86_64";
        FILE *fp = popen("uname -m 2>/dev/null", "r");
        char arch_buf[32] = "x86_64";
        if (fp) { fgets(arch_buf, sizeof(arch_buf), fp); pclose(fp); }
        size_t al = strlen(arch_buf);
        while (al > 0 && (arch_buf[al-1] == '\n' || arch_buf[al-1] == '\r')) arch_buf[--al] = '\0';
        if (al > 0) arch = arch_buf;
        cJSON_AddStringToObject(req, "arch", arch);
    }
    if (category_id && *category_id)
        cJSON_AddStringToObject(req, "categoryId", category_id);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return NULL;

    char url[256];
    snprintf(url, sizeof(url), "%s/visit/getSearchAppList", LINYAPS_REMOTE_BASE_URL);

    LOG_DEBUG("remote", "POST %s body=%s", url, body);

    char *resp_raw = http_post_json(url, body);
    free(body);

    if (!resp_raw) {
        LOG_ERR("remote", "HTTP request failed");
        return NULL;
    }

    cJSON *root = cJSON_Parse(resp_raw);
    free(resp_raw);

    if (!root) {
        LOG_ERR("remote", "JSON parse error");
        return NULL;
    }

    /* Check code == 200 */
    const cJSON *code_item = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!cJSON_IsNumber(code_item) || (int)code_item->valuedouble != 200) {
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
        LOG_ERR("remote", "API error: %s",
                cJSON_IsString(msg) ? msg->valuestring : "(no message)");
        cJSON_Delete(root);
        return NULL;
    }

    const cJSON *data    = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *records = cJSON_GetObjectItemCaseSensitive(data, "records");
    const cJSON *total_j = cJSON_GetObjectItemCaseSensitive(data, "total");

    if (!cJSON_IsArray(records)) {
        LOG_WARN("remote", "no records array in response");
        cJSON_Delete(root);
        return NULL;
    }

    long total = cJSON_IsNumber(total_j) ? (long)total_j->valuedouble : 0;
    if (out_total) *out_total = total;

    int n = cJSON_GetArraySize(records);
    LOG_INFO("remote", "fetched page=%d records=%d total=%ld", page, n, total);

    LinyapsRemoteAppInfo **list = NULL;
    size_t count = 0, cap = 0;

    const cJSON *obj = NULL;
    cJSON_ArrayForEach(obj, records) {
        LinyapsRemoteAppInfo *info = parse_record(obj);
        if (!info) continue;

        if (count >= cap) {
            cap = cap == 0 ? 32 : cap * 2;
            LinyapsRemoteAppInfo **tmp = realloc(list, cap * sizeof(*list));
            if (!tmp) { linyaps_remote_app_info_free(info); break; }
            list = tmp;
        }
        list[count++] = info;
    }

    cJSON_Delete(root);
    if (out_count) *out_count = count;
    return list;
}

/* ------------------------------------------------------------------ */
/* 公开：内存管理                                                        */
/* ------------------------------------------------------------------ */

void linyaps_remote_app_info_free(LinyapsRemoteAppInfo *info)
{
    if (!info) return;
    /* free base fields */
    free(info->base.id);
    free(info->base.name);
    free(info->base.version);
    free(info->base.arch);
    free(info->base.channel);
    free(info->base.repo);
    free(info->base.description);
    free(info->base.kind);
    free(info->base.module);
    free(info->base.base);
    free(info->base.runtime);
    free(info->base.schema_version);
    free(info->base.command);
    /* remote-only fields */
    free(info->icon_url);
    free(info->zh_name);
    free(info->category_id);
    free(info->category_name);
    free(info);
}

void linyaps_remote_app_info_list_free(LinyapsRemoteAppInfo **list, size_t count)
{
    if (!list) return;
    for (size_t i = 0; i < count; i++) linyaps_remote_app_info_free(list[i]);
    free(list);
}

LinyapsPackageInfo *linyaps_remote_app_info_to_package_info(const LinyapsRemoteAppInfo *info)
{
    if (!info) return NULL;
    LinyapsPackageInfo *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
#define DUP(f) p->f = info->base.f ? strdup(info->base.f) : NULL
    DUP(id); DUP(name); DUP(version); DUP(arch); DUP(channel);
    DUP(repo); DUP(description); DUP(kind); DUP(module);
    DUP(base); DUP(runtime); DUP(schema_version); DUP(command);
#undef DUP
    p->size = info->base.size;
    return p;
}
