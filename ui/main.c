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
#include <string.h>

/* ------------------------------------------------------------------ */
/* Remote fetch — runs in a background thread                          */
/* ------------------------------------------------------------------ */

/* Custom SDL user event types */
#define EVT_REMOTE_READY          0u   /* remote app list arrived */
#define EVT_CHECK_UPDATES_READY   1u   /* check updates finished */
#define EVT_UPDATE_ITEM_PROGRESS  2u   /* update progress update */
#define EVT_UPDATE_ITEM_FINISHED  3u   /* update finished */
#define EVT_RANKING_READY         4u   /* ranking list arrived */

typedef struct {
    LinyapsPackageInfo **list;
    size_t               count;
    long                 total;
} RemoteResult;

static StoreState *g_store = NULL;
static Uint32      g_user_event_type = 0;

/* ------------------------------------------------------------------ */
/* Updates triggers & simulation                                       */
/* ------------------------------------------------------------------ */

static void *check_updates_thread(void *arg)
{
    (void)arg;
    SDL_Delay(1500); /* simulate 1.5 seconds check time */

    SDL_Event ev;
    SDL_zero(ev);
    ev.type = g_user_event_type;
    ev.user.code = EVT_CHECK_UPDATES_READY;
    SDL_PushEvent(&ev);

    return NULL;
}

void store_ui_trigger_check_updates(void)
{
    if (!g_store || g_store->checking_updates) return;
    g_store->checking_updates = true;
    g_store->dirty = true;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, check_updates_thread, NULL);
    pthread_attr_destroy(&attr);
}

typedef struct {
    StoreState *store;
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

        item->progress = p;

        SDL_Event ev;
        SDL_zero(ev);
        ev.type = g_user_event_type;
        ev.user.code = EVT_UPDATE_ITEM_PROGRESS;
        ev.user.data1 = item;
        SDL_PushEvent(&ev);
    }

    SDL_Event ev;
    SDL_zero(ev);
    ev.type = g_user_event_type;
    ev.user.code = EVT_UPDATE_ITEM_FINISHED;
    ev.user.data1 = item;
    SDL_PushEvent(&ev);

    free(task);
    return NULL;
}

static void on_update_progress(const LinyapsTaskProgress *prog, void *userdata)
{
    StoreUpdateItem *item = userdata;
    if (!item || !g_store) return;

    item->progress = (float)prog->percentage;

    if (prog->state == LINYAPS_TASK_STATE_SUCCEED) {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = g_user_event_type;
        ev.user.code = EVT_UPDATE_ITEM_FINISHED;
        ev.user.data1 = item;
        SDL_PushEvent(&ev);
    } else if (prog->state == LINYAPS_TASK_STATE_FAILED || prog->state == LINYAPS_TASK_STATE_CANCELED) {
        item->updating = false;
        item->progress = 0.0f;
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = g_user_event_type;
        ev.user.code = EVT_UPDATE_ITEM_FINISHED;
        ev.user.data1 = item;
        SDL_PushEvent(&ev);
    } else {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = g_user_event_type;
        ev.user.code = EVT_UPDATE_ITEM_PROGRESS;
        ev.user.data1 = item;
        SDL_PushEvent(&ev);
    }
}

void store_ui_trigger_update_item(StoreUpdateItem *item)
{
    if (!g_store || !item || item->updating) return;

    item->updating = true;
    item->progress = 0.0f;
    g_store->dirty = true;

    bool use_simulation = true;

    if (g_store->ctx) {
        /* Check if the package is installed locally first */
        bool installed = false;
        for (size_t i = 0; i < g_store->installed_count; i++) {
            if (g_store->installed_list[i] && g_store->installed_list[i]->id &&
                strcmp(g_store->installed_list[i]->id, item->id) == 0) {
                installed = true;
                break;
            }
        }
        if (installed) {
            linyaps_update(g_store->ctx, item->id, on_update_progress, item);
            use_simulation = false;
        }
    }

    if (use_simulation) {
        SimulatedUpdateTask *task = malloc(sizeof(*task));
        if (task) {
            task->store = g_store;
            task->item = item;

            pthread_t tid;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&tid, &attr, simulate_update_thread, task);
            pthread_attr_destroy(&attr);
        } else {
            item->updating = false;
        }
    }
}

void store_ui_trigger_update_all(void)
{
    if (!g_store) return;
    for (size_t i = 0; i < g_store->update_count; i++) {
        if (!g_store->update_list[i].updating) {
            store_ui_trigger_update_item(&g_store->update_list[i]);
        }
    }
}

static void *fetch_remote_thread(void *arg)
{
    (void)arg;
    size_t count = 0;
    long   total = 0;

    /* Fetch first page (up to 30 apps) — extend later for pagination */
    LinyapsRemoteAppInfo **remote = linyaps_remote_fetch_apps(
        "", NULL, 1, LINYAPS_REMOTE_PAGE_SIZE, &count, &total);

    /* Convert to LinyapsPackageInfo** so we can reuse existing display code */
    LinyapsPackageInfo **list = NULL;
    if (remote && count > 0) {
        list = calloc(count, sizeof(*list));
        if (list) {
            for (size_t i = 0; i < count; i++)
                list[i] = linyaps_remote_app_info_to_package_info(remote[i]);
        }
        linyaps_remote_app_info_list_free(remote, count);
    }

    LOG_INFO("remote", "远端拉取完成: count=%zu total=%ld", count, total);

    /* Post result back to the main thread via SDL user event */
    RemoteResult *res = malloc(sizeof(*res));
    if (res) {
        res->list  = list;
        res->count = count;
        SDL_Event ev;
        SDL_zero(ev);
        ev.type       = g_user_event_type;
        ev.user.code  = EVT_REMOTE_READY;
        ev.user.data1 = res;
        SDL_PushEvent(&ev);
    } else {
        /* fallback: free on thread if malloc failed */
        linyaps_package_info_list_free(list, count);
    }
    return NULL;
}

static void start_remote_fetch(void)
{
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, fetch_remote_thread, NULL) != 0)
        LOG_WARN("remote", "无法创建后台拉取线程");
    pthread_attr_destroy(&attr);
}

static void *fetch_ranking_thread(void *arg)
{
    uintptr_t val = (uintptr_t)arg;
    int tab_type = val & 0xFF;
    int page = (val >> 8) & 0xFFFF;
    if (page == 0) page = 1;

    size_t count = 0;
    long   total = 0;

    LinyapsRankingType rtype = (tab_type == 0) ? LINYAPS_RANKING_NEWEST : LINYAPS_RANKING_DOWNLOADS;

    LinyapsRemoteAppInfo **remote = linyaps_remote_fetch_ranking(
        rtype, page, 30, &count, &total);

    /* Convert to LinyapsPackageInfo** */
    LinyapsPackageInfo **list = NULL;
    if (remote && count > 0) {
        list = calloc(count, sizeof(*list));
        if (list) {
            for (size_t i = 0; i < count; i++)
                list[i] = linyaps_remote_app_info_to_package_info(remote[i]);
        }
        linyaps_remote_app_info_list_free(remote, count);
    }

    LOG_INFO("ranking", "排行远端拉取完成: type=%d page=%d count=%zu total=%ld", tab_type, page, count, total);

    /* Post result back to the main thread via SDL user event */
    RemoteResult *res = malloc(sizeof(*res));
    if (res) {
        res->list  = list;
        res->count = count;
        res->total = total;
        SDL_Event ev;
        SDL_zero(ev);
        ev.type       = g_user_event_type;
        ev.user.code  = EVT_RANKING_READY;
        ev.user.data1 = res;
        ev.user.data2 = (void*)val;
        SDL_PushEvent(&ev);
    } else {
        linyaps_package_info_list_free(list, count);
    }
    return NULL;
}

static void start_ranking_fetch(int tab_type, int page)
{
    g_store->loading_ranking = true;
    g_store->dirty = true;

    uintptr_t val = (tab_type & 0xFF) | ((page & 0xFFFF) << 8);

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, fetch_ranking_thread, (void*)val) != 0) {
        LOG_WARN("ranking", "无法创建后台排行拉取线程");
        g_store->loading_ranking = false;
    }
    pthread_attr_destroy(&attr);
}

void store_ui_trigger_change_nav(NavItem item)
{
    if (g_store->active_nav == item) return;
    g_store->active_nav = item;
    g_store->current_page = 0;
    g_store->dirty = true;

    if (item == NAV_RANKING) {
        start_ranking_fetch(g_store->ranking_tab, g_store->current_page + 1);
    }
}

void store_ui_trigger_change_ranking_tab(int tab_idx)
{
    if (g_store->ranking_tab == tab_idx) return;
    g_store->ranking_tab = tab_idx;
    g_store->current_page = 0;
    g_store->dirty = true;

    /* Free old ranking list */
    if (g_store->ranking_list) {
        linyaps_package_info_list_free(g_store->ranking_list, g_store->ranking_count);
        g_store->ranking_list = NULL;
        g_store->ranking_count = 0;
    }
    g_store->ranking_total = 0;

    start_ranking_fetch(tab_idx, g_store->current_page + 1);
}

void store_ui_trigger_change_ranking_page(int page_idx)
{
    if (page_idx < 0) return;
    g_store->current_page = page_idx;
    g_store->dirty = true;

    /* Free old ranking list */
    if (g_store->ranking_list) {
        linyaps_package_info_list_free(g_store->ranking_list, g_store->ranking_count);
        g_store->ranking_list = NULL;
        g_store->ranking_count = 0;
    }

    start_ranking_fetch(g_store->ranking_tab, page_idx + 1);
}

/* ------------------------------------------------------------------ */
/* Search callback                                                     */
/* ------------------------------------------------------------------ */

static void on_search_results(LinyapsPackageInfo **items,
                               size_t count,
                               int error_code,
                               const char *message,
                               void *userdata)
{
    StoreState *s = userdata;

    if (error_code != 0) {
        LOG_WARN("search", "搜索失败: code=%d msg=%s",
                 error_code, message ? message : "");
    } else {
        LOG_INFO("search", "搜索结果: count=%zu", count);
    }

    linyaps_package_info_list_free(s->search_results, s->search_count);

    if (error_code == 0 && items) {
        s->search_results = items;
        s->search_count   = count;
        s->current_page   = 0;
    } else {
        s->search_results = NULL;
        s->search_count   = 0;
    }
    s->dirty = true;
}

/* ------------------------------------------------------------------ */
/* Text input handling                                                 */
/* ------------------------------------------------------------------ */

static void handle_text_input(StoreState *s, const char *text)
{
    size_t cur_len = strlen(s->search_buf);
    size_t add_len = strlen(text);
    if (cur_len + add_len < sizeof(s->search_buf) - 1) {
        strcat(s->search_buf, text);
        s->dirty = true;
    }
}

static void handle_key(StoreState *s, SDL_Keycode key)
{
    if (!s->search_focused) return;

    if (key == SDLK_BACKSPACE) {
        size_t len = strlen(s->search_buf);
        if (len > 0) { s->search_buf[len - 1] = '\0'; s->dirty = true; }
    } else if (key == SDLK_ESCAPE) {
        s->search_focused = false;
        s->dirty = true;
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (s->search_buf[0] && s->ctx) {
            LOG_INFO("search", "开始搜索: keyword=\"%s\"", s->search_buf);
            linyaps_search(s->ctx, s->search_buf, NULL, on_search_results, s);
        }
        s->search_focused = false;
        s->dirty = true;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

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

    /* ---- Register custom SDL event type for background threads ---- */
    g_user_event_type = SDL_RegisterEvents(1);
    if (g_user_event_type == (Uint32)-1)
        LOG_WARN("main", "SDL_RegisterEvents failed");

    /* ---- Backend init ---- */
    LinyapsContext *lctx = linyaps_context_new();
    if (lctx) {
        LOG_INFO("main", "D-Bus 后端初始化成功");
    } else {
        LOG_WARN("main", "D-Bus 后端初始化失败，UI 将在离线模式运行");
    }

    /* ---- App state ---- */
    StoreState store;
    store_state_init(&store, lctx);
    g_store = &store;
    DS_SetTheme(&DS_THEME_LIGHT);   /* reference UI uses a light theme */
    store.dark_mode = false;

    /* Pre-populate with installed packages (shown while remote loads) */
    if (lctx) {
        store.installed_list = linyaps_list_installed(lctx, &store.installed_count);
        LOG_INFO("main", "已安装应用: count=%zu", store.installed_count);
        /* Show installed list as default content */
        store.search_results = store.installed_list;
        store.search_count   = store.installed_count;
    }

    /* Kick off background remote fetch to populate the full app list */
    LOG_INFO("main", "启动远端应用列表拉取...");
    start_remote_fetch();

    /* ---- Event loop ---- */
    bool running        = true;
    bool mouse_down     = false;
    bool mouse_released = false;
    float mx = 0, my = 0;

    while (running) {
        SDL_Event e;
        mouse_released = false;

        /* Wait for event when idle (zero CPU usage) */
        if (!store.dirty) {
            SDL_WaitEvent(NULL);
        }

        while (SDL_PollEvent(&e)) {
            store.dirty = true;

            if (e.type == SDL_EVENT_QUIT) { running = false; break; }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE
                && !store.search_focused) { running = false; break; }

            /* ---- Background remote fetch completed ---- */
            if (g_user_event_type != (Uint32)-1 &&
                e.type == g_user_event_type &&
                e.user.code == EVT_REMOTE_READY) {
                RemoteResult *res = e.user.data1;
                if (res) {
                    LOG_INFO("main", "远端列表到达，刷新显示 count=%zu", res->count);
                    /* Replace display list with remote full list */
                    if (store.search_results != store.installed_list)
                        linyaps_package_info_list_free(store.search_results, store.search_count);
                    store.search_results = res->list;
                    store.search_count   = res->count;
                    store.current_page   = 0;
                    store.dirty = true;
                    free(res);
                }
                continue;
            }

            /* ---- Ranking remote fetch completed ---- */
            if (g_user_event_type != (Uint32)-1 &&
                e.type == g_user_event_type &&
                e.user.code == EVT_RANKING_READY) {
                RemoteResult *res = e.user.data1;
                uintptr_t val = (uintptr_t)e.user.data2;
                int tab_type = val & 0xFF;
                int page = (val >> 8) & 0xFFFF;
                if (res) {
                    LOG_INFO("main", "排行数据到达, type=%d page=%d count=%zu total=%ld", tab_type, page, res->count, res->total);
                    /* Only apply if the current active ranking tab and page match */
                    if (store.ranking_tab == tab_type && store.current_page == (page - 1)) {
                        if (store.ranking_list) {
                            linyaps_package_info_list_free(store.ranking_list, store.ranking_count);
                        }
                        store.ranking_list = res->list;
                        store.ranking_count = res->count;
                        store.ranking_total = res->total;
                        store.loading_ranking = false;
                        store.dirty = true;
                    } else {
                        /* discard */
                        linyaps_package_info_list_free(res->list, res->count);
                    }
                    free(res);
                }
                continue;
            }

            /* ---- Check Updates completed ---- */
            if (g_user_event_type != (Uint32)-1 &&
                e.type == g_user_event_type &&
                e.user.code == EVT_CHECK_UPDATES_READY) {
                store.checking_updates = false;
                if (store.update_count == 0) {
                    /* Repopulate mock data so the user can test updates again */
                    store.update_count = 3;
                    store.update_list = calloc(store.update_count, sizeof(*store.update_list));
                    if (store.update_list) {
                        store.update_list[0] = (StoreUpdateItem){
                            .id = strdup("cn.wps.wps-office"),
                            .name = strdup("WPS Office For Linux 个人版"),
                            .current_version = strdup("12.1.2.25882"),
                            .new_version = strdup("12.1.2.26885"),
                            .channel = strdup("main"),
                        };
                        store.update_list[1] = (StoreUpdateItem){
                            .id = strdup("com.microsoft.edge"),
                            .name = strdup("Microsoft Edge"),
                            .current_version = strdup("148.0.3967.86"),
                            .new_version = strdup("149.0.4022.69"),
                            .channel = strdup("main"),
                        };
                        store.update_list[2] = (StoreUpdateItem){
                            .id = strdup("org.localsend.localsend"),
                            .name = strdup("LocalSend"),
                            .current_version = strdup("1.17.0.3"),
                            .new_version = strdup("1.17.0.5"),
                            .channel = strdup("main"),
                        };
                    }
                }
                store.dirty = true;
                continue;
            }

            /* ---- Update Item Progress ---- */
            if (g_user_event_type != (Uint32)-1 &&
                e.type == g_user_event_type &&
                e.user.code == EVT_UPDATE_ITEM_PROGRESS) {
                store.dirty = true;
                continue;
            }

            /* ---- Update Item Finished ---- */
            if (g_user_event_type != (Uint32)-1 &&
                e.type == g_user_event_type &&
                e.user.code == EVT_UPDATE_ITEM_FINISHED) {
                StoreUpdateItem *item = e.user.data1;
                if (item) {
                    /* Remove the completed update item from list */
                    size_t match_idx = (size_t)-1;
                    for (size_t i = 0; i < store.update_count; i++) {
                        if (&store.update_list[i] == item) {
                            match_idx = i;
                            break;
                        }
                    }
                    if (match_idx != (size_t)-1) {
                        /* Free the removed item string fields */
                        free(store.update_list[match_idx].id);
                        free(store.update_list[match_idx].name);
                        free(store.update_list[match_idx].current_version);
                        free(store.update_list[match_idx].new_version);
                        free(store.update_list[match_idx].channel);
                        free(store.update_list[match_idx].task_path);

                        /* Shift remaining items */
                        for (size_t i = match_idx; i < store.update_count - 1; i++) {
                            store.update_list[i] = store.update_list[i + 1];
                        }
                        store.update_count--;
                        if (store.update_count == 0) {
                            free(store.update_list);
                            store.update_list = NULL;
                        }
                    }
                }
                store.dirty = true;
                continue;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                mx = e.motion.x; my = e.motion.y;
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                mouse_down = true; mx = e.button.x; my = e.button.y;
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                mouse_down = false; mouse_released = true;
                mx = e.button.x; my = e.button.y;
                /* clicking outside search box defocuses */
                store.search_focused = false;
            } else if (e.type == SDL_EVENT_TEXT_INPUT && store.search_focused) {
                handle_text_input(&store, e.text.text);
            } else if (e.type == SDL_EVENT_KEY_DOWN) {
                handle_key(&store, e.key.key);
            }

            KilnUI_handle_event(&ctx, &e);
        }
        if (!running) break;

        /* Process D-Bus messages if backend is alive */
        if (lctx) {
            int r = linyaps_process(lctx);
            if (r > 0) store.dirty = true;
        }

        if (store.dirty) {
            UI_SetPointerState(mouse_down, mouse_released, mx, my);
            store_ui_handle_pre_layout_actions(&store, mouse_released);

            Clay_BeginLayout();
            store_ui_build(&store);
            float dt = 0.016f;
            Clay_RenderCommandArray cmds = Clay_EndLayout(dt);
            KilnUI_render(&ctx, cmds);
            store.dirty = false;
        }
    }

    /* ---- Cleanup ---- */
    /* Detach shared pointers before freeing */
    if (store.search_results == store.installed_list) {
        store.search_results = NULL;
        store.search_count   = 0;
    }
    store_state_free(&store);
    if (lctx) linyaps_context_free(lctx);
    KilnUI_destroy(&ctx);
    return 0;
}
