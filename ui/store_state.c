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
}

void store_state_free(StoreState *s)
{
    if (!s) return;
    linyaps_package_info_list_free(s->search_results, s->search_count);
    linyaps_package_info_list_free(s->installed_list, s->installed_count);
    s->search_results = NULL;
    s->installed_list = NULL;
}
