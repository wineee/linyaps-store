/* SPDX-License-Identifier: LGPL-3.0-or-later */

#pragma once

#include "linyaps_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LinyapsContext LinyapsContext;

LinyapsContext *linyaps_context_new(void);
void linyaps_context_free(LinyapsContext *ctx);

int linyaps_process(LinyapsContext *ctx);
bool linyaps_is_service_available(LinyapsContext *ctx);

void linyaps_search(LinyapsContext *ctx,
                    const char *keyword,
                    const char *repos,
                    LinyapsSearchCallback cb,
                    void *userdata);

LinyapsPackageInfo **linyaps_list_installed(LinyapsContext *ctx,
                                            size_t *out_count);

LinyapsPackageInfo *linyaps_info(LinyapsContext *ctx, const char *app_id);

void linyaps_install(LinyapsContext *ctx,
                     const char *app_id,
                     const char *version,
                     const char *channel,
                     LinyapsProgressCallback cb,
                     void *userdata);

void linyaps_uninstall(LinyapsContext *ctx,
                       const char *app_id,
                       const char *version,
                       const char *channel,
                       LinyapsProgressCallback cb,
                       void *userdata);

void linyaps_update(LinyapsContext *ctx,
                    const char *app_id,
                    LinyapsProgressCallback cb,
                    void *userdata);

void linyaps_cancel_task(LinyapsContext *ctx, const char *task_object_path);
void linyaps_reply_interaction(LinyapsContext *ctx,
                               const char *task_object_path,
                               bool accepted);

void linyaps_set_interaction_callback(LinyapsContext *ctx,
                                      LinyapsInteractionCallback cb,
                                      void *userdata);

void linyaps_prune(LinyapsContext *ctx,
                   LinyapsCompletionCallback cb,
                   void *userdata);

void linyaps_package_info_free(LinyapsPackageInfo *info);
void linyaps_package_info_list_free(LinyapsPackageInfo **list, size_t count);
const char *linyaps_task_state_string(LinyapsTaskState state);

#ifdef __cplusplus
}
#endif
