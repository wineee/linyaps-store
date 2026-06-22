/* SPDX-License-Identifier: MIT */
/* ui/views/ranking_view.c — Ranking page with tabs & pagination */

#include "views_internal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *format_release_time(const char *create_time) {
  if (!create_time || strlen(create_time) < 10)
    return "刚刚上架";

  int y = 0, m = 0, d = 0;
  if (sscanf(create_time, "%d-%d-%d", &y, &m, &d) != 3)
    return "刚刚上架";

  /* 获取当前系统时间 */
  time_t now = time(NULL);
  struct tm *tm_now = localtime(&now);
  int cur_y = tm_now->tm_year + 1900;
  int cur_m = tm_now->tm_mon + 1;
  int cur_d = tm_now->tm_mday;

  /* 简化的天数计算（近似值足够用于显示） */
  int days = (cur_y - y) * 365 + (cur_m - m) * 30 + (cur_d - d);
  if (days < 0)
    days = 0;

  static char buf[64];
  if (days == 0)
    return "今天上架";
  else if (days < 30) {
    snprintf(buf, sizeof(buf), "%d天前上架", days);
    return buf;
  } else if (days < 365) {
    snprintf(buf, sizeof(buf), "%d个月前上架", days / 30);
    return buf;
  } else {
    snprintf(buf, sizeof(buf), "%d年前上架", days / 365);
    return buf;
  }
}

static const char *format_download_count(int64_t count) {
  static char buf[64];
  if (count <= 0)
    return "暂无下载";
  else if (count < 1000)
    snprintf(buf, sizeof(buf), "下载 %ld次", (long)count);
  else if (count < 1000000)
    snprintf(buf, sizeof(buf), "下载 %ld,%03ld次", (long)(count / 1000),
             (long)(count % 1000));
  else
    snprintf(buf, sizeof(buf), "下载 %.1f万次", (double)count / 10000.0);
  return buf;
}

static void ranking_app_card(int card_idx, int rank,
                             const LinyapsPackageInfo *info, bool installed,
                             int card_w) {
  int base = ID_CARD_BASE + 20000 + card_idx * 20;

  Clay_ElementId card_id =
      Clay_GetElementIdWithIndex(CLAY_STRING("RankCard"), card_idx);
  bool card_hov = Clay_PointerOver(card_id);
  Clay_Color card_bg = card_hov ? ds_theme->surface1 : ds_theme->surface0;

  CLAY(card_id,
       {
           .layout =
               {
                   .sizing = {CLAY_SIZING_FIXED((float)card_w),
                              CLAY_SIZING_FIXED(CARD_H)},
                   .layoutDirection = CLAY_LEFT_TO_RIGHT,
                   .padding = {DS_SPACE_3, DS_SPACE_3, DS_SPACE_3, DS_SPACE_3},
                   .childGap = DS_SPACE_3,
                   .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER},
               },
           .backgroundColor = card_bg,
           .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_LG),
       }) {
    /* Rank badge */
    Clay_Color rank_bg, rank_fg = ds_theme->text;
    if (rank == 1) {
      rank_bg = (Clay_Color){249, 226, 175, 255};
      rank_fg = (Clay_Color){17, 17, 27, 255};
    } else if (rank == 2) {
      rank_bg = (Clay_Color){166, 173, 200, 255};
      rank_fg = (Clay_Color){17, 17, 27, 255};
    } else if (rank == 3) {
      rank_bg = (Clay_Color){250, 179, 135, 255};
      rank_fg = (Clay_Color){17, 17, 27, 255};
    } else {
      rank_bg = ds_theme->surface1;
      rank_fg = ds_theme->muted;
    }

    CLAY(CLAY_SIDI(CLAY_STRING("RankBadgeBox"), base),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(32)},
                     .childAlignment = {CLAY_ALIGN_X_CENTER,
                                        CLAY_ALIGN_Y_CENTER},
                 },
             .backgroundColor = rank_bg,
             .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
         }) {
      const char *rank_str = store_ui_frame_str("%d", rank);
      CLAY_TEXT(UI__str(rank_str),
                {.textColor = rank_fg, .fontSize = DS_FS_MD});
    }

    /* Letter icon */
    char letter_buf[2] = {'?', '\0'};
    if (info->name && info->name[0]) {
      letter_buf[0] = info->name[0];
      if (info->id) {
        const char *last_dot = strrchr(info->id, '.');
        if (last_dot && last_dot[1])
          letter_buf[0] = last_dot[1];
      }
    }
    if (letter_buf[0] >= 'a' && letter_buf[0] <= 'z')
      letter_buf[0] = (char)(letter_buf[0] - 'a' + 'A');
    letter_icon_placeholder(base + 1, ICON_PLACEHOLDER,
                            store_ui_frame_str("%s", letter_buf));

    /* Text block */
    CLAY(CLAY_SIDI(CLAY_STRING("RankCardText"), base + 2),
         {
             .layout =
                 {
                     .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 2,
                 },
         }) {
      TY_Text(base + 3, info->name ? info->name : info->id, TY_H4);

      CLAY(CLAY_SIDI(CLAY_STRING("RankCardDesc"), base + 4),
           {
               .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
               .clip = {.horizontal = true},
           }) {
        const char *desc = info->description ? info->description : "";
        CLAY_TEXT(UI__str(desc),
                  {.textColor = ds_theme->subtext, .fontSize = DS_FS_SM});
      }

      CLAY(CLAY_SIDI(CLAY_STRING("RankCardMeta"), base + 6),
           {
               .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}},
           }) {
        const char *meta = (g_state->ranking_tab == 0)
                               ? format_release_time(info->create_time)
                               : format_download_count(info->download_count);
        CLAY_TEXT(UI__str(store_ui_frame_str("%s", meta)),
                  {.textColor = ds_theme->muted, .fontSize = DS_FS_SM});
      }
    }

    CLAY(CLAY_SIDI(CLAY_STRING("RankCardSpacer"), base + 10),
         {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}}}) {}

    /* Action button */
    if (installed) {
      if (UI_Button(base + 5, "打开", UI_BTN_SECONDARY, UI_BTN_SM, false)) {
        launch_app(info->id);
      }
    } else {
      if (UI_Button(base + 5, "安装", UI_BTN_PRIMARY, UI_BTN_SM, false)) {
        LOG_INFO("ui", "安装按钮点击: id=%s ver=%s",
                 info->id ? info->id : "(unknown)",
                 info->version ? info->version : "-");
        if (g_state->ctx) {
          linyaps_install(g_state->ctx, info->id, info->version, info->channel,
                          NULL, NULL);
        } else {
          LOG_WARN("ui", "后端未连接，无法安装 %s", info->id ? info->id : "?");
        }
        SDL_SetAtomicInt(&g_state->dirty, 1);
      }
    }
  }
}

void view_ranking(void) {
  UI_COL(ID_STATUS + 500, DS_SPACE_3) {
    /* Tab bar */
    UI_ROW(ID_STATUS + 501, DS_SPACE_3) {
      for (int t = 0; t < 2; t++) {
        bool active = (g_state->ranking_tab == t);
        Clay_ElementId tab_id =
            Clay_GetElementIdWithIndex(CLAY_STRING("RankingTab"), t);
        bool hov = Clay_PointerOver(tab_id);
        if (hov && UI__mouse_released)
          store_ui_trigger_change_ranking_tab(t);

        Clay_Color bg = active ? ds_theme->accent
                        : hov  ? ds_theme->surface1
                               : ds_theme->surface0;
        Clay_Color fg = active ? ds_theme->base : ds_theme->text;
        const char *label = (t == 0) ? "最新上架榜" : "下载量榜";

        CLAY(tab_id, {
                         .layout = {.padding = {DS_SPACE_4, DS_SPACE_4,
                                                DS_SPACE_2, DS_SPACE_2}},
                         .backgroundColor = bg,
                         .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
                     }) {
          CLAY_TEXT(UI__str(label), {.textColor = fg, .fontSize = DS_FS_MD});
        }
      }
    }

    /* Content */
    if (SDL_GetAtomicInt(&g_state->loading_ranking)) {
      /* Skeleton cards while loading — assume pagination */
      AppGridSpec spec = app_grid_spec(44, true);
      CLAY(CLAY_SIDI(CLAY_STRING("RankingSkeleton"), ID_STATUS + 510),
           {
               .layout =
                   {
                       .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                       .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = DS_SPACE_3,
                   },
           }) {
        for (int row = 0; row < spec.rows; row++) {
          UI_ROW(ID_STATUS + 520 + row * 2, DS_SPACE_3) {
            for (int col = 0; col < spec.cols; col++) {
              int uid = ID_STATUS + 600 + row * 20 + col * 2;
              Clay_Color bar = ds_theme->surface1;
              Clay_Color bar2 = ds_theme->surface0;
              CLAY(CLAY_SIDI(CLAY_STRING("RSkel"), uid),
                   {
                       .layout =
                           {
                               .sizing = {CLAY_SIZING_FIXED((float)spec.card_w),
                                          CLAY_SIZING_FIXED(CARD_H)},
                               .layoutDirection = CLAY_LEFT_TO_RIGHT,
                               .padding = {DS_SPACE_3, DS_SPACE_3, DS_SPACE_3,
                                           DS_SPACE_3},
                               .childGap = DS_SPACE_3,
                               .childAlignment = {CLAY_ALIGN_X_LEFT,
                                                  CLAY_ALIGN_Y_CENTER},
                           },
                       .backgroundColor = ds_theme->surface0,
                       .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_LG),
                   }) {
                /* Rank badge skeleton */
                CLAY(CLAY_SIDI(CLAY_STRING("RSkelRank"), uid + 1),
                     {
                         .layout = {.sizing = {CLAY_SIZING_FIXED(32),
                                               CLAY_SIZING_FIXED(32)}},
                         .backgroundColor = bar2,
                         .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
                     }) {}
                /* Icon skeleton */
                CLAY(CLAY_SIDI(CLAY_STRING("RSkelIcon"), uid + 2),
                     {
                         .layout = {.sizing = {CLAY_SIZING_FIXED(
                                                   (float)ICON_PLACEHOLDER),
                                               CLAY_SIZING_FIXED(
                                                   (float)ICON_PLACEHOLDER)}},
                         .backgroundColor = bar,
                         .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
                     }) {}
                /* Text skeleton */
                CLAY(CLAY_SIDI(CLAY_STRING("RSkelText"), uid + 3),
                     {
                         .layout =
                             {
                                 .sizing = {CLAY_SIZING_GROW(0),
                                            CLAY_SIZING_FIT(0)},
                                 .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                 .childGap = 8,
                             },
                     }) {
                  CLAY(CLAY_SIDI(CLAY_STRING("RSkelTitle"), uid + 4),
                       {
                           .layout = {.sizing = {CLAY_SIZING_PERCENT(0.5f),
                                                 CLAY_SIZING_FIXED(14)}},
                           .backgroundColor = bar,
                           .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_SM),
                       }) {}
                  CLAY(CLAY_SIDI(CLAY_STRING("RSkelDesc"), uid + 5),
                       {
                           .layout = {.sizing = {CLAY_SIZING_PERCENT(0.8f),
                                                 CLAY_SIZING_FIXED(10)}},
                           .backgroundColor = bar2,
                           .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_SM),
                       }) {}
                }
                /* Button skeleton */
                CLAY(CLAY_SIDI(CLAY_STRING("RSkelBtn"), uid + 6),
                     {
                         .layout = {.sizing = {CLAY_SIZING_FIXED(56),
                                               CLAY_SIZING_FIXED(28)}},
                         .backgroundColor = bar,
                         .cornerRadius = CLAY_CORNER_RADIUS(DS_RADIUS_MD),
                     }) {}
              }
            }
          }
        }
      }
    } else if (!g_state->ranking_list || g_state->ranking_count == 0) {
      CLAY(CLAY_SIDI(CLAY_STRING("RankingEmpty"), ID_STATUS + 510),
           {.layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(200)},
                .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap = DS_SPACE_3,
            }}) {
        TY_Text(ID_STATUS + 511, "\xf0\x9f\x92\xa4", TY_DISPLAY);
        TY_TextColored(ID_STATUS + 512, "暂无排行数据", TY_H3,
                       ds_theme->subtext);
      }
    } else {
      /* First pass: assume pagination */
      AppGridSpec spec = app_grid_spec(44, true);
      long total_pages = (g_state->ranking_total + spec.items_per_page - 1) /
                         spec.items_per_page;
      if (total_pages < 1)
        total_pages = 1;
      int cur_p = g_state->current_page;
      bool show_pagination = (total_pages > 1);

      /* Second pass: if only 1 page, recalculate without pagination */
      if (!show_pagination) {
        spec = app_grid_spec(44, false);
        total_pages = (g_state->ranking_total + spec.items_per_page - 1) /
                      spec.items_per_page;
        if (total_pages < 1)
          total_pages = 1;
        show_pagination = (total_pages > 1);
      }

      CLAY(CLAY_SIDI(CLAY_STRING("RankingContent"), ID_STATUS + 519),
           {
               .layout =
                   {
                       .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                       .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = DS_SPACE_3,
                   },
           }) {
        /* Scrollable grid area */
        CLAY(CLAY_SIDI(CLAY_STRING("RankingGridScroll"), ID_STATUS + 520),
             {
                 .layout =
                     {
                         .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = DS_SPACE_3,
                     },
                 .clip = {.vertical = true},
             }) {
          size_t i = 0, count = g_state->ranking_count;
          /* Limit display to items_per_page even if we have more cached data */
          if (count > (size_t)spec.items_per_page)
            count = (size_t)spec.items_per_page;
          int row_idx = 0;
          while (i < count) {
            UI_ROW(ID_STATUS + 600 + row_idx * 2, DS_SPACE_3) {
              int col = 0;
              for (; col < spec.cols && i < count; col++, i++) {
                bool inst = (g_state->ranking_list[i]->id &&
                             id_set_contains(&g_state->installed_id_set,
                                             g_state->ranking_list[i]->id));
                ranking_app_card(
                    (int)i,
                    (int)(g_state->current_page * spec.items_per_page + i + 1),
                    g_state->ranking_list[i], inst, spec.card_w);
              }
              for (; col < spec.cols; col++) {
                CLAY(CLAY_SIDI(CLAY_STRING("RankCardFill"),
                               ID_STATUS + 800 + col),
                     {
                         .layout = {.sizing = {CLAY_SIZING_FIXED(
                                                   (float)spec.card_w),
                                               CLAY_SIZING_FIXED(CARD_H)}},
                     }) {}
              }
            }
            row_idx++;
          }
        }

        /* Pagination — fixed at bottom, not scrollable */
        if (show_pagination) {
          CLAY(CLAY_SIDI(CLAY_STRING("RankingPag"), ID_STATUS + 900),
               {
                   .layout =
                       {
                           .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                           .layoutDirection = CLAY_LEFT_TO_RIGHT,
                           .childAlignment = {CLAY_ALIGN_X_CENTER,
                                              CLAY_ALIGN_Y_CENTER},
                           .childGap = DS_SPACE_4,
                           .padding = {DS_SPACE_2, DS_SPACE_2, 0, 0},
                       },
               }) {
            if (UI_Button(ID_STATUS + 901, "< 上一页", UI_BTN_SECONDARY,
                          UI_BTN_SM, cur_p <= 0))
              store_ui_trigger_change_ranking_page(cur_p - 1);

            const char *page_str =
                store_ui_frame_str("第 %d / %ld 页", cur_p + 1, total_pages);
            CLAY_TEXT(UI__str(page_str),
                      {.textColor = ds_theme->text, .fontSize = DS_FS_MD});

            if (UI_Button(ID_STATUS + 902, "下一页 >", UI_BTN_SECONDARY,
                          UI_BTN_SM, cur_p >= total_pages - 1))
              store_ui_trigger_change_ranking_page(cur_p + 1);
          }
        }
      }
    }
  }
}
