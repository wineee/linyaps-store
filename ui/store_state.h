/* ui/store_state.h — linyaps-store application state */

#pragma once

#include "../lib/linyaps_backend.h"

#include <stdbool.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Navigation                                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    NAV_RECOMMENDED = 0,
    NAV_ALL,
    NAV_RANKING,
    NAV_UPDATES,
    /* --- category group --- */
    NAV_CAT_OFFICE,
    NAV_CAT_SYSTEM,
    NAV_CAT_DEV,
    NAV_CAT_GAMES,
    NAV_COUNT,
} NavItem;

static const char *const NAV_LABELS[NAV_COUNT] = {
    "推荐",
    "全部",
    "排行",
    "更新",
    "办公",
    "系统",
    "开发",
    "娱乐",
};

/* ------------------------------------------------------------------ */
/* Category tabs (top bar)                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    CAT_ALL = 0,
    CAT_NETWORK,
    CAT_SOCIAL,
    CAT_DEV,
    CAT_VIDEO,
    CAT_EDU,
    CAT_GAMES,
    CAT_OFFICE,
    CAT_SYSTEM,
    CAT_LIFESTYLE,
    CAT_COUNT,
} CategoryTab;

static const char *const CAT_LABELS[CAT_COUNT] = {
    "全部",
    "网络应用",
    "社交通讯",
    "编程开发",
    "视频播放",
    "教育学习",
    "游戏娱乐",
    "效率办公",
    "系统工具",
    "便捷生活",
};

/* ------------------------------------------------------------------ */
/* App entry (UI-level, mirrors LinyapsPackageInfo + installed flag)   */
/* ------------------------------------------------------------------ */

typedef struct {
    const LinyapsPackageInfo *info;
    bool installed;          /* true → show "打开", false → show "安装" */
    float install_progress;  /* 0.0 when idle, 0–1 while installing     */
    bool  installing;
} AppEntry;

typedef struct {
    char *id;
    char *name;
    char *current_version;
    char *new_version;
    char *channel;
    bool updating;
    float progress;
    char *task_path;
} StoreUpdateItem;

/* ------------------------------------------------------------------ */
/* Global store state                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    LinyapsContext *ctx;

    /* navigation */
    NavItem     active_nav;
    CategoryTab active_cat;
    int         cat_scroll_x;  /* horizontal scroll offset for cat tabs */

    /* search */
    char search_buf[256];
    bool search_focused;

    /* package data */
    LinyapsPackageInfo **search_results;
    size_t               search_count;
    LinyapsPackageInfo **installed_list;
    size_t               installed_count;
    bool                 loading_remote;

    /* updates */
    StoreUpdateItem *update_list;
    size_t           update_count;
    bool             checking_updates;
    float            check_updates_timer;

    /* theme */
    bool dark_mode;

    /* ranking */
    int                  ranking_tab;      /* 0 = 最新上架榜, 1 = 下载量榜 */
    bool                 loading_ranking;
    LinyapsPackageInfo **ranking_list;
    size_t               ranking_count;
    long                 ranking_total;

    /* pagination */
    int current_page;

    /* dirty flag driven rendering */
    bool dirty;
} StoreState;

void store_state_init(StoreState *s, LinyapsContext *ctx);
void store_state_free(StoreState *s);
