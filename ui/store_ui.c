/* ui/store_ui.c — linyaps-store UI layout (KilnUI / Clay)
 *
 * Top-level orchestrator: frame-string arena, common helpers,
 * and the store_ui_build() entry point that composes the views.
 */

#include "store_ui.h"
#include "store_state.h"
#include "views/views_internal.h"

#include <spawn.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern char **environ;

/* ================================================================ */
/* Frame-string arena                                                */
/* ================================================================ */

static char   g_string_arena[16384];
static size_t g_string_arena_offset = 0;

const char *store_ui_frame_str(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char *dest = &g_string_arena[g_string_arena_offset];
    int remaining = (int)(sizeof(g_string_arena) - g_string_arena_offset);
    if (remaining <= 0) { va_end(args); return ""; }
    int n = vsnprintf(dest, remaining, fmt, args);
    va_end(args);
    if (n < 0) return "";
    g_string_arena_offset += n + 1;
    return dest;
}

void store_ui_reset_frame_str_arena(void)
{
    g_string_arena_offset = 0;
}

/* ================================================================ */
/* Global state                                                      */
/* ================================================================ */

StoreState *g_state = NULL;

/* ================================================================ */
/* App launch helper                                                 */
/* ================================================================ */

void launch_app(const char *app_id)
{
    if (!app_id || !*app_id) {
        LOG_WARN("launch", "app_id is empty, skipping launch");
        return;
    }
    for (const char *p = app_id; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '.' || *p == '-' || *p == '_' || *p == '/') continue;
        LOG_WARN("launch", "app_id '%s' contains unsafe char '%c', aborting", app_id, *p);
        return;
    }

    char *const argv[] = { "ll-cli", "run", (char *)app_id, NULL };
    pid_t pid;
    int r = posix_spawnp(&pid, "ll-cli", NULL, NULL, argv, environ);
    if (r != 0) {
        LOG_ERR("launch", "posix_spawnp failed for '%s': %s", app_id, strerror(r));
        return;
    }
    LOG_INFO("launch", "ll-cli run %s (pid=%d)", app_id, (int)pid);
    waitpid(pid, NULL, WNOHANG);
}

/* ================================================================ */
/* Grid layout helpers                                               */
/* ================================================================ */

static int iabs_int(int v)  { return v < 0 ? -v : v; }
static int clamp_int(int v, int min, int max)
{
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

AppGridSpec app_grid_spec(int reserve_h)
{
    const int gap = DS_SPACE_3;
    int content_w = g_state->window_w - SIDEBAR_W - DS_SPACE_4 * 2;
    int content_h = g_state->window_h - TITLEBAR_H - reserve_h - DS_SPACE_4 * 2;
    if (content_w < 1)       content_w = 1;
    if (content_h < CARD_H)  content_h = CARD_H;

    int max_fit_cols = (content_w + gap) / (GRID_MIN_CARD_W + gap);
    max_fit_cols = clamp_int(max_fit_cols, 1, GRID_MAX_COLS);

    int best_cols = 1, best_w = content_w, best_score = 1000000;
    for (int cols = 1; cols <= max_fit_cols; cols++) {
        int w = (content_w - gap * (cols - 1)) / cols;
        int score = iabs_int(w - GRID_TARGET_CARD_W);
        if (w > GRID_MAX_CARD_W) score += (w - GRID_MAX_CARD_W) * 2;
        if (score < best_score) { best_score = score; best_cols = cols; best_w = w; }
    }

    int rows_h = content_h - GRID_PAGE_H;
    int rows = clamp_int((rows_h + gap) / (CARD_H + gap), 1, 12);

    return (AppGridSpec){
        .cols = best_cols,
        .rows = rows,
        .card_w = best_w,
        .items_per_page = best_cols * rows,
    };
}

/* ================================================================ */
/* Icon placeholders                                                 */
/* ================================================================ */

void icon_placeholder(int uid, int size)
{
    Clay_Color col = { 60, 60, 80, 255 };
    CLAY(CLAY_SIDI(CLAY_STRING("IconPH"), uid), {
        .layout = {
            .sizing         = { CLAY_SIZING_FIXED((float)size),
                                CLAY_SIZING_FIXED((float)size) },
            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
        },
        .backgroundColor = col,
        .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
    }) {}
}

void letter_icon_placeholder(int uid, int size, const char *letter)
{
    Clay_Color col = { 60, 60, 80, 255 };
    CLAY(CLAY_SIDI(CLAY_STRING("LetterIconPH"), uid), {
        .layout = {
            .sizing         = { CLAY_SIZING_FIXED((float)size),
                                CLAY_SIZING_FIXED((float)size) },
            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
        },
        .backgroundColor = col,
        .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
    }) {
        CLAY_TEXT(UI__str(letter), { .textColor = ds_theme->text, .fontSize = 18 });
    }
}

/* ================================================================ */
/* Content area — dispatches to the active view                      */
/* ================================================================ */

static void content_area(void)
{
    CLAY(CLAY_SIDI(CLAY_STRING("ContentArea"), ID_ROOT + 2), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
    }) {
        if (g_state->active_nav == NAV_UPDATES) {
            view_updates();
        } else if (g_state->active_nav == NAV_RANKING) {
            CLAY(CLAY_SIDI(CLAY_STRING("RankingWrap"), ID_ROOT + 4), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .padding         = { DS_SPACE_4, DS_SPACE_4, DS_SPACE_4, DS_SPACE_4 },
                    .childGap        = DS_SPACE_3,
                },
                .backgroundColor = ds_theme->base,
            }) {
                view_ranking();
            }
        } else {
            /* Category header for sidebar categories */
            bool is_sidebar_cat = (g_state->active_nav >= NAV_CAT_OFFICE &&
                                   g_state->active_nav <= NAV_CAT_GAMES);
            if (is_sidebar_cat) {
                CLAY(CLAY_SIDI(CLAY_STRING("CatHeader"), ID_STATUS + 400), {
                    .layout = {
                        .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT,
                        .childAlignment  = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                        .padding         = { DS_SPACE_4, DS_SPACE_4, 0, 0 },
                    },
                    .backgroundColor = ds_theme->base,
                    .border = { .color = ds_theme->surface0, .width = { .bottom = 1 } },
                }) {
                    const char *hdr = store_ui_frame_str("%s (%zu)",
                        NAV_LABELS[g_state->active_nav], g_state->search_count);
                    CLAY_TEXT(UI__str(hdr), { .textColor = ds_theme->text, .fontSize = DS_FS_LG });
                }
            } else if (g_state->active_nav == NAV_RECOMMENDED) {
                CLAY(CLAY_SIDI(CLAY_STRING("RecommendHeader"), ID_STATUS + 410), {
                    .layout = {
                        .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT,
                        .childAlignment  = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                        .padding         = { DS_SPACE_4, DS_SPACE_4, 0, 0 },
                    },
                    .backgroundColor = ds_theme->base,
                    .border = { .color = ds_theme->surface0, .width = { .bottom = 1 } },
                }) {
                    CLAY_TEXT(CLAY_STRING("推荐"), { .textColor = ds_theme->text, .fontSize = DS_FS_LG });
                }
            } else if (g_state->active_nav == NAV_ALL) {
                view_category_tab_bar();
            }

            CLAY(CLAY_SIDI(CLAY_STRING("GridWrap"), ID_ROOT + 3), {
                .layout = {
                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .padding         = { DS_SPACE_4, DS_SPACE_4, DS_SPACE_4, DS_SPACE_4 },
                    .childGap        = DS_SPACE_3,
                },
                .backgroundColor = ds_theme->base,
            }) {
                view_app_grid();
            }
        }
    }
}

/* ================================================================ */
/* Public entry points                                               */
/* ================================================================ */

void store_ui_build(StoreState *state)
{
    store_ui_reset_frame_str_arena();
    g_state = state;

    CLAY(CLAY_ID("StoreRoot"), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .backgroundColor = ds_theme->base,
    }) {
        view_titlebar();

        CLAY(CLAY_SIDI(CLAY_STRING("Body"), ID_ROOT + 1), {
            .layout = {
                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
        }) {
            view_sidebar();
            content_area();
        }
    }
}

void store_ui_handle_pre_layout_actions(StoreState *state, bool mouse_released)
{
    if (!state || !mouse_released) return;

    Clay_ElementId theme_id = Clay_GetElementIdWithIndex(
        CLAY_STRING("UIIconBtn"), ID_THEME_TOGGLE);

    if (Clay_PointerOver(theme_id)) {
        state->dark_mode = !state->dark_mode;
        DS_SetTheme(state->dark_mode ? &DS_THEME_DARK : &DS_THEME_LIGHT);
        SDL_SetAtomicInt(&state->dirty, 1);
    }
}
