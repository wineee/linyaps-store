/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * linyaps_remote.h — 远端商店 API 客户端
 *
 * API base: https://storeapi.linyaps.org.cn
 *
 * 主要接口：
 *   POST /visit/getSearchAppList  —— 搜索 / 全量列表
 *   GET  /visit/getDisCategoryList —— 分类列表
 *
 * 所有调用均为同步（阻塞），建议在后台线程调用，
 * 或在已有 dirty-flag 事件循环中容忍短暂阻塞（首屏 < 1s）。
 */

#pragma once

#include "linyaps_types.h"

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 配置                                                                 */
/* ------------------------------------------------------------------ */

#define LINYAPS_REMOTE_BASE_URL  "https://storeapi.linyaps.org.cn"
#define LINYAPS_REMOTE_REPO_NAME "stable"
#define LINYAPS_REMOTE_PAGE_SIZE 30
#define LINYAPS_REMOTE_TIMEOUT_SEC 15L

/* ------------------------------------------------------------------ */
/* 远端应用信息（扩展字段，本地 LinyapsPackageInfo 没有的）             */
/* ------------------------------------------------------------------ */

typedef struct LinyapsRemoteAppInfo {
    /* 复用基础结构（id/name/version/arch/channel/repo/description/kind/size） */
    LinyapsPackageInfo base;

    /* 远端额外字段 */
    char *icon_url;       /* CDN 图标 URL，可能为 NULL */
    char *zh_name;        /* 中文名 */
    char *category_id;    /* 分类 ID，如 "01" */
    char *category_name;  /* 分类名，如 "网络应用" */
} LinyapsRemoteAppInfo;

typedef enum {
    LINYAPS_RANKING_NEWEST = 0,
    LINYAPS_RANKING_DOWNLOADS,
} LinyapsRankingType;

/* ------------------------------------------------------------------ */
/* 函数                                                                 */
/* ------------------------------------------------------------------ */

/**
 * linyaps_remote_fetch_apps — 拉取远端应用列表
 *
 * @keyword:    搜索关键词，空字符串表示全量拉取
 * @category_id: 分类过滤，NULL 表示全部
 * @page:       页码（从 1 开始）
 * @page_size:  每页数量（建议 LINYAPS_REMOTE_PAGE_SIZE）
 * @out_count:  [out] 返回的条目数
 * @out_total:  [out] 服务器端总条目数（用于分页判断），可为 NULL
 *
 * 返回：堆分配的 LinyapsRemoteAppInfo* 数组，调用方负责释放。
 *       失败返回 NULL，*out_count == 0。
 */
LinyapsRemoteAppInfo **linyaps_remote_fetch_apps(
    const char *keyword,
    const char *category_id,
    int page,
    int page_size,
    size_t *out_count,
    long   *out_total);

/**
 * linyaps_remote_fetch_welcome_apps — 拉取推荐应用列表
 */
LinyapsRemoteAppInfo **linyaps_remote_fetch_welcome_apps(
    int page,
    int page_size,
    size_t *out_count,
    long   *out_total);

/**
 * linyaps_remote_fetch_ranking — 拉取排行列表
 */
LinyapsRemoteAppInfo **linyaps_remote_fetch_ranking(
    LinyapsRankingType type,
    int                page,
    int                page_size,
    size_t            *out_count,
    long              *out_total);

/**
 * linyaps_remote_app_info_free — 释放单个 LinyapsRemoteAppInfo
 */
void linyaps_remote_app_info_free(LinyapsRemoteAppInfo *info);

/**
 * linyaps_remote_app_info_list_free — 释放数组
 */
void linyaps_remote_app_info_list_free(LinyapsRemoteAppInfo **list, size_t count);

/**
 * linyaps_remote_app_info_to_package_info — 浅拷贝为 LinyapsPackageInfo
 *
 * 返回的结构体独立持有字符串，需用 linyaps_package_info_free 释放。
 */
LinyapsPackageInfo *linyaps_remote_app_info_to_package_info(const LinyapsRemoteAppInfo *info);

#ifdef __cplusplus
}
#endif
