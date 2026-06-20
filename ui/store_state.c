/* SPDX-License-Identifier: MIT */
/* ui/store_state.c — StoreState lifecycle */

#include "store_state.h"

#include <string.h>
#include <stdlib.h>

void store_state_init(StoreState *s, LinyapsContext *ctx)
{
    memset(s, 0, sizeof(*s));
    s->ctx        = ctx;
    s->active_nav = NAV_RECOMMENDED;
    s->active_cat = CAT_ALL;
    s->dark_mode  = true;
    s->window_w   = 1280;
    s->window_h   = 720;
    SDL_SetAtomicInt(&s->dirty, 1);

    /* 更新列表将在启动时通过真实 API 检查后填充 */
    s->update_count = 0;
    s->update_list = NULL;

    /* 初始化已安装应用 ID 集合 */
    id_set_init(&s->installed_id_set);
}

void store_state_free(StoreState *s)
{
    if (!s) return;
    linyaps_package_info_list_free(s->search_results, s->search_count);
    linyaps_package_info_list_free(s->installed_list, s->installed_count);
    linyaps_package_info_list_free(s->ranking_list, s->ranking_count);
    s->search_results = NULL;
    s->installed_list = NULL;
    s->ranking_list   = NULL;

    id_set_free(&s->installed_id_set);

    if (s->update_list) {
        for (size_t i = 0; i < s->update_count; i++) {
            free(s->update_list[i].id);
            free(s->update_list[i].name);
            free(s->update_list[i].current_version);
            free(s->update_list[i].new_version);
            free(s->update_list[i].channel);
            free(s->update_list[i].task_path);
        }
        free(s->update_list);
        s->update_list = NULL;
        s->update_count = 0;
    }
}
