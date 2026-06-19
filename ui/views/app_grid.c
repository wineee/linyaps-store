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

void view_app_grid(void)
{
    if (SDL_GetAtomicInt(&g_state->loading_remote)) {
        CLAY(CLAY_SIDI(CLAY_STRING("GridLoading"), ID_STATUS + 110), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(200) },
                .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap = DS_SPACE_3,
            }
        }) {
            TY_Text(ID_STATUS + 111, ICON_SPINNER, TY_DISPLAY);
            TY_TextColored(ID_STATUS + 112, "正在加载应用列表...", TY_BODY, ds_theme->muted);
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
    int total_pages = (count + items_per_page - 1) / items_per_page;
    if (g_state->current_page >= total_pages) g_state->current_page = total_pages - 1;
    if (g_state->current_page < 0) g_state->current_page = 0;

    size_t start_idx = g_state->current_page * items_per_page;
    size_t end_idx = start_idx + items_per_page;
    if (end_idx > count) end_idx = count;

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

        if (total_pages > 1) {
            UI_ROW(ID_STATUS + 81, DS_SPACE_3) {
                UI_SPACER(ID_STATUS + 82);

                if (UI_Button(ID_STATUS + 83, "上一页",
                    g_state->current_page > 0 ? UI_BTN_SECONDARY : UI_BTN_GHOST, UI_BTN_SM, false)) {
                    if (g_state->current_page > 0) {
                        g_state->current_page--;
                        SDL_SetAtomicInt(&g_state->dirty, 1);
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

                if (UI_Button(ID_STATUS + 85, "下一页",
                    g_state->current_page < total_pages - 1 ? UI_BTN_SECONDARY : UI_BTN_GHOST, UI_BTN_SM, false)) {
                    if (g_state->current_page < total_pages - 1) {
                        g_state->current_page++;
                        SDL_SetAtomicInt(&g_state->dirty, 1);
                    }
                }

                UI_SPACER(ID_STATUS + 86);
            }
        }
    }
}
