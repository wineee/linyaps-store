/* ui/store_ui.h — Public interface for store_ui.c */

#pragma once

#include "store_state.h"

/* Build the full UI layout into Clay.
 * Call between Clay_BeginLayout() and Clay_EndLayout(). */
void store_ui_build(StoreState *state);

/* Handle actions that must affect the whole frame before layout commands
 * are generated, such as switching the active design-system theme. */
void store_ui_handle_pre_layout_actions(StoreState *state, bool mouse_released);

/* Update action triggers */
void store_ui_trigger_check_updates(void);
void store_ui_trigger_update_all(void);
void store_ui_trigger_update_item(StoreUpdateItem *item);
void store_ui_trigger_change_nav(NavItem item);
void store_ui_trigger_change_ranking_tab(int tab_idx);
void store_ui_trigger_change_ranking_page(int page_idx);
void store_ui_trigger_change_category_tab(CategoryTab tab);
