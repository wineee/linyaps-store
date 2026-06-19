/* ui/store_state.c — StoreState lifecycle */

#include "store_state.h"

#include <string.h>
#include <stdlib.h>

void store_state_init(StoreState *s, LinyapsContext *ctx)
{
    memset(s, 0, sizeof(*s));
    s->ctx        = ctx;
    s->active_nav = NAV_ALL;
    s->active_cat = CAT_ALL;
    s->dark_mode  = true;
    s->dirty      = true;

    /* Initialize default updates matching reference screenshot */
    s->update_count = 3;
    s->update_list = calloc(s->update_count, sizeof(*s->update_list));
    if (s->update_list) {
        s->update_list[0] = (StoreUpdateItem){
            .id = strdup("cn.wps.wps-office"),
            .name = strdup("WPS Office For Linux 个人版"),
            .current_version = strdup("12.1.2.25882"),
            .new_version = strdup("12.1.2.26885"),
            .channel = strdup("main"),
        };
        s->update_list[1] = (StoreUpdateItem){
            .id = strdup("com.microsoft.edge"),
            .name = strdup("Microsoft Edge"),
            .current_version = strdup("148.0.3967.86"),
            .new_version = strdup("149.0.4022.69"),
            .channel = strdup("main"),
        };
        s->update_list[2] = (StoreUpdateItem){
            .id = strdup("org.localsend.localsend"),
            .name = strdup("LocalSend"),
            .current_version = strdup("1.17.0.3"),
            .new_version = strdup("1.17.0.5"),
            .channel = strdup("main"),
        };
    }
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
