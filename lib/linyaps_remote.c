/* SPDX-License-Identifier: LGPL-3.0-or-later */

#include "linyaps_remote.h"
#include "linyaps_log.h"

#include <cJSON.h>
#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

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
/* 内部：JSON 字符串/数值辅助                                           */
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
    if (cJSON_IsString(item) && item->valuestring)
        return (int64_t)atoll(item->valuestring);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 内部：添加系统架构到请求                                              */
/* ------------------------------------------------------------------ */

static void add_system_arch(cJSON *req)
{
    char arch_buf[32] = "x86_64";
    struct utsname uts;
    if (uname(&uts) == 0 && uts.machine[0] != '\0') {
        snprintf(arch_buf, sizeof(arch_buf), "%s", uts.machine);
    }
    cJSON_AddStringToObject(req, "arch", arch_buf);
}

/* ------------------------------------------------------------------ */
/* 内部：给请求添加公共分页字段                                          */
/* ------------------------------------------------------------------ */

static cJSON *new_page_request(int page, int page_size)
{
    cJSON *req = cJSON_CreateObject();
    if (!req) return NULL;
    cJSON_AddStringToObject(req, "repoName", LINYAPS_REMOTE_REPO_NAME);
    cJSON_AddNumberToObject(req, "pageNo",   page);
    cJSON_AddNumberToObject(req, "pageSize", page_size > 0 ? page_size : LINYAPS_REMOTE_PAGE_SIZE);
    add_system_arch(req);
    return req;
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
    
    /* Name fallbacks */
    info->base.name        = jdup(obj, "zhName");
    if (!info->base.name) info->base.name = jdup(obj, "appName");
    if (!info->base.name) info->base.name = jdup(obj, "name");
    
    /* Version fallbacks */
    info->base.version     = jdup(obj, "version");
    if (!info->base.version) info->base.version = jdup(obj, "appVersion");
    
    info->base.arch        = jdup(obj, "arch");
    info->base.channel     = jdup(obj, "channel");
    info->base.repo        = jdup(obj, "repoName");
    
    /* Description fallbacks */
    info->base.description = jdup(obj, "description");
    if (!info->base.description) info->base.description = jdup(obj, "appDesc");
    
    info->base.kind        = jdup(obj, "kind");
    info->base.module      = jdup(obj, "module");
    info->base.runtime     = jdup(obj, "runtime");
    
    /* Size fallbacks */
    info->base.size        = jint64(obj, "size");
    if (info->base.size == 0) info->base.size = jint64(obj, "packageSize");

    /* remote-only fields */
    info->icon_url      = jdup(obj, "icon");
    if (!info->icon_url) info->icon_url = jdup(obj, "appIcon");
    
    info->zh_name       = jdup(obj, "zhName");
    info->category_id   = jdup(obj, "categoryId");
    info->category_name = jdup(obj, "categoryName");

    info->base.create_time = jdup(obj, "createTime");
    if (!info->base.create_time) info->base.create_time = jdup(obj, "updateTime");
    info->base.download_count = jint64(obj, "installCount");
    if (info->base.download_count == 0) info->base.download_count = jint64(obj, "downloadTimes");

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
/* 内部：解析 records 数组为 AppInfo 列表                                */
/* ------------------------------------------------------------------ */

static LinyapsRemoteAppInfo **parse_records_array(const cJSON *records,
                                                   size_t      *out_count)
{
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

    if (out_count) *out_count = count;
    return list;
}

/* ------------------------------------------------------------------ */
/* 内部：远端 API 通用分页请求                                          */
/*                                                                     */
/* 1. POST JSON → 2. 解析响应 → 3. 验证 code==200 → 4. 提取 records  */
/* ------------------------------------------------------------------ */

static LinyapsRemoteAppInfo **remote_fetch_page(cJSON       *req,
                                                 const char  *url,
                                                 const char  *tag,
                                                 size_t      *out_count,
                                                 long        *out_total)
{
    if (out_count) *out_count = 0;
    if (out_total) *out_total = 0;
    if (!req) return NULL;

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return NULL;

    LOG_DEBUG("remote", "POST %s body=%s", url, body);

    char *resp_raw = http_post_json(url, body);
    free(body);
    if (!resp_raw) {
        LOG_ERR("remote", "[%s] HTTP request failed", tag);
        return NULL;
    }

    cJSON *root = cJSON_Parse(resp_raw);
    free(resp_raw);
    if (!root) {
        LOG_ERR("remote", "[%s] JSON parse error", tag);
        return NULL;
    }

    /* Check code == 200 */
    const cJSON *code_item = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!cJSON_IsNumber(code_item) || (int)code_item->valuedouble != 200) {
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
        LOG_ERR("remote", "[%s] API error: %s", tag,
                cJSON_IsString(msg) ? msg->valuestring : "(no message)");
        cJSON_Delete(root);
        return NULL;
    }

    const cJSON *data    = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *records = cJSON_GetObjectItemCaseSensitive(data, "records");
    const cJSON *total_j = cJSON_GetObjectItemCaseSensitive(data, "total");

    if (!cJSON_IsArray(records)) {
        LOG_WARN("remote", "[%s] no records array in response", tag);
        cJSON_Delete(root);
        return NULL;
    }

    long total = cJSON_IsNumber(total_j) ? (long)total_j->valuedouble : 0;
    if (out_total) *out_total = total;

    size_t count = 0;
    LinyapsRemoteAppInfo **list = parse_records_array(records, &count);

    LOG_INFO("remote", "[%s] fetched records=%zu total=%ld", tag, count, total);

    cJSON_Delete(root);
    if (out_count) *out_count = count;
    return list;
}

/* ================================================================== */
/* 公开 API                                                             */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* 搜索 / 分类                                                          */
/* ------------------------------------------------------------------ */

LinyapsRemoteAppInfo **linyaps_remote_fetch_apps(
    const char *keyword,
    const char *category_id,
    int         page,
    int         page_size,
    size_t     *out_count,
    long       *out_total)
{
    cJSON *req = new_page_request(page, page_size);
    if (!req) return NULL;
    /* 后端期望字段名是 "name"，不是 "keyword" */
    cJSON_AddStringToObject(req, "name", keyword ? keyword : "");
    if (category_id && *category_id)
        cJSON_AddStringToObject(req, "categoryId", category_id);

    char url[256];
    snprintf(url, sizeof(url), "%s/visit/getSearchAppList", LINYAPS_REMOTE_BASE_URL);
    return remote_fetch_page(req, url, "search", out_count, out_total);
}

/* ------------------------------------------------------------------ */
/* 欢迎页                                                              */
/* ------------------------------------------------------------------ */

LinyapsRemoteAppInfo **linyaps_remote_fetch_welcome_apps(
    int page,
    int page_size,
    size_t *out_count,
    long   *out_total)
{
    cJSON *req = new_page_request(page, page_size);
    if (!req) return NULL;

    char url[256];
    snprintf(url, sizeof(url), "%s/visit/getWelcomeAppList", LINYAPS_REMOTE_BASE_URL);
    return remote_fetch_page(req, url, "welcome", out_count, out_total);
}

/* ------------------------------------------------------------------ */
/* 排行榜                                                              */
/* ------------------------------------------------------------------ */

LinyapsRemoteAppInfo **linyaps_remote_fetch_ranking(
    LinyapsRankingType type,
    int                page,
    int                page_size,
    size_t            *out_count,
    long              *out_total)
{
    cJSON *req = new_page_request(page, page_size);
    if (!req) return NULL;

    char url[256];
    if (type == LINYAPS_RANKING_NEWEST) {
        snprintf(url, sizeof(url), "%s/visit/getNewAppList", LINYAPS_REMOTE_BASE_URL);
    } else {
        snprintf(url, sizeof(url), "%s/visit/getInstallAppList", LINYAPS_REMOTE_BASE_URL);
    }
    return remote_fetch_page(req, url, "ranking", out_count, out_total);
}

/* ================================================================== */
/* 批量检查更新                                                         */
/* ================================================================== */

LinyapsRemoteAppInfo **linyaps_remote_check_updates(
    const LinyapsPackageInfo **installed_apps,
    size_t                    installed_count,
    size_t                   *out_count)
{
    if (out_count) *out_count = 0;
    if (!installed_apps || installed_count == 0) return NULL;

    /* 构建请求体: [{"appId": "xxx", "arch": "x86_64", "version": "1.0.0"}, ...] */
    cJSON *req = cJSON_CreateArray();
    if (!req) return NULL;

    char arch_buf[32] = "x86_64";
    struct utsname uts;
    if (uname(&uts) == 0 && uts.machine[0] != '\0') {
        snprintf(arch_buf, sizeof(arch_buf), "%s", uts.machine);
    }

    for (size_t i = 0; i < installed_count; i++) {
        const LinyapsPackageInfo *app = installed_apps[i];
        if (!app || !app->id || !app->version) continue;

        cJSON *item = cJSON_CreateObject();
        if (!item) continue;

        cJSON_AddStringToObject(item, "appId", app->id);
        cJSON_AddStringToObject(item, "arch", app->arch ? app->arch : arch_buf);
        cJSON_AddStringToObject(item, "version", app->version);
        cJSON_AddItemToArray(req, item);
    }

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return NULL;

    LOG_DEBUG("remote", "POST %s/app/appCheckUpdate body=%s", LINYAPS_REMOTE_BASE_URL, body);

    char url[256];
    snprintf(url, sizeof(url), "%s/app/appCheckUpdate", LINYAPS_REMOTE_BASE_URL);

    char *resp_raw = http_post_json(url, body);
    free(body);
    if (!resp_raw) {
        LOG_ERR("remote", "[check_updates] HTTP request failed");
        return NULL;
    }

    cJSON *root = cJSON_Parse(resp_raw);
    free(resp_raw);
    if (!root) {
        LOG_ERR("remote", "[check_updates] JSON parse error");
        return NULL;
    }

    /* 检查 code == 200 */
    const cJSON *code_item = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!cJSON_IsNumber(code_item) || (int)code_item->valuedouble != 200) {
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
        LOG_ERR("remote", "[check_updates] API error: %s",
                cJSON_IsString(msg) ? msg->valuestring : "(no message)");
        cJSON_Delete(root);
        return NULL;
    }

    /* 解析 data 数组 (直接是 AppDetailDTO 数组) */
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        LOG_WARN("remote", "[check_updates] no data array in response");
        cJSON_Delete(root);
        return NULL;
    }

    size_t count = 0;
    LinyapsRemoteAppInfo **list = parse_records_array(data, &count);

    LOG_INFO("remote", "[check_updates] returned %zu apps with updates", count);

    cJSON_Delete(root);
    if (out_count) *out_count = count;
    return list;
}

/* ================================================================== */
/* 内存管理                                                             */
/* ================================================================== */

void linyaps_remote_app_info_free(LinyapsRemoteAppInfo *info)
{
    if (!info) return;
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
    free(info->base.create_time);
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
    DUP(create_time);
#undef DUP
    p->size = info->base.size;
    p->download_count = info->base.download_count;
    return p;
}
