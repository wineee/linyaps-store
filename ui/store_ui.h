/* ui/store_ui.h — Public interface for store_ui.c */

#pragma once

#include "store_state.h"

/* Build the full UI layout into Clay.
 * Call between Clay_BeginLayout() and Clay_EndLayout(). */
void store_ui_build(StoreState *state);
