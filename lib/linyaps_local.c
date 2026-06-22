/* SPDX-License-Identifier: MIT */
/**
 * linyaps_local.c — 读取本地已安装应用信息
 *
 * 直接读取 /var/lib/linglong/states.json，避免 subprocess 开销。
 * 仅在 states.json 不存在时 fallback 到 ll-cli。
 */

#include "linyaps_private.h"

#include <cJSON.h>

/* ------------------------------------------------------------------ */
/* JSON helpers                                                         */
/* ------------------------------------------------------------------ */

static char *json_dup_string(const cJSON *obj, const char *field) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
  if (!cJSON_IsString(item) || !item->valuestring)
    return NULL;
  return dupstr(item->valuestring);
}

static int64_t json_int64(const cJSON *obj, const char *field) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
  if (!cJSON_IsNumber(item))
    return 0;
  return (int64_t)item->valuedouble;
}

static char *json_first_string(const cJSON *obj, const char *field) {
  const cJSON *array = cJSON_GetObjectItemCaseSensitive(obj, field);
  const cJSON *item = cJSON_GetArrayItem(array, 0);
  if (!cJSON_IsString(item) || !item->valuestring)
    return NULL;
  return dupstr(item->valuestring);
}

static LinyapsPackageInfo *package_info_from_json_object(const cJSON *obj,
                                                         const char *repo) {
  LinyapsPackageInfo *info = calloc(1, sizeof(*info));
  if (!info)
    return NULL;
  info->id = json_dup_string(obj, "id");
  info->name = json_dup_string(obj, "name");
  info->version = json_dup_string(obj, "version");
  info->channel = json_dup_string(obj, "channel");
  info->description = json_dup_string(obj, "description");
  info->kind = json_dup_string(obj, "kind");
  info->module = json_dup_string(obj, "module");
  info->arch = json_first_string(obj, "arch");
  info->base = json_dup_string(obj, "base");
  info->runtime = json_dup_string(obj, "runtime");
  info->schema_version = json_dup_string(obj, "schema_version");
  info->command = json_first_string(obj, "command");
  info->repo = dupstr(repo);
  info->size = json_int64(obj, "size");
  if (!info->id) {
    linyaps_package_info_free(info);
    return NULL;
  }
  return info;
}

/* ------------------------------------------------------------------ */
/* 文件读取                                                             */
/* ------------------------------------------------------------------ */

#define LINGLONG_STATES_FILE "/var/lib/linglong/states.json"

/**
 * 读取整个文件到内存。返回的字符串需要 free()。
 * 失败时返回 NULL。
 */
static char *read_file_contents(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    return NULL;

  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (file_size <= 0) {
    fclose(fp);
    return NULL;
  }

  char *buf = malloc((size_t)file_size + 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }

  size_t nread = fread(buf, 1, (size_t)file_size, fp);
  fclose(fp);
  buf[nread] = '\0';
  return buf;
}

/* ------------------------------------------------------------------ */
/* Subprocess fallback（仅在 states.json 不存在时使用）                  */
/* ------------------------------------------------------------------ */

static char *run_command_capture(const char *cmd) {
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return NULL;
  size_t capbuf = 8192, len = 0;
  char *buf = malloc(capbuf);
  if (!buf) {
    pclose(fp);
    return NULL;
  }
  for (;;) {
    if (len + 4096 + 1 > capbuf) {
      capbuf *= 2;
      char *tmp = realloc(buf, capbuf);
      if (!tmp) {
        free(buf);
        pclose(fp);
        return NULL;
      }
      buf = tmp;
    }
    size_t n = fread(buf + len, 1, 4096, fp);
    len += n;
    if (n < 4096) {
      if (feof(fp))
        break;
      if (ferror(fp)) {
        free(buf);
        pclose(fp);
        return NULL;
      }
    }
  }
  pclose(fp);
  buf[len] = '\0';
  return buf;
}

/* ------------------------------------------------------------------ */
/* states.json 解析                                                     */
/* ------------------------------------------------------------------ */

/**
 * 从 states.json 的 layers 数组中解析所有已安装应用。
 * 只返回 kind=app 的条目（跳过 runtime/base）。
 */
static LinyapsPackageInfo **parse_states_json(const cJSON *root,
                                              size_t *out_count) {
  /* states.json 格式: { "layers": [ { "info": {...}, "repo": "..." }, ... ] }
   */
  const cJSON *layers = cJSON_GetObjectItemCaseSensitive(root, "layers");
  if (!cJSON_IsArray(layers)) {
    /* 兼容旧格式: 直接是数组 (ll-cli list --json 输出) */
    if (cJSON_IsArray(root)) {
      layers = root;
    } else {
      if (out_count)
        *out_count = 0;
      return NULL;
    }
  }

  LinyapsPackageInfo **items = NULL;
  size_t count = 0, cap = 0;
  const cJSON *item = NULL;
  cJSON_ArrayForEach(item, layers) {
    /* states.json 中每个 item 有 "info" 和 "repo" 字段 */
    const cJSON *info_obj = cJSON_GetObjectItemCaseSensitive(item, "info");
    const cJSON *repo_obj = cJSON_GetObjectItemCaseSensitive(item, "repo");

    /* 如果没有 "info" 字段，可能是旧格式，直接使用 item */
    if (!info_obj || !cJSON_IsObject(info_obj)) {
      info_obj = item;
    }

    /* 只返回 kind=app 的应用（与 ll-cli 行为一致） */
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(info_obj, "kind");
    if (cJSON_IsString(kind) && strcmp(kind->valuestring, "app") != 0) {
      continue; /* 跳过 runtime/base 等非应用类型 */
    }

    const char *repo = "local";
    if (cJSON_IsString(repo_obj)) {
      repo = repo_obj->valuestring;
    }

    LinyapsPackageInfo *info = package_info_from_json_object(info_obj, repo);
    if (info)
      package_list_append(&items, &count, &cap, info);
  }

  if (out_count)
    *out_count = count;
  return items;
}

/* ------------------------------------------------------------------ */
/* 公共 API                                                             */
/* ------------------------------------------------------------------ */

/**
 * 获取所有已安装应用列表。
 * 优先读取 states.json，失败时 fallback 到 ll-cli。
 */
LinyapsPackageInfo **linyaps_list_installed(LinyapsContext *ctx,
                                            size_t *out_count) {
  (void)ctx;
  if (out_count)
    *out_count = 0;

  /* 直接读取 states.json，避免 ll-cli subprocess 开销 */
  char *buf = read_file_contents(LINGLONG_STATES_FILE);
  if (!buf) {
    /* fallback: 尝试 ll-cli */
    buf = run_command_capture("ll-cli list --json");
    if (!buf)
      return NULL;
  }

  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if (!root)
    return NULL;

  LinyapsPackageInfo **items = parse_states_json(root, out_count);
  cJSON_Delete(root);
  return items;
}

/**
 * 获取单个应用的详细信息。
 * 从 states.json 的 layers 数组中查找匹配的 app_id。
 */
LinyapsPackageInfo *linyaps_info(LinyapsContext *ctx, const char *app_id) {
  (void)ctx;
  if (!app_id || !*app_id)
    return NULL;

  /* 从 states.json 查找 */
  char *buf = read_file_contents(LINGLONG_STATES_FILE);
  if (buf) {
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (root) {
      const cJSON *layers = cJSON_GetObjectItemCaseSensitive(root, "layers");
      if (cJSON_IsArray(layers)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, layers) {
          const cJSON *info_obj =
              cJSON_GetObjectItemCaseSensitive(item, "info");
          const cJSON *repo_obj =
              cJSON_GetObjectItemCaseSensitive(item, "repo");
          if (!info_obj || !cJSON_IsObject(info_obj))
            info_obj = item;

          const cJSON *id_item =
              cJSON_GetObjectItemCaseSensitive(info_obj, "id");
          if (cJSON_IsString(id_item) &&
              strcmp(id_item->valuestring, app_id) == 0) {
            const char *repo = "local";
            if (cJSON_IsString(repo_obj))
              repo = repo_obj->valuestring;
            LinyapsPackageInfo *info =
                package_info_from_json_object(info_obj, repo);
            cJSON_Delete(root);
            return info;
          }
        }
      }
      cJSON_Delete(root);
    }
  }

  /* fallback: ll-cli */
  /* 验证 app_id 只包含安全字符 */
  for (const char *p = app_id; *p; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
        (*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '_' ||
        *p == '/')
      continue;
    return NULL; /* 包含不安全字符 */
  }

  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "ll-cli --json info %s", app_id);
  buf = run_command_capture(cmd);
  if (!buf)
    return NULL;

  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return NULL;
  }

  LinyapsPackageInfo *info = package_info_from_json_object(root, "local");
  cJSON_Delete(root);
  return info;
}
