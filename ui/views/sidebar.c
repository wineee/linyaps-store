/* ui/views/sidebar.c — Sidebar navigation */

#include "views_internal.h"

static void sidebar_nav_item(int uid, NavItem item, const char *label,
                              const char *icon_str)
{
    bool active = (g_state->active_nav == item);
    Clay_ElementId eid = Clay_GetElementIdWithIndex(CLAY_STRING("NavItem"), uid);
    bool hov = Clay_PointerOver(eid);

    if (hov && UI__mouse_released) {
        store_ui_trigger_change_nav(item);
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
            CLAY(CLAY_SIDI(CLAY_STRING("NavSpacer"), uid + 100), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }
            }) {}

            const char *count_str = store_ui_frame_str("%zu", g_state->update_count);
            UI_Badge(uid + 200, count_str, DS_VARIANT_ERROR);
        }
    }
}

void view_sidebar(void)
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
        sidebar_nav_item(0, NAV_RECOMMENDED, NAV_LABELS[NAV_RECOMMENDED], "\xe2\x99\xa1");
        sidebar_nav_item(1, NAV_ALL,         NAV_LABELS[NAV_ALL],         "\xe2\x98\xb0");
        sidebar_nav_item(2, NAV_RANKING,     NAV_LABELS[NAV_RANKING],     "\xf0\x9f\x93\x8a");
        sidebar_nav_item(3, NAV_UPDATES,     NAV_LABELS[NAV_UPDATES],     "\xe2\x86\xba");

        UI_Divider(ID_SIDEBAR + 10);

        TY_Text(ID_SIDEBAR + 11, "分类", TY_CAPTION);
        sidebar_nav_item(4, NAV_CAT_OFFICE, NAV_LABELS[NAV_CAT_OFFICE], "\xf0\x9f\x96\xa5");
        sidebar_nav_item(5, NAV_CAT_SYSTEM, NAV_LABELS[NAV_CAT_SYSTEM], "\xe2\x9a\x99");
        sidebar_nav_item(6, NAV_CAT_DEV,    NAV_LABELS[NAV_CAT_DEV],    "\xe2\x8c\xa8");
        sidebar_nav_item(7, NAV_CAT_GAMES,  NAV_LABELS[NAV_CAT_GAMES],  "\xf0\x9f\x8e\xae");

        UI_SPACER(ID_SIDEBAR + 20);

        UI_Divider(ID_SIDEBAR + 21);
        UI_ROW(ID_SIDEBAR + 22, DS_SPACE_1) {
            UI_IconButton(ID_SIDEBAR + 23, "\xf0\x9f\x93\x81", DS_FS_LG, UI_BTN_GHOST, false);
            UI_IconButton(ID_SIDEBAR + 24, "\xe2\xac\x87", DS_FS_LG, UI_BTN_GHOST, false);
            UI_IconButton(ID_SIDEBAR + 26, "\xe2\x9a\x99", DS_FS_LG, UI_BTN_GHOST, false);
        }
    }
}

void view_category_tab_bar(void)
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
                store_ui_trigger_change_category_tab((CategoryTab)i);
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
