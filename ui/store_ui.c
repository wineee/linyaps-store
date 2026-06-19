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
#include "../lib/linyaps_backend.h"
#include "../lib/linyaps_log.h"

#include <spawn.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>

extern char **environ;

/* ================================================================
 * App launch helper
 * ================================================================ */

/* Launch an installed Linglong app via 'll-cli run <app_id>'.
 * Uses posix_spawn so the UI thread is never blocked.
 * The child is immediately waitpid(WNOHANG) to reap any instant exits;
 * long-running apps are left as orphans (ll-cli exits after handing
 * control to the container init). */
static void launch_app(const char *app_id)
{
    if (!app_id || !*app_id) {
        LOG_WARN("launch", "app_id is empty, skipping launch");
        return;
    }
    /* Basic shell-safety check (app ids are reverse-DNS, dots/dashes OK) */
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
    /* Non-blocking reap — ll-cli exits quickly after container launch */
    waitpid(pid, NULL, WNOHANG);
}

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

#define ID_THEME_TOGGLE   (ID_TITLEBAR + 11)

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
        g_state->current_page = 0;
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

        if (item == NAV_UPDATES && g_state->update_count > 0) {
            /* Push badge to the right side of the nav item */
            CLAY(CLAY_SIDI(CLAY_STRING("NavSpacer"), uid + 100), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }
            }) {}

            char count_str[16];
            snprintf(count_str, sizeof(count_str), "%zu", g_state->update_count);
            UI_Badge(uid + 200, count_str, DS_VARIANT_ERROR);
        }
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
                g_state->current_page = 0;
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

        /* Action button — capture return value to handle clicks */
        if (installed) {
            if (UI_Button(base + 5, "打开", UI_BTN_GHOST, UI_BTN_SM, false)) {
                const char *app_id = info->id ? info->id : NULL;
                LOG_INFO("ui", "打开按钮点击: id=%s", app_id ? app_id : "(unknown)");
                launch_app(app_id);
            }
        } else {
            if (UI_Button(base + 5, "安装", UI_BTN_PRIMARY, UI_BTN_SM, false)) {
                const char *app_id = info->id ? info->id : "(unknown)";
                LOG_INFO("ui", "安装按钮点击: id=%s ver=%s",
                         app_id,
                         info->version ? info->version : "-");
                if (g_state->ctx) {
                    linyaps_install(g_state->ctx,
                                    info->id,
                                    info->version,
                                    info->channel,
                                    NULL, NULL);
                } else {
                    LOG_WARN("ui", "后端未连接，无法安装 %s", app_id);
                }
                g_state->dirty = true;
            }
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

    int items_per_page = 12; // 3 columns * 4 rows
    int total_pages = (count + items_per_page - 1) / items_per_page;
    if (g_state->current_page >= total_pages) g_state->current_page = total_pages - 1;
    if (g_state->current_page < 0) g_state->current_page = 0;

    size_t start_idx = g_state->current_page * items_per_page;
    size_t end_idx = start_idx + items_per_page;
    if (end_idx > count) end_idx = count;

    /* Outer column for grid + pagination */
    UI_COL(ID_STATUS + 9, DS_SPACE_3) {
        /* App Grid */
        UI_COL(ID_STATUS + 10, DS_SPACE_3) {
            /* Rows of 3 cards each */
            size_t i = start_idx;
            int row_idx = 0;
            while (i < end_idx) {
                UI_ROW(ID_STATUS + 20 + row_idx * 2, DS_SPACE_3) {
                    for (int col = 0; col < 3 && i < end_idx; col++, i++) {
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
                    for (int col = (int)((end_idx - start_idx) - (i - start_idx - ((end_idx - start_idx) % 3 == 0 ? 3 : (end_idx - start_idx) % 3)));
                         (end_idx - start_idx) % 3 != 0 && col < 3 && row_idx == (int)((end_idx - start_idx - 1) / 3); col++) {
                        /* invisible filler */
                        CLAY(CLAY_SIDI(CLAY_STRING("CardFill"), ID_STATUS + 50 + col), {
                            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CARD_H) } },
                        }) {}
                    }
                }
                row_idx++;
            }
        }
        
        UI_SPACER(ID_STATUS + 80);
        
        /* Pagination Controls */
        if (total_pages > 1) {
            UI_ROW(ID_STATUS + 81, DS_SPACE_3) {
                UI_SPACER(ID_STATUS + 82);
                
                if (UI_Button(ID_STATUS + 83, "上一页", g_state->current_page > 0 ? UI_BTN_SECONDARY : UI_BTN_GHOST, UI_BTN_SM, false)) {
                    if (g_state->current_page > 0) {
                        g_state->current_page--;
                        g_state->dirty = true;
                    }
                }
                
                static char page_text[32];
                snprintf(page_text, sizeof(page_text), "第 %d / %d 页", g_state->current_page + 1, total_pages);
                CLAY(CLAY_SIDI(CLAY_STRING("PageText"), ID_STATUS + 84), {
                    .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) }, .padding = { 8, 8, 0, 0 } },
                }) {
                    CLAY_TEXT(UI__str(page_text), { .textColor = ds_theme->text, .fontSize = DS_FS_SM });
                }
                
                if (UI_Button(ID_STATUS + 85, "下一页", g_state->current_page < total_pages - 1 ? UI_BTN_SECONDARY : UI_BTN_GHOST, UI_BTN_SM, false)) {
                    if (g_state->current_page < total_pages - 1) {
                        g_state->current_page++;
                        g_state->dirty = true;
                    }
                }
                
                UI_SPACER(ID_STATUS + 86);
            }
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
        UI_IconButton(ID_THEME_TOGGLE,
                      g_state->dark_mode ? "\xe2\x98\x80" /* ☀ */ : "\xe2\x98\xbd" /* ☽ */,
                      DS_FS_LG, UI_BTN_GHOST, false);
    }
}

static void letter_icon_placeholder(int uid, int size, const char *letter)
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

static void updates_view(void)
{
    /* Scrollable card grid with padding */
    CLAY(CLAY_SIDI(CLAY_STRING("UpdatesWrap"), ID_ROOT + 3), {
        .layout = {
            .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .padding         = { DS_SPACE_4, DS_SPACE_4, DS_SPACE_4, DS_SPACE_4 },
            .childGap        = DS_SPACE_3,
        },
        .backgroundColor = ds_theme->base,
    }) {
        /* Outer column */
        UI_COL(ID_STATUS + 100, DS_SPACE_4) {
            /* Header row */
            UI_ROW(ID_STATUS + 101, DS_SPACE_3) {
                /* Left title text */
                char header_text[64];
                if (g_state->checking_updates) {
                    snprintf(header_text, sizeof(header_text), "正在检查可更新的应用...");
                } else if (g_state->update_count > 0) {
                    snprintf(header_text, sizeof(header_text), "共 %zu 个应用可更新", g_state->update_count);
                } else {
                    snprintf(header_text, sizeof(header_text), "您的应用已是最新版本");
                }
                TY_Text(ID_STATUS + 102, header_text, TY_H3);

                UI_SPACER(ID_STATUS + 103);

                /* Right buttons */
                if (g_state->checking_updates) {
                    /* Disabled loading buttons */
                    UI_Button(ID_STATUS + 104, "\xe2\x86\xba  正在检查...", UI_BTN_GHOST, UI_BTN_SM, true);
                    UI_Button(ID_STATUS + 105, "\xe2\x86\xba  全部更新", UI_BTN_PRIMARY, UI_BTN_SM, true);
                } else {
                    if (UI_Button(ID_STATUS + 104, "\xe2\x86\xba  检查更新", UI_BTN_SECONDARY, UI_BTN_SM, false)) {
                        store_ui_trigger_check_updates();
                    }

                    bool disable_update_all = (g_state->update_count == 0);
                    if (UI_Button(ID_STATUS + 105, "\xe2\x86\xba  全部更新", UI_BTN_PRIMARY, UI_BTN_SM, disable_update_all)) {
                        store_ui_trigger_update_all();
                    }
                }
            }

            /* Scrollable list of update cards */
            UI_SCROLLCOL(ID_STATUS + 110, DS_SPACE_3) {
                if (g_state->checking_updates) {
                    /* Show a nice loading placeholder */
                    CLAY(CLAY_SIDI(CLAY_STRING("CheckingPlaceholder"), ID_STATUS + 111), {
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(200) },
                            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM,
                            .childGap = DS_SPACE_3,
                        }
                    }) {
                        TY_Text(ID_STATUS + 112, ICON_SPINNER, TY_DISPLAY);
                        TY_TextColored(ID_STATUS + 113, "正在获取服务器应用列表，请稍候...", TY_BODY, ds_theme->muted);
                    }
                } else if (g_state->update_count == 0) {
                    /* Show a nice "all up to date" placeholder */
                    CLAY(CLAY_SIDI(CLAY_STRING("AllUpToDatePlaceholder"), ID_STATUS + 111), {
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(200) },
                            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM,
                            .childGap = DS_SPACE_3,
                        }
                    }) {
                        TY_TextColored(ID_STATUS + 112, ICON_CHECK2, TY_DISPLAY, ds_theme->success);
                        TY_TextColored(ID_STATUS + 113, "所有应用已是最新", TY_H3, ds_theme->subtext);
                    }
                } else {
                    for (size_t i = 0; i < g_state->update_count; i++) {
                        StoreUpdateItem *item = &g_state->update_list[i];
                        int base = ID_STATUS + 200 + (int)i * 20;

                        Clay_ElementId card_id = Clay_GetElementIdWithIndex(CLAY_STRING("UpdateCard"), (int)i);
                        bool card_hov = Clay_PointerOver(card_id);
                        Clay_Color card_bg = card_hov ? ds_theme->surface1 : ds_theme->surface0;

                        CLAY(card_id, {
                            .layout = {
                                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                                .padding         = { DS_SPACE_3, DS_SPACE_3, DS_SPACE_3, DS_SPACE_3 },
                                .childGap        = DS_SPACE_3,
                                .childAlignment  = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                            },
                            .backgroundColor = card_bg,
                            .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_LG),
                        }) {
                            char letter[2] = { '?', '\0' };
                            if (item->name && item->name[0]) {
                                letter[0] = item->name[0];
                                if (item->id) {
                                    const char *last_dot = strrchr(item->id, '.');
                                    if (last_dot && last_dot[1]) {
                                        letter[0] = last_dot[1];
                                    }
                                }
                            }
                            if (letter[0] >= 'a' && letter[0] <= 'z') {
                                letter[0] = (char)(letter[0] - 'a' + 'A');
                            }

                            letter_icon_placeholder(base + 1, ICON_PLACEHOLDER, letter);

                            /* Text block: Name and Version transition */
                            CLAY(CLAY_SIDI(CLAY_STRING("UpdateCardText"), base + 2), {
                                .layout = {
                                    .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                    .childGap        = 4,
                                },
                            }) {
                                TY_Text(base + 3, item->name ? item->name : item->id, TY_H4);

                                if (item->updating) {
                                    char prog_text[64];
                                    snprintf(prog_text, sizeof(prog_text), "正在更新中... %.0f%%", item->progress * 100.0f);
                                    UI_Progress(base + 4, prog_text, item->progress, 1.0f);
                                } else {
                                    char ver_text[128];
                                    snprintf(ver_text, sizeof(ver_text), "%s  \xe2\x86\x92  %s",
                                             item->current_version ? item->current_version : "0.0.0",
                                             item->new_version ? item->new_version : "0.0.0");
                                    CLAY(CLAY_SIDI(CLAY_STRING("UpdateCardVer"), base + 5), {
                                        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } }
                                    }) {
                                        CLAY_TEXT(UI__str(ver_text), { .textColor = ds_theme->accent, .fontSize = DS_FS_SM });
                                    }
                                }
                            }

                            CLAY(CLAY_SIDI(CLAY_STRING("UpdateCardSpacer"), base + 10), {
                                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }
                            }) {}

                            if (item->updating) {
                                UI_Button(base + 11, "\xe2\x86\xba  更新中", UI_BTN_GHOST, UI_BTN_SM, true);
                            } else {
                                if (UI_Button(base + 11, "\xe2\x86\xba  更新", UI_BTN_PRIMARY, UI_BTN_SM, false)) {
                                    store_ui_trigger_update_item(item);
                                }
                            }
                        }
                    }
                }
            }
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
        if (g_state->active_nav == NAV_UPDATES) {
            updates_view();
        } else {
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

void store_ui_handle_pre_layout_actions(StoreState *state, bool mouse_released)
{
    if (!state || !mouse_released) return;

    Clay_ElementId theme_id = Clay_GetElementIdWithIndex(
        CLAY_STRING("UIIconBtn"), ID_THEME_TOGGLE);

    if (Clay_PointerOver(theme_id)) {
        state->dark_mode = !state->dark_mode;
        DS_SetTheme(state->dark_mode ? &DS_THEME_DARK : &DS_THEME_LIGHT);
        state->dirty = true;
    }
}
