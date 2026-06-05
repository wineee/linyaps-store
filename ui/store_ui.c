/* ui/store_ui.c — linyaps-store UI layout (KilnUI / Clay)
 *
 * Layout mirrors the reference screenshot:
 *
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │  Titlebar: logo  [  搜索框  ]                     [─][□][✕]   │
 *  ├────────┬────────────────────────────────────────────────────────┤
 *  │        │  [全部][网络应用][社交通讯][编程开发]…  [∨]           │
 *  │ Sidebar│────────────────────────────────────────────────────────│
 *  │  nav   │  App cards  (3-column grid, scrollable)               │
 *  │        │  [icon][name][desc]  [安装/打开]                      │
 *  │        │                                                        │
 *  └────────┴────────────────────────────────────────────────────────┘
 *
 * IDs are allocated in non-overlapping ranges per widget group.
 */

#include "store_ui.h"
#include "store_state.h"

#include "../kilnui/src/kilnui.h"
#include "../kilnui/src/ui/ui.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * ID ranges
 * ================================================================ */
/* 1 000 000 – root shell                                            */
/* 1 001 000 – titlebar                                             */
/* 1 002 000 – sidebar (nav items: 1002000 + NavItem index)         */
/* 1 003 000 – category tab bar (1003000 + CategoryTab index)       */
/* 1 004 000 – app grid cards (1004000 + card_index * 10 + field)   */
/* 1 005 000 – search input                                         */
/* 1 006 000 – empty / status placeholders                          */

#define ID_ROOT           1000000
#define ID_TITLEBAR       1001000
#define ID_SIDEBAR        1002000
#define ID_CAT_BAR        1003000
#define ID_CARD_BASE      1004000
#define ID_SEARCH         1005000
#define ID_STATUS         1006000

#define SIDEBAR_W         160
#define CARD_H            72
#define ICON_PLACEHOLDER  48

/* ================================================================
 * Helpers
 * ================================================================ */

static StoreState *g_state = NULL;

/* Compact helper: fixed-size colored rectangle (icon placeholder) */
static void icon_placeholder(int uid, int size)
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

/* ================================================================
 * Sidebar
 * ================================================================ */

static void sidebar_nav_item(int uid, NavItem item, const char *label,
                              const char *icon_str)
{
    bool active = (g_state->active_nav == item);
    Clay_ElementId eid = Clay_GetElementIdWithIndex(CLAY_STRING("NavItem"), uid);
    bool hov = Clay_PointerOver(eid);

    if (hov && UI__mouse_released) {
        g_state->active_nav = item;
        g_state->dirty      = true;
    }

    Clay_Color bg = active ? ds_theme->surface1
                 : hov    ? ds_theme->surface0
                 :          (Clay_Color){ 0, 0, 0, 0 };
    Clay_Color fg = active ? ds_theme->accent : ds_theme->subtext;

    CLAY(eid, {
        .layout = {
            .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(36) },
            .padding        = { DS_SPACE_3, DS_SPACE_3, 0, 0 },
            .childGap       = DS_SPACE_2,
            .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
        },
        .backgroundColor = bg,
        .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
        .border = {
            .color = ds_theme->accent,
            .width = { .left = (uint16_t)(active ? 3 : 0) },
        },
    }) {
        if (icon_str && *icon_str) {
            CLAY_TEXT(UI__str(icon_str), { .textColor = fg, .fontSize = DS_FS_MD });
        }
        CLAY_TEXT(UI__str(label), { .textColor = fg, .fontSize = DS_FS_MD });
    }
}

static void sidebar(void)
{
    CLAY(CLAY_SIDI(CLAY_STRING("Sidebar"), ID_SIDEBAR), {
        .layout = {
            .sizing          = { CLAY_SIZING_FIXED(SIDEBAR_W), CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .padding         = { DS_SPACE_2, DS_SPACE_2, DS_SPACE_3, DS_SPACE_3 },
            .childGap        = 2,
        },
        .backgroundColor = ds_theme->mantle,
        .border = {
            .color = ds_theme->surface0,
            .width = { .right = 1 },
        },
    }) {
        /* Top group: 推荐 */
        sidebar_nav_item(0, NAV_RECOMMENDED, NAV_LABELS[NAV_RECOMMENDED], "\xe2\x99\xa1"); /* ♡ */
        sidebar_nav_item(1, NAV_ALL,         NAV_LABELS[NAV_ALL],         "\xe2\x98\xb0"); /* ☰ */
        sidebar_nav_item(2, NAV_RANKING,     NAV_LABELS[NAV_RANKING],     "\xf0\x9f\x93\x8a");
        sidebar_nav_item(3, NAV_UPDATES,     NAV_LABELS[NAV_UPDATES],     "\xe2\x86\xba"); /* ↺ */

        /* Divider */
        UI_Divider(ID_SIDEBAR + 10);

        /* Category group */
        TY_Text(ID_SIDEBAR + 11, "分类", TY_CAPTION);
        sidebar_nav_item(4, NAV_CAT_OFFICE, NAV_LABELS[NAV_CAT_OFFICE], "\xf0\x9f\x96\xa5");
        sidebar_nav_item(5, NAV_CAT_SYSTEM, NAV_LABELS[NAV_CAT_SYSTEM], "\xe2\x9a\x99");  /* ⚙ */
        sidebar_nav_item(6, NAV_CAT_DEV,    NAV_LABELS[NAV_CAT_DEV],    "\xe2\x8c\xa8");  /* ⌨ */
        sidebar_nav_item(7, NAV_CAT_GAMES,  NAV_LABELS[NAV_CAT_GAMES],  "\xf0\x9f\x8e\xae");

        /* Spacer pushes bottom controls down */
        UI_SPACER(ID_SIDEBAR + 20);

        /* Bottom bar: folder / download / settings */
        UI_Divider(ID_SIDEBAR + 21);
        UI_ROW(ID_SIDEBAR + 22, DS_SPACE_2) {
            UI_IconButton(ID_SIDEBAR + 23, ICON_HOME,     DS_FS_LG, UI_BTN_GHOST, false);
            UI_IconButton(ID_SIDEBAR + 24, ICON_ARROW_DOWN, DS_FS_LG, UI_BTN_GHOST, false);
            UI_SPACER(ID_SIDEBAR + 25);
            UI_IconButton(ID_SIDEBAR + 26, ICON_SETTINGS, DS_FS_LG, UI_BTN_GHOST, false);
        }
    }
}

/* ================================================================
 * Category tab bar
 * ================================================================ */

static void category_tab_bar(void)
{
    CLAY(CLAY_SIDI(CLAY_STRING("CatBar"), ID_CAT_BAR), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment  = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
            .padding         = { DS_SPACE_4, DS_SPACE_4, 0, 0 },
            .childGap        = DS_SPACE_1,
        },
        .backgroundColor = ds_theme->base,
        .border = {
            .color = ds_theme->surface0,
            .width = { .bottom = 1 },
        },
    }) {
        for (int i = 0; i < CAT_COUNT; i++) {
            bool active = (g_state->active_cat == (CategoryTab)i);
            Clay_ElementId eid = Clay_GetElementIdWithIndex(CLAY_STRING("CatTab"), i);
            bool hov = Clay_PointerOver(eid);

            if (hov && UI__mouse_released) {
                g_state->active_cat = (CategoryTab)i;
                g_state->dirty      = true;
            }

            Clay_Color bg = active ? ds_theme->surface1
                         : hov    ? ds_theme->surface0
                         :          (Clay_Color){ 0, 0, 0, 0 };
            Clay_Color fg = active ? ds_theme->accent : ds_theme->subtext;

            CLAY(eid, {
                .layout = {
                    .sizing         = { CLAY_SIZING_FIT(DS_SPACE_4), CLAY_SIZING_FIXED(32) },
                    .padding        = { DS_SPACE_3, DS_SPACE_3, 0, 0 },
                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = bg,
                .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
                .border = {
                    .color = ds_theme->accent,
                    .width = { .bottom = (uint16_t)(active ? 2 : 0) },
                },
            }) {
                CLAY_TEXT(UI__str(CAT_LABELS[i]),
                          { .textColor = fg, .fontSize = DS_FS_SM });
            }
        }
    }
}

/* ================================================================
 * Single app card
 * ================================================================ */

static void app_card(int card_idx, const LinyapsPackageInfo *info, bool installed)
{
    int base = ID_CARD_BASE + card_idx * 20;

    Clay_ElementId card_id = Clay_GetElementIdWithIndex(CLAY_STRING("AppCard"), card_idx);
    bool card_hov = Clay_PointerOver(card_id);

    Clay_Color card_bg = card_hov ? ds_theme->surface1 : ds_theme->surface0;

    CLAY(card_id, {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CARD_H) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .padding         = { DS_SPACE_3, DS_SPACE_3, DS_SPACE_3, DS_SPACE_3 },
            .childGap        = DS_SPACE_3,
            .childAlignment  = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
        },
        .backgroundColor = card_bg,
        .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_LG),
    }) {
        /* Icon placeholder */
        icon_placeholder(base + 1, ICON_PLACEHOLDER);

        /* Text block: name + description */
        CLAY(CLAY_SIDI(CLAY_STRING("CardText"), base + 2), {
            .layout = {
                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap        = 4,
            },
        }) {
            TY_Text(base + 3, info->name ? info->name : info->id, TY_H4);
            CLAY(CLAY_SIDI(CLAY_STRING("CardDesc"), base + 4), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                },
                .clip = { .horizontal = true },
            }) {
                const char *desc = info->description ? info->description : "";
                CLAY_TEXT(UI__str(desc),
                          { .textColor = ds_theme->subtext, .fontSize = DS_FS_SM });
            }
        }

        /* Action button */
        if (installed) {
            UI_Button(base + 5, "打开", UI_BTN_GHOST, UI_BTN_SM, false);
        } else {
            UI_Button(base + 5, "安装", UI_BTN_PRIMARY, UI_BTN_SM, false);
        }
    }
}

/* ================================================================
 * App grid (3-column)
 * ================================================================ */

static void app_grid(void)
{
    LinyapsPackageInfo **list = g_state->search_results;
    size_t count              = g_state->search_count;

    if (!list || count == 0) {
        /* Empty state */
        CLAY(CLAY_SIDI(CLAY_STRING("EmptyState"), ID_STATUS), {
            .layout = {
                .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap        = DS_SPACE_4,
            },
        }) {
            TY_Text(ID_STATUS + 1, "\xf0\x9f\x94\x8d", TY_DISPLAY); /* 🔍 */
            TY_TextColored(ID_STATUS + 2, "暂无应用", TY_H3, ds_theme->subtext);
            TY_TextColored(ID_STATUS + 3, "搜索或选择分类以查看应用", TY_BODY, ds_theme->muted);
        }
        return;
    }

    /* Outer scroll container */
    UI_SCROLLCOL(ID_STATUS + 10, DS_SPACE_3) {
        /* Rows of 3 cards each */
        size_t i = 0;
        int row_idx = 0;
        while (i < count) {
            UI_ROW(ID_STATUS + 20 + row_idx * 2, DS_SPACE_3) {
                for (int col = 0; col < 3 && i < count; col++, i++) {
                    bool inst = false;
                    /* Check installed list */
                    for (size_t k = 0; k < g_state->installed_count; k++) {
                        LinyapsPackageInfo *ins = g_state->installed_list[k];
                        if (ins && ins->id && list[i]->id &&
                            strcmp(ins->id, list[i]->id) == 0) {
                            inst = true;
                            break;
                        }
                    }
                    app_card((int)i, list[i], inst);
                }
                /* Fill empty columns in last row */
                for (int col = (int)(count - (i - (count % 3 == 0 ? 3 : count % 3)));
                     count % 3 != 0 && col < 3 && row_idx == (int)((count - 1) / 3); col++) {
                    /* invisible filler */
                    CLAY(CLAY_SIDI(CLAY_STRING("CardFill"), ID_STATUS + 50 + col), {
                        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CARD_H) } },
                    }) {}
                }
            }
            row_idx++;
        }
    }
}

/* ================================================================
 * Titlebar (search + logo)
 * ================================================================ */

static void titlebar(void)
{
    CLAY(CLAY_SIDI(CLAY_STRING("Titlebar"), ID_TITLEBAR), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(52) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment  = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
            .padding         = { DS_SPACE_4, DS_SPACE_4, 0, 0 },
            .childGap        = DS_SPACE_4,
        },
        .backgroundColor = ds_theme->crust,
        .border = {
            .color = ds_theme->surface0,
            .width = { .bottom = 1 },
        },
    }) {
        /* Logo / app name */
        CLAY(CLAY_SIDI(CLAY_STRING("TitleLogo"), ID_TITLEBAR + 1), {
            .layout = {
                .sizing         = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) },
                .childGap       = DS_SPACE_2,
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
            },
        }) {
            /* Icon placeholder circle */
            CLAY(CLAY_SIDI(CLAY_STRING("AppIcon"), ID_TITLEBAR + 2), {
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) },
                },
                .backgroundColor = ds_theme->accent,
                .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_FULL),
            }) {}
            TY_Text(ID_TITLEBAR + 3, "玲珑应用商店", TY_H4);
        }

        /* Search bar (grows to fill) */
        CLAY(CLAY_SIDI(CLAY_STRING("SearchWrap"), ID_SEARCH), {
            .layout = {
                .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(DS_HEIGHT_SM) },
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
            },
        }) {
            UIInputResult ir = UI_Input(ID_SEARCH + 1,
                                        NULL,
                                        g_state->search_buf,
                                        "在这里搜索您想搜索的应用",
                                        g_state->search_focused,
                                        false);
            if (ir.clicked) {
                g_state->search_focused = true;
                g_state->dirty          = true;
            }
        }

        UI_SPACER(ID_TITLEBAR + 10);

        /* Theme toggle */
        if (UI_IconButton(ID_TITLEBAR + 11,
                          g_state->dark_mode ? "\xe2\x98\x80" /* ☀ */ : "\xe2\x98\xbd" /* ☽ */,
                          DS_FS_LG, UI_BTN_GHOST, false)) {
            g_state->dark_mode = !g_state->dark_mode;
            DS_SetTheme(g_state->dark_mode ? &DS_THEME_DARK : &DS_THEME_LIGHT);
            g_state->dirty = true;
        }
    }
}

/* ================================================================
 * Main content area
 * ================================================================ */

static void content_area(void)
{
    CLAY(CLAY_SIDI(CLAY_STRING("ContentArea"), ID_ROOT + 2), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
    }) {
        category_tab_bar();

        /* Scrollable card grid with padding */
        CLAY(CLAY_SIDI(CLAY_STRING("GridWrap"), ID_ROOT + 3), {
            .layout = {
                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .padding         = { DS_SPACE_4, DS_SPACE_4, DS_SPACE_4, DS_SPACE_4 },
                .childGap        = DS_SPACE_3,
            },
            .backgroundColor = ds_theme->base,
        }) {
            app_grid();
        }
    }
}

/* ================================================================
 * Top-level layout entry point
 * ================================================================ */

void store_ui_build(StoreState *state)
{
    g_state = state;

    CLAY(CLAY_ID("StoreRoot"), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .backgroundColor = ds_theme->base,
    }) {
        /* Titlebar spans full width */
        titlebar();

        /* Below titlebar: sidebar + content side by side */
        CLAY(CLAY_SIDI(CLAY_STRING("Body"), ID_ROOT + 1), {
            .layout = {
                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
        }) {
            sidebar();
            content_area();
        }
    }
}
