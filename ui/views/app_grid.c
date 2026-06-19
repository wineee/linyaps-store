/* ui/views/app_grid.c — App card grid with pagination */

#include "views_internal.h"

#include <string.h>

static void app_card(int card_idx, const LinyapsPackageInfo *info, bool installed, int card_w)
{
    int base = ID_CARD_BASE + card_idx * 20;

    Clay_ElementId card_id = Clay_GetElementIdWithIndex(CLAY_STRING("AppCard"), card_idx);
    bool card_hov = Clay_PointerOver(card_id);
    Clay_Color card_bg = card_hov ? ds_theme->surface1 : ds_theme->surface0;

    CLAY(card_id, {
        .layout = {
            .sizing          = { CLAY_SIZING_FIXED((float)card_w), CLAY_SIZING_FIXED(CARD_H) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .padding         = { DS_SPACE_3, DS_SPACE_3, DS_SPACE_3, DS_SPACE_3 },
            .childGap        = DS_SPACE_3,
            .childAlignment  = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
        },
        .backgroundColor = card_bg,
        .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_LG),
    }) {
        icon_placeholder(base + 1, ICON_PLACEHOLDER);

        /* Text block: name + description (Expanded to fill available space) */
        CLAY(CLAY_SIDI(CLAY_STRING("CardText"), base + 2), {
            .layout = {
                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap        = 4,
            },
        }) {
            TY_Text(base + 3, info->name ? info->name : info->id, TY_H4);
            CLAY(CLAY_SIDI(CLAY_STRING("CardDesc"), base + 4), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } },
                .clip = { .horizontal = true },
            }) {
                const char *desc = info->description ? info->description : "";
                CLAY_TEXT(UI__str(desc),
                          { .textColor = ds_theme->subtext, .fontSize = DS_FS_SM });
            }
        }

        /* Action button — at the right edge */
        if (installed) {
            if (UI_Button(base + 5, "打开", UI_BTN_SECONDARY, UI_BTN_SM, false)) {
                const char *app_id = info->id ? info->id : NULL;
                LOG_INFO("ui", "打开按钮点击: id=%s", app_id ? app_id : "(unknown)");
                launch_app(app_id);
            }
        } else {
            if (UI_Button(base + 5, "安装", UI_BTN_PRIMARY, UI_BTN_SM, false)) {
                const char *app_id = info->id ? info->id : "(unknown)";
                LOG_INFO("ui", "安装按钮点击: id=%s ver=%s",
                         app_id, info->version ? info->version : "-");
                if (g_state->ctx) {
                    linyaps_install(g_state->ctx, info->id, info->version,
                                    info->channel, NULL, NULL);
                } else {
                    LOG_WARN("ui", "后端未连接，无法安装 %s", app_id);
                }
                SDL_SetAtomicInt(&g_state->dirty, 1);
            }
        }
    }
}

/* Skeleton placeholder card — mimics app_card layout with gray bars */
static void skeleton_card(int uid, int card_w)
{
    Clay_Color bar = ds_theme->surface1;
    Clay_Color bar2 = ds_theme->surface0;

    CLAY(CLAY_SIDI(CLAY_STRING("SkelCard"), uid), {
        .layout = {
            .sizing          = { CLAY_SIZING_FIXED((float)card_w), CLAY_SIZING_FIXED(CARD_H) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .padding         = { DS_SPACE_3, DS_SPACE_3, DS_SPACE_3, DS_SPACE_3 },
            .childGap        = DS_SPACE_3,
            .childAlignment  = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
        },
        .backgroundColor = ds_theme->surface0,
        .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_LG),
    }) {
        /* Icon skeleton */
        CLAY(CLAY_SIDI(CLAY_STRING("SkelIcon"), uid + 1), {
            .layout = { .sizing = { CLAY_SIZING_FIXED((float)ICON_PLACEHOLDER),
                                    CLAY_SIZING_FIXED((float)ICON_PLACEHOLDER) } },
            .backgroundColor = bar,
            .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
        }) {}

        /* Text skeleton: two gray bars */
        CLAY(CLAY_SIDI(CLAY_STRING("SkelText"), uid + 2), {
            .layout = {
                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap        = 8,
            },
        }) {
            /* Title bar (60% width) */
            CLAY(CLAY_SIDI(CLAY_STRING("SkelTitle"), uid + 3), {
                .layout = { .sizing = { CLAY_SIZING_PERCENT(0.6f), CLAY_SIZING_FIXED(14) } },
                .backgroundColor = bar,
                .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_SM),
            }) {}
            /* Desc bar (90% width) */
            CLAY(CLAY_SIDI(CLAY_STRING("SkelDesc"), uid + 4), {
                .layout = { .sizing = { CLAY_SIZING_PERCENT(0.9f), CLAY_SIZING_FIXED(10) } },
                .backgroundColor = bar2,
                .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_SM),
            }) {}
        }

        /* Button skeleton */
        CLAY(CLAY_SIDI(CLAY_STRING("SkelBtn"), uid + 5), {
            .layout = { .sizing = { CLAY_SIZING_FIXED(56), CLAY_SIZING_FIXED(28) } },
            .backgroundColor = bar,
            .cornerRadius    = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
        }) {}
    }
}

void view_app_grid(void)
{
    if (SDL_GetAtomicInt(&g_state->loading_remote)) {
        /* Show skeleton cards while loading */
        int reserve_h = 0;
        if (g_state->active_nav == NAV_ALL ||
            g_state->active_nav == NAV_RECOMMENDED ||
            (g_state->active_nav >= NAV_CAT_OFFICE && g_state->active_nav <= NAV_CAT_GAMES)) {
            reserve_h += CONTENT_HEADER_H;
        }
        AppGridSpec spec = app_grid_spec(reserve_h);

        CLAY(CLAY_SIDI(CLAY_STRING("SkeletonGrid"), ID_STATUS + 110), {
            .layout = {
                .sizing          = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap        = DS_SPACE_3,
            },
        }) {
            for (int row = 0; row < spec.rows; row++) {
                UI_ROW(ID_STATUS + 120 + row * 2, DS_SPACE_3) {
                    for (int col = 0; col < spec.cols; col++) {
                        skeleton_card(ID_STATUS + 200 + row * 20 + col * 2, spec.card_w);
                    }
                }
            }
        }
        return;
    }

    LinyapsPackageInfo **list = g_state->search_results;
    size_t count              = g_state->search_count;

    if (!list || count == 0) {
        CLAY(CLAY_SIDI(CLAY_STRING("EmptyState"), ID_STATUS), {
            .layout = {
                .sizing         = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap       = DS_SPACE_4,
            },
        }) {
            TY_Text(ID_STATUS + 1, "\xf0\x9f\x94\x8d", TY_DISPLAY);
            TY_TextColored(ID_STATUS + 2, "暂无应用", TY_H3, ds_theme->subtext);
            TY_TextColored(ID_STATUS + 3, "搜索或选择分类以查看应用", TY_BODY, ds_theme->muted);
        }
        return;
    }

    int reserve_h = 0;
    if (g_state->active_nav == NAV_ALL ||
        g_state->active_nav == NAV_RECOMMENDED ||
        (g_state->active_nav >= NAV_CAT_OFFICE && g_state->active_nav <= NAV_CAT_GAMES)) {
        reserve_h += CONTENT_HEADER_H;
    }
    AppGridSpec spec = app_grid_spec(reserve_h);
    int items_per_page = spec.items_per_page;

    /* Server-side pagination when remote_total is available, else client-side */
    bool use_server_paging = (g_state->remote_total > 0);
    int total_pages;
    if (use_server_paging) {
        total_pages = (int)((g_state->remote_total + items_per_page - 1) / items_per_page);
    } else {
        total_pages = (count + items_per_page - 1) / items_per_page;
    }
    if (total_pages < 1) total_pages = 1;
    if (g_state->current_page >= total_pages) g_state->current_page = total_pages - 1;
    if (g_state->current_page < 0) g_state->current_page = 0;

    /* For server-side paging, the fetched data IS the current page (no sub-slicing) */
    size_t start_idx = 0;
    size_t end_idx = count;

    UI_COL(ID_STATUS + 9, DS_SPACE_3) {
        UI_COL(ID_STATUS + 10, DS_SPACE_3) {
            size_t i = start_idx;
            int row_idx = 0;
            while (i < end_idx) {
                UI_ROW(ID_STATUS + 20 + row_idx * 2, DS_SPACE_3) {
                    int col = 0;
                    for (; col < spec.cols && i < end_idx; col++, i++) {
                        bool inst = false;
                        for (size_t k = 0; k < g_state->installed_count; k++) {
                            LinyapsPackageInfo *ins = g_state->installed_list[k];
                            if (ins && ins->id && list[i]->id &&
                                strcmp(ins->id, list[i]->id) == 0) {
                                inst = true;
                                break;
                            }
                        }
                        app_card((int)i, list[i], inst, spec.card_w);
                    }
                    for (; col < spec.cols; col++) {
                        CLAY(CLAY_SIDI(CLAY_STRING("CardFill"), ID_STATUS + 50 + col), {
                            .layout = { .sizing = { CLAY_SIZING_FIXED((float)spec.card_w),
                                                    CLAY_SIZING_FIXED(CARD_H) } },
                        }) {}
                    }
                }
                row_idx++;
            }
        }

        UI_SPACER(ID_STATUS + 80);

        if (g_state->is_searching && count > 0) {
            UI_ROW(ID_STATUS + 800, DS_SPACE_3) {
                UI_SPACER(ID_STATUS + 801);
                CLAY_TEXT(CLAY_STRING("没有更多了"), { .textColor = ds_theme->muted, .fontSize = DS_FS_SM });
                UI_SPACER(ID_STATUS + 802);
            }
        }

        if (total_pages > 1) {
            UI_ROW(ID_STATUS + 81, DS_SPACE_3) {
                UI_SPACER(ID_STATUS + 82);

                bool prev_disabled = (g_state->current_page <= 0);
                if (UI_Button(ID_STATUS + 83, "上一页",
                    prev_disabled ? UI_BTN_GHOST : UI_BTN_SECONDARY, UI_BTN_SM, prev_disabled)) {
                    if (!prev_disabled) {
                        if (use_server_paging)
                            store_ui_trigger_remote_page(g_state->current_page - 1);
                        else {
                            g_state->current_page--;
                            SDL_SetAtomicInt(&g_state->dirty, 1);
                        }
                    }
                }

                const char *page_text = store_ui_frame_str(
                    "第 %d / %d 页", g_state->current_page + 1, total_pages);
                CLAY(CLAY_SIDI(CLAY_STRING("PageText"), ID_STATUS + 84), {
                    .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) },
                                .padding = { 8, 8, 0, 0 } },
                }) {
                    CLAY_TEXT(UI__str(page_text), { .textColor = ds_theme->text, .fontSize = DS_FS_SM });
                }

                bool next_disabled = (g_state->current_page >= total_pages - 1);
                if (UI_Button(ID_STATUS + 85, "下一页",
                    next_disabled ? UI_BTN_GHOST : UI_BTN_SECONDARY, UI_BTN_SM, next_disabled)) {
                    if (!next_disabled) {
                        if (use_server_paging)
                            store_ui_trigger_remote_page(g_state->current_page + 1);
                        else {
                            g_state->current_page++;
                            SDL_SetAtomicInt(&g_state->dirty, 1);
                        }
                    }
                }

                UI_SPACER(ID_STATUS + 86);
            }
        }
    }
}
