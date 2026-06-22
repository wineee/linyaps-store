/* SPDX-License-Identifier: MIT */
/* ui/views/titlebar.c — Top bar with logo, search, theme toggle */

#include "views_internal.h"

void view_titlebar(void) {
  CLAY(CLAY_SIDI(CLAY_STRING("Titlebar"), ID_TITLEBAR),
       {
           .layout =
               {
                   .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(52)},
                   .layoutDirection = CLAY_LEFT_TO_RIGHT,
                   .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER},
                   .padding = {DS_SPACE_4, DS_SPACE_4, 0, 0},
                   .childGap = DS_SPACE_4,
               },
           .backgroundColor = ds_theme->crust,
           .border =
               {
                   .color = ds_theme->surface0,
                   .width = {.bottom = 1},
               },
       }) {
    /* Logo / app name */
    CLAY(CLAY_SIDI(CLAY_STRING("TitleLogo"), ID_TITLEBAR + 1),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)},
                     .childGap = DS_SPACE_2,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER},
                 },
         }) {
      CLAY(CLAY_SIDI(CLAY_STRING("AppIcon"), ID_TITLEBAR + 2),
           {
               .layout = {.sizing = {CLAY_SIZING_FIXED(24),
                                     CLAY_SIZING_FIXED(24)}},
               .backgroundColor = ds_theme->accent,
               .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_FULL),
           }) {}
      TY_Text(ID_TITLEBAR + 3, "玲珑应用商店", TY_H4);
    }

    /* Search bar — UI_Input handles its own styling and click detection */
    CLAY(CLAY_SIDI(CLAY_STRING("SearchWrap"), ID_SEARCH),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0),
                                CLAY_SIZING_FIXED(DS_HEIGHT_SM)},
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER},
                 },
         }) {
      UIInputResult ir = UI_Input(ID_SEARCH + 1, NULL, g_state->search_buf,
                                  "搜索应用", g_state->search_focused, false);
      if (ir.clicked) {
        g_state->search_focused = true;
        SDL_SetAtomicInt(&g_state->dirty, 1);
      }
      /* Release focus when clicking outside the search input */
      if (UI__mouse_released && !ir.clicked && g_state->search_focused) {
        g_state->search_focused = false;
        UI_Input_ResetFocus();
        SDL_SetAtomicInt(&g_state->dirty, 1);
      }
    }

    UI_SPACER(ID_TITLEBAR + 10);

    /* Theme toggle */
    UI_IconButton(ID_THEME_TOGGLE,
                  g_state->dark_mode ? "\xe2\x98\x80" : "\xe2\x98\xbd",
                  DS_FS_LG, UI_BTN_GHOST, false);
  }
}
