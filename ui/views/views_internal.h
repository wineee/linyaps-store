/* SPDX-License-Identifier: MIT */
/* ui/views/views_internal.h — Shared state & helpers for store views
 *
 * This header is internal to the UI layer — not part of the public API.
 * Each view .c file includes this to access g_state, ds_theme,
 * frame-string arena, ID ranges, and layout constants.
 */

#pragma once

#include "../store_state.h"
#include "../store_ui.h"

#include "../../kilnui/src/kilnui.h"
#include "../../kilnui/src/ui/ui.h"
#include "../../lib/linyaps_backend.h"
#include "../../lib/linyaps_log.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* ID ranges (non-overlapping per widget group)                        */
/* ------------------------------------------------------------------ */

#define ID_ROOT           1000000
#define ID_TITLEBAR       1001000
#define ID_SIDEBAR        1002000
#define ID_CAT_BAR        1003000
#define ID_CARD_BASE      1004000
#define ID_SEARCH         1005000
#define ID_STATUS         1006000
#define ID_THEME_TOGGLE   (ID_TITLEBAR + 11)

/* ------------------------------------------------------------------ */
/* Layout constants                                                    */
/* ------------------------------------------------------------------ */

#define SIDEBAR_W         180
#define CARD_H            88
#define ICON_PLACEHOLDER  48
#define GRID_MIN_CARD_W   320
#define GRID_TARGET_CARD_W 380
#define GRID_MAX_CARD_W   480
#define GRID_MAX_COLS     6
#define TITLEBAR_H        52
#define CONTENT_HEADER_H  44
#define GRID_PAGE_H       40

/* ------------------------------------------------------------------ */
/* Global state (defined in store_ui.c)                                */
/* ------------------------------------------------------------------ */

extern StoreState *g_state;

/* ------------------------------------------------------------------ */
/* Frame-string arena (defined in store_ui.c)                          */
/*                                                                     */
/* Provides temporary strings that live until the next frame.          */
/* Each call appends to a static buffer; the buffer is reset once      */
/* per frame by store_ui_reset_frame_str_arena().                      */
/* ------------------------------------------------------------------ */

const char *store_ui_frame_str(const char *fmt, ...);
void        store_ui_reset_frame_str_arena(void);

/* ------------------------------------------------------------------ */
/* Helpers (defined in store_ui.c)                                     */
/* ------------------------------------------------------------------ */

/* Launch an installed Linglong app asynchronously */
void launch_app(const char *app_id);

/* Adaptive grid layout calculation */
typedef struct {
    int cols;
    int rows;
    int card_w;
    int items_per_page;
} AppGridSpec;

AppGridSpec app_grid_spec(int reserve_h);

/* Icon placeholder rectangle */
void icon_placeholder(int uid, int size);

/* Letter-based icon placeholder (used by updates & ranking views) */
void letter_icon_placeholder(int uid, int size, const char *letter);

/* ------------------------------------------------------------------ */
/* View entry points (each defined in its own .c file)                 */
/* ------------------------------------------------------------------ */

void view_sidebar(void);
void view_titlebar(void);
void view_category_tab_bar(void);
void view_app_grid(void);
void view_updates(void);
void view_ranking(void);
