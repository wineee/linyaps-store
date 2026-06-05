#include "store_state.h"
#include "store_ui.h"

#include "../kilnui/src/kilnui.h"
#include "../kilnui/src/ui/ui.h"
#include "../lib/linyaps_backend.h"
#include "../lib/linyaps_log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Search callback                                                     */
/* ------------------------------------------------------------------ */

static StoreState *g_store = NULL;

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
    if (!KilnUI_init(&ctx, "玲珑应用商店", 1060, 660, font, 14)) return 1;

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

    /* Pre-populate with installed packages */
    if (lctx) {
        store.installed_list = linyaps_list_installed(lctx, &store.installed_count);
        LOG_INFO("main", "已安装应用: count=%zu", store.installed_count);
        /* Show installed list as default content */
        store.search_results = store.installed_list;
        store.search_count   = store.installed_count;
    }

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
