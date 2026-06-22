/* SPDX-License-Identifier: MIT */
/* ui/views/updates_view.c — Updates page */

#include "views_internal.h"

void view_updates(void) {
  CLAY(CLAY_SIDI(CLAY_STRING("UpdatesWrap"), ID_ROOT + 3),
       {
           .layout =
               {
                   .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .padding = {DS_SPACE_4, DS_SPACE_4, DS_SPACE_4, DS_SPACE_4},
                   .childGap = DS_SPACE_3,
               },
           .backgroundColor = ds_theme->base,
       }) {
    UI_COL(ID_STATUS + 100, DS_SPACE_4) {
      /* Header row */
      UI_ROW(ID_STATUS + 101, DS_SPACE_3) {
        const char *header_text = "";
        if (SDL_GetAtomicInt(&g_state->checking_updates)) {
          header_text = "正在检查可更新的应用...";
        } else if (g_state->update_count > 0) {
          header_text =
              store_ui_frame_str("共 %zu 个应用可更新", g_state->update_count);
        } else {
          header_text = "您的应用已是最新版本";
        }
        TY_Text(ID_STATUS + 102, header_text, TY_H3);
        UI_SPACER(ID_STATUS + 103);

        if (SDL_GetAtomicInt(&g_state->checking_updates)) {
          UI_Button(ID_STATUS + 104, "\xe2\x86\xba  正在检查...", UI_BTN_GHOST,
                    UI_BTN_SM, true);
          UI_Button(ID_STATUS + 105, "\xe2\x86\xba  全部更新", UI_BTN_PRIMARY,
                    UI_BTN_SM, true);
        } else {
          if (UI_Button(ID_STATUS + 104, "\xe2\x86\xba  检查更新",
                        UI_BTN_SECONDARY, UI_BTN_SM, false))
            store_ui_trigger_check_updates();
          if (UI_Button(ID_STATUS + 105, "\xe2\x86\xba  全部更新",
                        UI_BTN_PRIMARY, UI_BTN_SM, g_state->update_count == 0))
            store_ui_trigger_update_all();
        }
      }

      /* Update card list */
      UI_SCROLLCOL(ID_STATUS + 110, DS_SPACE_3) {
        if (SDL_GetAtomicInt(&g_state->checking_updates)) {
          CLAY(
              CLAY_SIDI(CLAY_STRING("CheckingPlaceholder"), ID_STATUS + 111),
              {.layout = {
                   .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(200)},
                   .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .childGap = DS_SPACE_3,
               }}) {
            TY_Text(ID_STATUS + 112, ICON_SPINNER, TY_DISPLAY);
            TY_TextColored(ID_STATUS + 113, "正在获取服务器应用列表，请稍候...",
                           TY_BODY, ds_theme->muted);
          }
        } else if (g_state->update_count == 0) {
          CLAY(
              CLAY_SIDI(CLAY_STRING("AllUpToDate"), ID_STATUS + 111),
              {.layout = {
                   .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(200)},
                   .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .childGap = DS_SPACE_3,
               }}) {
            TY_TextColored(ID_STATUS + 112, ICON_CHECK2, TY_DISPLAY,
                           ds_theme->success);
            TY_TextColored(ID_STATUS + 113, "所有应用已是最新", TY_H3,
                           ds_theme->subtext);
          }
        } else {
          for (size_t i = 0; i < g_state->update_count; i++) {
            StoreUpdateItem *item = &g_state->update_list[i];
            int base = ID_STATUS + 200 + (int)i * 20;

            Clay_ElementId card_id =
                Clay_GetElementIdWithIndex(CLAY_STRING("UpdateCard"), (int)i);
            bool card_hov = Clay_PointerOver(card_id);
            Clay_Color card_bg =
                card_hov ? ds_theme->surface1 : ds_theme->surface0;

            CLAY(card_id, {
                              .layout =
                                  {
                                      .sizing = {CLAY_SIZING_GROW(0),
                                                 CLAY_SIZING_FIT(0)},
                                      .layoutDirection = CLAY_LEFT_TO_RIGHT,
                                      .padding = {DS_SPACE_3, DS_SPACE_3,
                                                  DS_SPACE_3, DS_SPACE_3},
                                      .childGap = DS_SPACE_3,
                                      .childAlignment = {CLAY_ALIGN_X_LEFT,
                                                         CLAY_ALIGN_Y_CENTER},
                                  },
                              .backgroundColor = card_bg,
                              .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_LG),
                          }) {
              char letter_buf[2] = {'?', '\0'};
              if (item->name && item->name[0]) {
                letter_buf[0] = item->name[0];
                if (item->id) {
                  const char *last_dot = strrchr(item->id, '.');
                  if (last_dot && last_dot[1])
                    letter_buf[0] = last_dot[1];
                }
              }
              if (letter_buf[0] >= 'a' && letter_buf[0] <= 'z')
                letter_buf[0] = (char)(letter_buf[0] - 'a' + 'A');

              letter_icon_placeholder(base + 1, ICON_PLACEHOLDER,
                                      store_ui_frame_str("%s", letter_buf));

              CLAY(CLAY_SIDI(CLAY_STRING("UpdateCardText"), base + 2),
                   {.layout = {
                        .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        .childGap = 4,
                    }}) {
                TY_Text(base + 3, item->name ? item->name : item->id, TY_H4);

                if (SDL_GetAtomicInt(&item->updating)) {
                  float progress =
                      SDL_GetAtomicInt(&item->progress_int) / 100.0f;
                  const char *prog_text = store_ui_frame_str(
                      "正在更新中... %.0f%%", progress * 100.0f);
                  UI_Progress(base + 4, prog_text, progress, 1.0f);
                } else {
                  const char *ver_text = store_ui_frame_str(
                      "%s  \xe2\x86\x92  %s",
                      item->current_version ? item->current_version : "0.0.0",
                      item->new_version ? item->new_version : "0.0.0");
                  CLAY(CLAY_SIDI(CLAY_STRING("UpdateCardVer"), base + 5),
                       {.layout = {.sizing = {CLAY_SIZING_GROW(0),
                                              CLAY_SIZING_FIT(0)}}}) {
                    CLAY_TEXT(UI__str(ver_text), {.textColor = ds_theme->accent,
                                                  .fontSize = DS_FS_SM});
                  }
                }
              }

              CLAY(CLAY_SIDI(CLAY_STRING("UpdateCardSpacer"), base + 10),
                   {.layout = {.sizing = {CLAY_SIZING_GROW(0),
                                          CLAY_SIZING_FIXED(1)}}}) {}

              if (SDL_GetAtomicInt(&item->updating)) {
                UI_Button(base + 11, "\xe2\x86\xba  更新中", UI_BTN_GHOST,
                          UI_BTN_SM, true);
              } else {
                if (UI_Button(base + 11, "\xe2\x86\xba  更新", UI_BTN_PRIMARY,
                              UI_BTN_SM, false))
                  store_ui_trigger_update_item(item);
              }
            }
          }
        }
      }
    }
  }
}
