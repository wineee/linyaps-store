/* SPDX-License-Identifier: LGPL-3.0-or-later */

#include "linyaps_private.h"

/* ------------------------------------------------------------------ */
/* Context lifecycle                                                    */
/* ------------------------------------------------------------------ */

LinyapsContext *linyaps_context_new(void)
{
    LinyapsContext *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    int r = sd_bus_open_system(&ctx->bus);
    if (r < 0) { free(ctx); return NULL; }
    r = sd_bus_add_match(ctx->bus, &ctx->task_added_slot,
                         "type='signal',sender='" PM_DEST "',interface='" PM_IFACE "',member='TaskAdded'",
                         on_task_added, ctx);
    if (r < 0) { linyaps_context_free(ctx); return NULL; }
    r = sd_bus_add_match(ctx->bus, &ctx->task_removed_slot,
                         "type='signal',sender='" PM_DEST "',interface='" PM_IFACE "',member='TaskRemoved'",
                         on_task_removed, ctx);
    if (r < 0) { linyaps_context_free(ctx); return NULL; }
    r = sd_bus_add_match(ctx->bus, &ctx->search_finished_slot,
                         "type='signal',sender='" PM_DEST "',interface='" PM_IFACE "',member='SearchFinished'",
                         on_search_finished, ctx);
    if (r < 0) { linyaps_context_free(ctx); return NULL; }
    return ctx;
}

void linyaps_context_free(LinyapsContext *ctx)
{
    if (!ctx) return;
    while (ctx->pending_head) pending_pop(ctx, NULL, NULL);
    while (ctx->searches) {
        SearchOp *next = ctx->searches->next;
        search_op_free(ctx->searches);
        ctx->searches = next;
    }
    for (size_t i = 0; i < MAX_TASKS; i++) clear_task(&ctx->tasks[i]);
    ctx->task_added_slot    = sd_bus_slot_unref(ctx->task_added_slot);
    ctx->task_removed_slot  = sd_bus_slot_unref(ctx->task_removed_slot);
    ctx->search_finished_slot = sd_bus_slot_unref(ctx->search_finished_slot);
    ctx->bus = sd_bus_unref(ctx->bus);
    free(ctx);
}

/* ------------------------------------------------------------------ */
/* Event loop integration                                               */
/* ------------------------------------------------------------------ */

int linyaps_process(LinyapsContext *ctx)
{
    if (!ctx || !ctx->bus) return -EINVAL;
    int processed = 0;
    for (;;) {
        int r = sd_bus_process(ctx->bus, NULL);
        if (r < 0) return r;
        if (r == 0) break;
        processed += r;
    }
    return processed;
}

bool linyaps_is_service_available(LinyapsContext *ctx)
{
    if (!ctx || !ctx->bus) return false;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(ctx->bus, PM_DEST, PM_PATH,
                               "org.freedesktop.DBus.Peer", "Ping",
                               &err, &reply, "");
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return r >= 0;
}

/* ------------------------------------------------------------------ */
/* Search                                                               */
/* ------------------------------------------------------------------ */

void linyaps_search(LinyapsContext *ctx,
                    const char *keyword,
                    const char *repos,
                    LinyapsSearchCallback cb,
                    void *userdata)
{
    if (!ctx || !ctx->bus || !keyword || !*keyword) {
        if (cb) cb(NULL, 0, -EINVAL, "invalid search request", userdata);
        return;
    }
    sd_bus_message *m = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_message_new_method_call(ctx->bus, &m, PM_DEST, PM_PATH, PM_IFACE, "Search");
    if (r >= 0) r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = append_sv_string(m, "id", keyword);
    char *auto_repos = NULL;
    if (r >= 0) {
        const char *repo_arg = repos;
        if (!repo_arg || !*repo_arg) {
            auto_repos = configured_repos_csv(ctx);
            repo_arg = auto_repos && *auto_repos ? auto_repos : "stable";
        }
        r = append_repos(m, repo_arg);
    }
    if (r >= 0) r = sd_bus_message_close_container(m);
    if (r >= 0) r = sd_bus_call(ctx->bus, m, CALL_TIMEOUT_USEC, &err, &reply);
    if (r < 0) {
        if (cb) cb(NULL, 0, r, err.message ? err.message : strerror(-r), userdata);
        free(auto_repos);
        sd_bus_error_free(&err);
        sd_bus_message_unref(m);
        sd_bus_message_unref(reply);
        return;
    }
    int64_t code = 0;
    char *message = NULL;
    char *job_id = NULL;
    r = parse_job_info(reply, &code, &message, &job_id);
    if (r < 0 || code != 0 || !job_id || !*job_id) {
        if (cb)
            cb(NULL, 0,
               r < 0 ? r : (code != 0 ? (int)code : -EINVAL),
               r < 0 ? strerror(-r) : (message ? message : "missing search job id"),
               userdata);
    } else {
        search_push(ctx, job_id, cb, userdata);
    }
    free(job_id); free(auto_repos); free(message);
    sd_bus_error_free(&err);
    sd_bus_message_unref(m);
    sd_bus_message_unref(reply);
}

/* ------------------------------------------------------------------ */
/* Install / Uninstall / Update (shared internal helper)               */
/* ------------------------------------------------------------------ */

static void call_task_method(LinyapsContext *ctx,
                             const char *method,
                             const char *app_id,
                             const char *version,
                             const char *channel,
                             LinyapsProgressCallback cb,
                             void *userdata)
{
    if (!ctx || !ctx->bus || !app_id || !*app_id) {
        notify_failed(cb, userdata, "invalid package request", -EINVAL);
        return;
    }
    pending_push(ctx, cb, userdata);
    sd_bus_message *m = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_message_new_method_call(ctx->bus, &m, PM_DEST, PM_PATH, PM_IFACE, method);
    if (r >= 0) r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) {
        int is_install = strcmp(method, "Install") == 0;
        int is_update  = strcmp(method, "Update")  == 0;
        r = append_package(m, app_id, version,
                           is_update ? NULL : (channel ? channel : "main"),
                           "binary", is_install);
    }
    if (r >= 0) {
        int is_install = strcmp(method, "Install") == 0;
        int is_update  = strcmp(method, "Update")  == 0;
        r = append_options(m, !is_update, 0, is_install);
    }
    if (r >= 0) r = sd_bus_message_close_container(m);
    if (r >= 0) r = sd_bus_call(ctx->bus, m, CALL_TIMEOUT_USEC, &err, &reply);
    if (r < 0) {
        pending_pop_tail(ctx);
        notify_failed(cb, userdata, err.message ? err.message : strerror(-r), r);
    } else {
        int64_t code = 0;
        char *message = NULL;
        int pr = parse_common_result(reply, &code, &message);
        if (pr < 0 || code != 0) {
            pending_pop_tail(ctx);
            notify_failed(cb, userdata, pr < 0 ? strerror(-pr) : message, pr < 0 ? pr : (int)code);
        }
        free(message);
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(m);
    sd_bus_message_unref(reply);
}

void linyaps_install(LinyapsContext *ctx,
                     const char *app_id,
                     const char *version,
                     const char *channel,
                     LinyapsProgressCallback cb,
                     void *userdata)
{
    call_task_method(ctx, "Install", app_id, version, channel, cb, userdata);
}

void linyaps_uninstall(LinyapsContext *ctx,
                       const char *app_id,
                       const char *version,
                       const char *channel,
                       LinyapsProgressCallback cb,
                       void *userdata)
{
    call_task_method(ctx, "Uninstall", app_id, version, channel, cb, userdata);
}

void linyaps_update(LinyapsContext *ctx,
                    const char *app_id,
                    LinyapsProgressCallback cb,
                    void *userdata)
{
    call_task_method(ctx, "Update", app_id, NULL, NULL, cb, userdata);
}

/* ------------------------------------------------------------------ */
/* Cancel / Interaction                                                  */
/* ------------------------------------------------------------------ */

void linyaps_cancel_task(LinyapsContext *ctx, const char *task_object_path)
{
    if (!ctx || !ctx->bus || !task_object_path) return;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    sd_bus_call_method(ctx->bus, PM_DEST, task_object_path, TASK_IFACE, "Cancel", &err, &reply, "");
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
}

void linyaps_reply_interaction(LinyapsContext *ctx, const char *task_object_path, bool accepted)
{
    if (!ctx || !ctx->bus || !task_object_path) return;
    sd_bus_message *m = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    const char *action = accepted ? "yes" : "no";
    int r = sd_bus_message_new_method_call(ctx->bus, &m, PM_DEST, task_object_path,
                                           TASK_IFACE, "ReplyInteraction");
    if (r >= 0) r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = append_sv_string(m, "action", action);
    if (r >= 0) r = sd_bus_message_close_container(m);
    if (r >= 0) sd_bus_call(ctx->bus, m, CALL_TIMEOUT_USEC, &err, &reply);
    sd_bus_error_free(&err);
    sd_bus_message_unref(m);
    sd_bus_message_unref(reply);
}

void linyaps_set_interaction_callback(LinyapsContext *ctx,
                                      LinyapsInteractionCallback cb,
                                      void *userdata)
{
    if (!ctx) return;
    ctx->interaction_cb = cb;
    ctx->interaction_userdata = userdata;
}

/* ------------------------------------------------------------------ */
/* Prune                                                                */
/* ------------------------------------------------------------------ */

void linyaps_prune(LinyapsContext *ctx, LinyapsCompletionCallback cb, void *userdata)
{
    if (!ctx || !ctx->bus) {
        if (cb) cb(-EINVAL, "invalid context", userdata);
        return;
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(ctx->bus, PM_DEST, PM_PATH, PM_IFACE, "Prune", &err, &reply, "");
    int64_t code = r;
    char *message = NULL;
    if (r >= 0) r = parse_common_result(reply, &code, &message);
    if (cb) cb(r < 0 ? r : (int)code, r < 0 ? strerror(-r) : (message ? message : ""), userdata);
    free(message);
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
}

/* ------------------------------------------------------------------ */
/* Package info helpers (public API)                                    */
/* ------------------------------------------------------------------ */

void linyaps_package_info_free(LinyapsPackageInfo *info)
{
    if (!info) return;
    free(info->id);
    free(info->name);
    free(info->version);
    free(info->arch);
    free(info->channel);
    free(info->repo);
    free(info->description);
    free(info->kind);
    free(info->module);
    free(info->base);
    free(info->runtime);
    free(info->schema_version);
    free(info->command);
    free(info);
}

void linyaps_package_info_list_free(LinyapsPackageInfo **list, size_t count)
{
    if (!list) return;
    for (size_t i = 0; i < count; i++) linyaps_package_info_free(list[i]);
    free(list);
}

const char *linyaps_task_state_string(LinyapsTaskState state)
{
    switch (state) {
    case LINYAPS_TASK_STATE_UNKNOWN:    return "Unknown";
    case LINYAPS_TASK_STATE_QUEUED:     return "Queued";
    case LINYAPS_TASK_STATE_PENDING:    return "Pending";
    case LINYAPS_TASK_STATE_PROCESSING: return "Processing";
    case LINYAPS_TASK_STATE_SUCCEED:    return "Succeed";
    case LINYAPS_TASK_STATE_FAILED:     return "Failed";
    case LINYAPS_TASK_STATE_CANCELED:   return "Canceled";
    default:                            return "Invalid";
    }
}
