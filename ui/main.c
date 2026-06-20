#include "store_state.h"
#include "store_ui.h"

#include "../kilnui/src/kilnui.h"
#include "../kilnui/src/ui/ui.h"
#include "../lib/linyaps_backend.h"
#include "../lib/linyaps_remote.h"
#include "../lib/linyaps_log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Custom SDL user event types (posted by background threads)          */
/* ------------------------------------------------------------------ */

#define EVT_REMOTE_READY          0u
#define EVT_CHECK_UPDATES_READY   1u
#define EVT_UPDATE_ITEM_PROGRESS  2u
#define EVT_UPDATE_ITEM_FINISHED  3u
#define EVT_RANKING_READY         4u

typedef struct {
    LinyapsPackageInfo **list;
    size_t               count;
    long                 total;
    int                  page;
    bool                 is_search;  /* true = keyword search, false = category browse */
} RemoteResult;

static StoreState *g_store = NULL;
static Uint32      g_user_event_type = 0;

/* ------------------------------------------------------------------ */
/* Helper: post an empty user event back to the main thread            */
/* ------------------------------------------------------------------ */

static void post_user_event(int code, void *data1, void *data2)
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type       = g_user_event_type;
    ev.user.code  = code;
    ev.user.data1 = data1;
    ev.user.data2 = data2;
    SDL_PushEvent(&ev);
}

/* ------------------------------------------------------------------ */
/* Category ID lookup helpers                                          */
/* ------------------------------------------------------------------ */

static const char *get_category_id_by_nav(NavItem nav)
{
    switch (nav) {
        case NAV_RECOMMENDED: return "__welcome__";
        case NAV_CAT_OFFICE:  return "07";
        case NAV_CAT_SYSTEM:  return "08";
        case NAV_CAT_DEV:     return "03";
        case NAV_CAT_GAMES:   return "06";
        default:              return NULL;
    }
}

static const char *get_category_id_by_tab(CategoryTab tab)
{
    switch (tab) {
        case CAT_OFFICE: return "07";
        case CAT_SYSTEM: return "08";
        case CAT_DEV:    return "03";
        case CAT_GAMES:  return "06";
        default:          return NULL;
    }
}


/* ------------------------------------------------------------------ */
/* Remote app list fetch thread                                        */
/* ------------------------------------------------------------------ */

/* Argument for fetch_remote_thread: owns keyword and category_id strings */
typedef struct {
    char *keyword;
    char *category_id;
    int   page;
} RemoteFetchArg;

static void *fetch_remote_thread(void *arg)
{
    RemoteFetchArg *farg = arg;
    char *keyword    = farg->keyword;
    char *category_id = farg->category_id;
    int   page = farg->page;
    free(farg);

    size_t count = 0;
    long   total = 0;

    LinyapsRemoteAppInfo **remote = NULL;
    if (category_id && strcmp(category_id, "__welcome__") == 0) {
        remote = linyaps_remote_fetch_welcome_apps(page, 30, &count, &total);
    } else {
        remote = linyaps_remote_fetch_apps(keyword ? keyword : "", category_id, page, 30, &count, &total);
    }

    LinyapsPackageInfo **list = NULL;
    if (remote && count > 0) {
        list = calloc(count, sizeof(*list));
        if (list) {
            for (size_t i = 0; i < count; i++)
                list[i] = linyaps_remote_app_info_to_package_info(remote[i]);
        }
        linyaps_remote_app_info_list_free(remote, count);
    }

    bool is_search = (keyword && *keyword);
    LOG_INFO("remote", "远端拉取完成: keyword=%s category=%s page=%d count=%zu total=%ld",
             keyword ? keyword : "", category_id ? category_id : "NULL", page, count, total);

    RemoteResult *res = malloc(sizeof(*res));
    if (res) {
        res->list = list; res->count = count; res->total = total; res->page = page;
        res->is_search = is_search;
        /* Pass keyword (if search) or category_id (if browse) as data2 */
        post_user_event(EVT_REMOTE_READY, res, is_search ? keyword : category_id);
    } else {
        linyaps_package_info_list_free(list, count);
        free(keyword);
        free(category_id);
    }
    /* Free the one NOT passed as data2 */
    if (is_search) free(category_id); else free(keyword);
    return NULL;
}

static void start_remote_fetch(const char *keyword, const char *category_id, int page)
{
    SDL_SetAtomicInt(&g_store->loading_remote, 1);
    SDL_SetAtomicInt(&g_store->dirty, 1);

    RemoteFetchArg *farg = malloc(sizeof(*farg));
    if (!farg) {
        SDL_SetAtomicInt(&g_store->loading_remote, 0);
        return;
    }
    farg->keyword     = keyword ? strdup(keyword) : NULL;
    farg->category_id = category_id ? strdup(category_id) : NULL;
    farg->page = page;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, fetch_remote_thread, farg) != 0) {
        LOG_WARN("remote", "无法创建后台远端拉取线程");
        free(farg->keyword);
        free(farg->category_id);
        free(farg);
        SDL_SetAtomicInt(&g_store->loading_remote, 0);
    }
    pthread_attr_destroy(&attr);
}

/* ------------------------------------------------------------------ */
/* Ranking fetch thread                                                */
/* ------------------------------------------------------------------ */

static void *fetch_ranking_thread(void *arg)
{
    uintptr_t val = (uintptr_t)arg;
    int tab_type = val & 0xFF;
    int page = (val >> 8) & 0xFFFF;
    if (page == 0) page = 1;

    size_t count = 0;
    long   total = 0;
    LinyapsRankingType rtype = (tab_type == 0)
        ? LINYAPS_RANKING_NEWEST : LINYAPS_RANKING_DOWNLOADS;

    LinyapsRemoteAppInfo **remote = linyaps_remote_fetch_ranking(
        rtype, page, 30, &count, &total);

    LinyapsPackageInfo **list = NULL;
    if (remote && count > 0) {
        list = calloc(count, sizeof(*list));
        if (list) {
            for (size_t i = 0; i < count; i++)
                list[i] = linyaps_remote_app_info_to_package_info(remote[i]);
        }
        linyaps_remote_app_info_list_free(remote, count);
    }

    LOG_INFO("ranking", "排行拉取完成: type=%d page=%d count=%zu total=%ld",
             tab_type, page, count, total);

    RemoteResult *res = malloc(sizeof(*res));
    if (res) {
        res->list = list; res->count = count; res->total = total;
        post_user_event(EVT_RANKING_READY, res, (void *)val);
    } else {
        linyaps_package_info_list_free(list, count);
    }
    return NULL;
}

static void start_ranking_fetch(int tab_type, int page)
{
    SDL_SetAtomicInt(&g_store->loading_ranking, 1);
    SDL_SetAtomicInt(&g_store->dirty, 1);

    uintptr_t val = (tab_type & 0xFF) | ((page & 0xFFFF) << 8);

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, fetch_ranking_thread, (void *)val) != 0) {
        LOG_WARN("ranking", "无法创建后台排行拉取线程");
        SDL_SetAtomicInt(&g_store->loading_ranking, 0);
    }
    pthread_attr_destroy(&attr);
}

/* ------------------------------------------------------------------ */
/* Update progress callback (called from linyaps_process on main thrd) */
/* ------------------------------------------------------------------ */

static void on_update_progress(const LinyapsTaskProgress *prog, void *userdata)
{
    StoreUpdateItem *item = userdata;
    if (!item || !g_store) return;

    SDL_SetAtomicInt(&item->progress_int, (int)(prog->percentage * 100.0f));

    if (prog->state == LINYAPS_TASK_STATE_SUCCEED) {
        post_user_event(EVT_UPDATE_ITEM_FINISHED, item, NULL);
    } else if (prog->state == LINYAPS_TASK_STATE_FAILED ||
               prog->state == LINYAPS_TASK_STATE_CANCELED) {
        SDL_SetAtomicInt(&item->updating, 0);
        SDL_SetAtomicInt(&item->progress_int, 0);
        post_user_event(EVT_UPDATE_ITEM_FINISHED, item, NULL);
    } else {
        post_user_event(EVT_UPDATE_ITEM_PROGRESS, item, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Simulation threads (development only)                               */
/* ------------------------------------------------------------------ */

#ifdef LINYAPS_SIMULATE_UPDATES

static void *check_updates_thread(void *arg)
{
    (void)arg;
    SDL_Delay(1500);
    post_user_event(EVT_CHECK_UPDATES_READY, NULL, NULL);
    return NULL;
}

typedef struct {
    StoreState     *store;
    StoreUpdateItem *item;
} SimulatedUpdateTask;

static void *simulate_update_thread(void *arg)
{
    SimulatedUpdateTask *task = arg;
    StoreUpdateItem *item = task->item;

    float p = 0.0f;
    while (p < 1.0f) {
        SDL_Delay(100);
        p += 0.05f;
        if (p > 1.0f) p = 1.0f;
        SDL_SetAtomicInt(&item->progress_int, (int)(p * 100.0f));
        post_user_event(EVT_UPDATE_ITEM_PROGRESS, item, NULL);
    }
    post_user_event(EVT_UPDATE_ITEM_FINISHED, item, NULL);
    free(task);
    return NULL;
}

static void start_simulated_update(StoreUpdateItem *item)
{
    SimulatedUpdateTask *task = malloc(sizeof(*task));
    if (!task) { SDL_SetAtomicInt(&item->updating, 0); return; }
    task->store = g_store;
    task->item  = item;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, simulate_update_thread, task) != 0) {
        LOG_WARN("updates", "无法创建模拟更新线程");
        SDL_SetAtomicInt(&item->updating, 0);
        SDL_SetAtomicInt(&item->progress_int, 0);
        free(task);
    }
    pthread_attr_destroy(&attr);
}

#endif /* LINYAPS_SIMULATE_UPDATES */

/* ------------------------------------------------------------------ */
/* Check updates thread (real implementation)                          */
/* ------------------------------------------------------------------ */

/**
 * UpdateCheckResult - 存储检查更新结果
 */
typedef struct {
    StoreUpdateItem *items;
    size_t           count;
} UpdateCheckResult;

static void *check_updates_real_thread(void *arg)
{
    (void)arg;
    LOG_INFO("updates", "开始检查应用更新...");

    /* 获取已安装应用列表 */
    size_t installed_count = 0;
    LinyapsPackageInfo **installed = linyaps_list_installed(g_store->ctx, &installed_count);
    if (!installed || installed_count == 0) {
        LOG_INFO("updates", "没有已安装的应用，跳过更新检查");
        linyaps_package_info_list_free(installed, installed_count);
        post_user_event(EVT_CHECK_UPDATES_READY, NULL, NULL);
        return NULL;
    }

    LOG_INFO("updates", "已安装 %zu 个应用，调用批量检查更新 API", installed_count);

    /* 调用批量检查更新 API */
    size_t remote_count = 0;
    LinyapsRemoteAppInfo **remote = linyaps_remote_check_updates(
        (const LinyapsPackageInfo **)installed, installed_count, &remote_count);

    /* 存储有更新的应用 */
    StoreUpdateItem *updates = NULL;
    size_t update_count = 0;

    if (remote && remote_count > 0) {
        /* 构建已安装应用的 ID 到信息的映射 */
        updates = calloc(remote_count, sizeof(StoreUpdateItem));
        if (updates) {
            for (size_t i = 0; i < remote_count; i++) {
                LinyapsRemoteAppInfo *remote_app = remote[i];
                if (!remote_app || !remote_app->base.id || !remote_app->base.version) continue;

                /* 查找对应的已安装应用获取当前版本 */
                const char *current_ver = NULL;
                for (size_t j = 0; j < installed_count; j++) {
                    if (installed[j] && installed[j]->id &&
                        strcmp(installed[j]->id, remote_app->base.id) == 0) {
                        current_ver = installed[j]->version;
                        break;
                    }
                }

                StoreUpdateItem *item = &updates[update_count];
                item->id = strdup(remote_app->base.id);
                item->name = strdup(remote_app->base.name ? remote_app->base.name : remote_app->base.id);
                item->current_version = strdup(current_ver ? current_ver : "0.0.0");
                item->new_version = strdup(remote_app->base.version);
                item->channel = strdup(remote_app->base.channel ? remote_app->base.channel : "main");
                SDL_SetAtomicInt(&item->updating, 0);
                SDL_SetAtomicInt(&item->progress_int, 0);
                item->task_path = NULL;
                update_count++;

                LOG_INFO("updates", "发现更新: %s %s -> %s",
                         remote_app->base.id,
                         current_ver ? current_ver : "0.0.0",
                         remote_app->base.version);
            }
        }
        linyaps_remote_app_info_list_free(remote, remote_count);
    }

    linyaps_package_info_list_free(installed, installed_count);

    /* 发送结果到主线程 */
    UpdateCheckResult *result = malloc(sizeof(UpdateCheckResult));
    if (result) {
        result->items = updates;
        result->count = update_count;
        post_user_event(EVT_CHECK_UPDATES_READY, result, NULL);
    } else {
        /* 内存分配失败，释放更新列表 */
        for (size_t i = 0; i < update_count; i++) {
            free(updates[i].id);
            free(updates[i].name);
            free(updates[i].current_version);
            free(updates[i].new_version);
            free(updates[i].channel);
            free(updates[i].task_path);
        }
        free(updates);
        post_user_event(EVT_CHECK_UPDATES_READY, NULL, NULL);
    }

    LOG_INFO("updates", "更新检查完成，发现 %zu 个可更新应用", update_count);
    return NULL;
}

void store_ui_trigger_check_updates(void)
{
    if (!g_store || SDL_GetAtomicInt(&g_store->checking_updates)) return;
    SDL_SetAtomicInt(&g_store->checking_updates, 1);
    SDL_SetAtomicInt(&g_store->dirty, 1);

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, check_updates_real_thread, NULL) != 0) {
        LOG_WARN("updates", "无法创建检查更新线程");
        SDL_SetAtomicInt(&g_store->checking_updates, 0);
    }
    pthread_attr_destroy(&attr);
}

void store_ui_trigger_update_item(StoreUpdateItem *item)
{
    if (!g_store || !item || SDL_GetAtomicInt(&item->updating)) return;

    SDL_SetAtomicInt(&item->updating, 1);
    SDL_SetAtomicInt(&item->progress_int, 0);
    SDL_SetAtomicInt(&g_store->dirty, 1);

    bool dispatched = false;
    if (g_store->ctx) {
        for (size_t i = 0; i < g_store->installed_count; i++) {
            if (g_store->installed_list[i] && g_store->installed_list[i]->id &&
                strcmp(g_store->installed_list[i]->id, item->id) == 0) {
                linyaps_update(g_store->ctx, item->id,
                               g_store->installed_list[i]->channel,
                               on_update_progress, item);
                dispatched = true;
                break;
            }
        }
    }

#ifdef LINYAPS_SIMULATE_UPDATES
    if (!dispatched) start_simulated_update(item);
#else
    if (!dispatched) {
        LOG_INFO("updates", "未安装 %s，且模拟未启用", item->id);
        SDL_SetAtomicInt(&item->updating, 0);
    }
#endif
}

void store_ui_trigger_update_all(void)
{
    if (!g_store) return;
    for (size_t i = 0; i < g_store->update_count; i++) {
        if (!SDL_GetAtomicInt(&g_store->update_list[i].updating))
            store_ui_trigger_update_item(&g_store->update_list[i]);
    }
}

void store_ui_trigger_change_nav(NavItem item)
{
    if (g_store->active_nav == item && !g_store->is_searching) return;
    g_store->active_nav = item;
    g_store->current_page = 0;
    g_store->remote_total = 0;
    g_store->is_searching = false;
    SDL_SetAtomicInt(&g_store->dirty, 1);

    if (item == NAV_RANKING) {
        start_ranking_fetch(g_store->ranking_tab, g_store->current_page + 1);
    } else if (item >= NAV_CAT_OFFICE && item <= NAV_CAT_GAMES) {
        start_remote_fetch(NULL, get_category_id_by_nav(item), 1);
    } else if (item == NAV_ALL) {
        g_store->active_cat = CAT_ALL;
        start_remote_fetch(NULL, NULL, 1);
    } else if (item == NAV_RECOMMENDED) {
        g_store->active_cat = CAT_ALL;
        start_remote_fetch(NULL, "__welcome__", 1);
    }
}

void store_ui_trigger_change_category_tab(CategoryTab tab)
{
    if (g_store->active_cat == tab) return;
    g_store->active_cat = tab;
    g_store->current_page = 0;
    g_store->remote_total = 0;
    SDL_SetAtomicInt(&g_store->dirty, 1);
    start_remote_fetch(NULL, get_category_id_by_tab(tab), 1);
}

void store_ui_trigger_change_ranking_tab(int tab_idx)
{
    if (g_store->ranking_tab == tab_idx) return;
    g_store->ranking_tab = tab_idx;
    g_store->current_page = 0;
    SDL_SetAtomicInt(&g_store->dirty, 1);
    start_ranking_fetch(tab_idx, 1);
}

void store_ui_trigger_clear_search(void)
{
    if (!g_store) return;
    g_store->search_buf[0] = '\0';
    g_store->is_searching = false;
    SDL_SetAtomicInt(&g_store->dirty, 1);

    /* Restore view based on current nav */
    NavItem item = g_store->active_nav;
    if (item == NAV_RANKING) {
        start_ranking_fetch(g_store->ranking_tab, g_store->current_page + 1);
    } else if (item >= NAV_CAT_OFFICE && item <= NAV_CAT_GAMES) {
        start_remote_fetch(NULL, get_category_id_by_nav(item), 1);
    } else if (item == NAV_ALL) {
        start_remote_fetch(NULL, get_category_id_by_tab(g_store->active_cat), 1);
    } else if (item == NAV_RECOMMENDED) {
        start_remote_fetch(NULL, "__welcome__", 1);
    }
}

void store_ui_trigger_change_ranking_page(int page_idx)
{
    if (page_idx < 0) return;
    g_store->current_page = page_idx;
    SDL_SetAtomicInt(&g_store->dirty, 1);

    if (g_store->ranking_list) {
        linyaps_package_info_list_free(g_store->ranking_list, g_store->ranking_count);
        g_store->ranking_list  = NULL;
        g_store->ranking_count = 0;
    }
    start_ranking_fetch(g_store->ranking_tab, page_idx + 1);
}

/* Helper: get current remote category_id based on active navigation */
static const char *current_remote_category(void)
{
    if (g_store->active_nav == NAV_RECOMMENDED)      return "__welcome__";
    if (g_store->active_nav == NAV_ALL)              return get_category_id_by_tab(g_store->active_cat);
    if (g_store->active_nav >= NAV_CAT_OFFICE &&
        g_store->active_nav <= NAV_CAT_GAMES)        return get_category_id_by_nav(g_store->active_nav);
    return NULL;
}

void store_ui_trigger_remote_page(int page_idx)
{
    if (page_idx < 0) return;
    g_store->current_page = page_idx;
    start_remote_fetch(NULL, current_remote_category(), page_idx + 1);
}


static void on_search_results(LinyapsPackageInfo **items,
                               size_t count,
                               int error_code,
                               const char *message,
                               void *userdata)
{
    StoreState *s = userdata;
    if (error_code != 0)
        LOG_WARN("search", "搜索失败: code=%d msg=%s", error_code, message ? message : "");
    else
        LOG_INFO("search", "搜索结果: count=%zu", count);

    linyaps_package_info_list_free(s->search_results, s->search_count);

    if (error_code == 0 && items) {
        s->search_results = items;
        s->search_count   = count;
        s->current_page   = 0;
        s->is_searching   = true;
        strncpy(s->last_search_keyword, s->search_buf, sizeof(s->last_search_keyword) - 1);
    } else {
        s->search_results = NULL;
        s->search_count   = 0;
        s->is_searching   = false;
    }
    SDL_SetAtomicInt(&s->dirty, 1);
}


static void handle_text_input(StoreState *s, const char *text)
{
    size_t cur_len = strlen(s->search_buf);
    size_t add_len = strlen(text);
    if (cur_len + add_len < sizeof(s->search_buf) - 1) {
        strcat(s->search_buf, text);
        SDL_SetAtomicInt(&s->dirty, 1);
    }
}

static void handle_key(StoreState *s, SDL_Keycode key)
{
    if (!s->search_focused) return;

    if (key == SDLK_BACKSPACE) {
        size_t len = strlen(s->search_buf);
        if (len > 0) { s->search_buf[len - 1] = '\0'; SDL_SetAtomicInt(&s->dirty, 1); }
    } else if (key == SDLK_ESCAPE) {
        s->search_focused = false;
        SDL_SetAtomicInt(&s->dirty, 1);
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (s->search_buf[0]) {
            LOG_INFO("search", "开始搜索: keyword=\"%s\"", s->search_buf);
            s->current_page = 0;  /* 搜索时重置页码 */
            start_remote_fetch(s->search_buf, NULL, 1);
        }
        s->search_focused = false;
        SDL_SetAtomicInt(&s->dirty, 1);
    }
}


/* ---- Mouse/input state shared across event handlers ---- */
typedef struct {
    bool  mouse_down;
    bool  mouse_released;
    bool  mouse_moved;
    float mx;
    float my;
} InputState;

/* ------------------------------------------------------------------ */
/* Handle a background-thread user event; returns true if consumed     */
/* ------------------------------------------------------------------ */

static bool handle_user_event(StoreState *store, const SDL_Event *e)
{
    if (g_user_event_type == (Uint32)-1 || e->type != g_user_event_type)
        return false;

    switch (e->user.code) {

    /* ---- Remote app list arrived ---- */
    case EVT_REMOTE_READY: {
        RemoteResult *res = e->user.data1;
        char *data_str = e->user.data2;  /* keyword (if search) or category_id (if browse) */
        if (res) {
            bool matches = false;
            if (res->is_search) {
                /* Search results: match if keyword still matches current search_buf */
                matches = (data_str && strcmp(data_str, store->search_buf) == 0);
                LOG_INFO("remote", "搜索结果到达: keyword=\"%s\" current_buf=\"%s\" page=%d current_page=%d matches=%d",
                         data_str ? data_str : "", store->search_buf, res->page, store->current_page, matches);
            } else {
                /* Category browse: match if category still matches current nav */
                const char *current_cat = current_remote_category();
                matches = (!data_str && !current_cat) ||
                          (data_str && current_cat && strcmp(data_str, current_cat) == 0);
            }
            /* Also check that the page matches what we expect */
            if (matches && store->current_page != res->page - 1) {
                LOG_INFO("remote", "页码不匹配: expected=%d got=%d", store->current_page, res->page - 1);
                matches = false;
            }

            if (matches) {
                if (store->search_results != store->installed_list)
                    linyaps_package_info_list_free(store->search_results, store->search_count);
                store->search_results = res->list;
                store->search_count   = res->count;
                store->remote_total   = res->total;
                if (res->is_search) {
                    store->is_searching = true;
                    strncpy(store->last_search_keyword, data_str, sizeof(store->last_search_keyword) - 1);
                }
                SDL_SetAtomicInt(&store->loading_remote, 0);
                SDL_SetAtomicInt(&store->dirty, 1);
                LOG_INFO("remote", "更新 search_results: count=%zu total=%ld ptr=%p", res->count, res->total, (void*)res->list);
                for (size_t i = 0; i < res->count && i < 5; i++) {
                    LOG_INFO("remote", "  [%zu] id=%s name=%s", i, res->list[i]->id, res->list[i]->name);
                }
            } else {
                linyaps_package_info_list_free(res->list, res->count);
            }
            free(res);
        }
        free(data_str);
        return true;
    }

    /* ---- Ranking data arrived ---- */
    case EVT_RANKING_READY: {
        RemoteResult *res = e->user.data1;
        uintptr_t val = (uintptr_t)e->user.data2;
        int tab_type = val & 0xFF;
        int page = (val >> 8) & 0xFFFF;
        if (res) {
            if (store->ranking_tab == tab_type && store->current_page == (page - 1)) {
                if (store->ranking_list)
                    linyaps_package_info_list_free(store->ranking_list, store->ranking_count);
                store->ranking_list  = res->list;
                store->ranking_count = res->count;
                store->ranking_total = res->total;
                SDL_SetAtomicInt(&store->loading_ranking, 0);
                SDL_SetAtomicInt(&store->dirty, 1);
            } else {
                linyaps_package_info_list_free(res->list, res->count);
            }
            free(res);
        }
        return true;
    }

    /* ---- Check updates completed ---- */
    case EVT_CHECK_UPDATES_READY: {
        SDL_SetAtomicInt(&store->checking_updates, 0);

        /* 释放旧的更新列表 */
        if (store->update_list) {
            for (size_t i = 0; i < store->update_count; i++) {
                free(store->update_list[i].id);
                free(store->update_list[i].name);
                free(store->update_list[i].current_version);
                free(store->update_list[i].new_version);
                free(store->update_list[i].channel);
                free(store->update_list[i].task_path);
            }
            free(store->update_list);
            store->update_list = NULL;
            store->update_count = 0;
        }

        /* 处理更新检查结果 */
        UpdateCheckResult *result = e->user.data1;
        if (result) {
            store->update_list = result->items;
            store->update_count = result->count;
            free(result);
            LOG_INFO("updates", "更新检查完成，发现 %zu 个可更新应用", store->update_count);
        } else {
            LOG_INFO("updates", "更新检查完成，未发现可更新应用");
        }

        SDL_SetAtomicInt(&store->dirty, 1);
        return true;
    }

    /* ---- Update progress / finished ---- */
    case EVT_UPDATE_ITEM_PROGRESS:
        SDL_SetAtomicInt(&store->dirty, 1);
        return true;

    case EVT_UPDATE_ITEM_FINISHED: {
        StoreUpdateItem *item = e->user.data1;
        if (item) {
            size_t match_idx = (size_t)-1;
            for (size_t i = 0; i < store->update_count; i++) {
                if (&store->update_list[i] == item) { match_idx = i; break; }
            }
            if (match_idx != (size_t)-1) {
                free(store->update_list[match_idx].id);
                free(store->update_list[match_idx].name);
                free(store->update_list[match_idx].current_version);
                free(store->update_list[match_idx].new_version);
                free(store->update_list[match_idx].channel);
                free(store->update_list[match_idx].task_path);
                for (size_t i = match_idx; i < store->update_count - 1; i++)
                    store->update_list[i] = store->update_list[i + 1];
                store->update_count--;
                if (store->update_count == 0) {
                    free(store->update_list);
                    store->update_list = NULL;
                }
            }
        }
        SDL_SetAtomicInt(&store->dirty, 1);
        return true;
    }

    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Handle SDL input events; returns false when quit is requested        */
/* ------------------------------------------------------------------ */

static bool handle_sdl_event(StoreState *store, KilnUI *ui,
                              SDL_Event *e, InputState *input)
{
    if (e->type == SDL_EVENT_QUIT) return false;
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_ESCAPE
        && !store->search_focused) return false;

    /* 只在真正需要重布局的事件上设置 dirty，避免鼠标移动每帧触发全量重布局 */
    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION:
        /* 鼠标移动：只更新位置，不立即设置 dirty
         * 在主循环中检测位置变化后才设置 dirty */
        input->mx = e->motion.x;
        input->my = e->motion.y;
        input->mouse_moved = true;
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        input->mouse_down = true;
        input->mx = e->button.x;
        input->my = e->button.y;
        SDL_SetAtomicInt(&store->dirty, 1);
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        input->mouse_down    = false;
        input->mouse_released = true;
        input->mx = e->button.x;
        input->my = e->button.y;
        SDL_SetAtomicInt(&store->dirty, 1);
        break;

    case SDL_EVENT_TEXT_INPUT:
        if (store->search_focused) {
            handle_text_input(store, e->text.text);
            SDL_SetAtomicInt(&store->dirty, 1);
        }
        break;

    case SDL_EVENT_KEY_DOWN:
        handle_key(store, e->key.key);
        SDL_SetAtomicInt(&store->dirty, 1);
        break;

    default:
        break;
    }

    KilnUI_handle_event(ui, e);
    return true;
}


int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* ---- KilnUI init ---- */
    KilnUI ctx;
    static const char *fonts[] = {
        "kilnui/assets/Inter-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        NULL
    };
    const char *font = KilnUI_find_font(fonts);
    if (!font) { SDL_Log("No font found"); return 1; }
    if (!KilnUI_init(&ctx, "玲珑应用商店", 2556, 1478, font, 14)) return 1;

    /* Tell UI_Input which window to use for SDL text input */
    UI_SetTextInputWindow(ctx.window);

    /* ---- Register custom SDL event type ---- */
    g_user_event_type = SDL_RegisterEvents(1);
    if (g_user_event_type == (Uint32)-1)
        LOG_WARN("main", "SDL_RegisterEvents failed");

    /* ---- Backend init ---- */
    LinyapsContext *lctx = linyaps_context_new();
    if (lctx)
        LOG_INFO("main", "D-Bus 后端初始化成功");
    else
        LOG_WARN("main", "D-Bus 后端初始化失败，UI 将在离线模式运行");

    /* ---- App state ---- */
    StoreState store;
    store_state_init(&store, lctx);
    g_store = &store;
    DS_SetTheme(&DS_THEME_LIGHT);
    store.dark_mode = false;

    if (lctx) {
        store.installed_list = linyaps_list_installed(lctx, &store.installed_count);
        LOG_INFO("main", "已安装应用: count=%zu", store.installed_count);
        store.search_results = store.installed_list;
        store.search_count   = store.installed_count;

        /* 构建已安装应用 ID 哈希表，加速后续查找 */
        id_set_build_from_packages(&store.installed_id_set,
                                   store.installed_list, store.installed_count);
    }

    LOG_INFO("main", "启动远端应用列表拉取...");
    start_remote_fetch(NULL, "__welcome__", 1);

    /* 启动时自动检查更新 */
    store_ui_trigger_check_updates();

    /* ---- Event loop ---- */
    InputState input = { false, false, false, 0, 0 };
    float prev_mx = 0, prev_my = 0;  /* 上一帧鼠标位置 */
    bool running = true;

    while (running) {
        SDL_Event e;
        input.mouse_released = false;
        input.mouse_moved = false;

        /* Wait for event when idle (zero CPU usage) */
        if (!SDL_GetAtomicInt(&store.dirty))
            SDL_WaitEvent(NULL);

        while (SDL_PollEvent(&e)) {
            /* User events (from background threads) */
            if (handle_user_event(&store, &e))
                continue;
            /* SDL input events */
            if (!handle_sdl_event(&store, &ctx, &e, &input))
                { running = false; break; }
        }
        if (!running) break;

        /* 鼠标移动检测：只在位置实际变化时设置 dirty
         * Clay 需要每帧更新 hover 状态，但只有位置变化时才需要重布局 */
        if (input.mouse_moved && (input.mx != prev_mx || input.my != prev_my)) {
            SDL_SetAtomicInt(&store.dirty, 1);
            prev_mx = input.mx;
            prev_my = input.my;
        }

        /* Process D-Bus messages */
        if (lctx) {
            int r = linyaps_process(lctx);
            if (r > 0) SDL_SetAtomicInt(&store.dirty, 1);
        }

        /* Window resize detection */
        int pixel_w = 0, pixel_h = 0;
        SDL_GetWindowSizeInPixels(ctx.window, &pixel_w, &pixel_h);
        int window_w = (int)((float)pixel_w / ctx.dpi_scale);
        int window_h = (int)((float)pixel_h / ctx.dpi_scale);
        if (window_w != store.window_w || window_h != store.window_h) {
            store.window_w = window_w;
            store.window_h = window_h;
            SDL_SetAtomicInt(&store.dirty, 1);
        }

        /* Render */
        if (SDL_GetAtomicInt(&store.dirty)) {
            UI_SetPointerState(input.mouse_down, input.mouse_released, input.mx, input.my);
            store_ui_handle_pre_layout_actions(&store, input.mouse_released);

            Clay_BeginLayout();
            store_ui_build(&store);
            Clay_RenderCommandArray cmds = Clay_EndLayout(0.016f);
            KilnUI_render(&ctx, cmds);
            SDL_SetAtomicInt(&store.dirty, 0);
        }
    }

    /* ---- Cleanup ---- */
    if (store.search_results == store.installed_list) {
        store.search_results = NULL;
        store.search_count   = 0;
    }
    store_state_free(&store);
    if (lctx) linyaps_context_free(lctx);
    KilnUI_destroy(&ctx);
    return 0;
}
