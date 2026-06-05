/* SPDX-License-Identifier: LGPL-3.0-or-later */

#include "linyaps_private.h"

/* ------------------------------------------------------------------ */
/* PendingOp queue                                                     */
/* ------------------------------------------------------------------ */

void pending_push(LinyapsContext *ctx, LinyapsProgressCallback cb, void *userdata)
{
    PendingOp *op = calloc(1, sizeof(*op));
    if (!op) {
        return;
    }
    op->cb = cb;
    op->userdata = userdata;
    if (ctx->pending_tail) {
        ctx->pending_tail->next = op;
    } else {
        ctx->pending_head = op;
    }
    ctx->pending_tail = op;
}

int pending_pop(LinyapsContext *ctx, LinyapsProgressCallback *cb, void **userdata)
{
    PendingOp *op = ctx->pending_head;
    if (!op) {
        return -ENOENT;
    }
    ctx->pending_head = op->next;
    if (!ctx->pending_head) {
        ctx->pending_tail = NULL;
    }
    if (cb) {
        *cb = op->cb;
    }
    if (userdata) {
        *userdata = op->userdata;
    }
    free(op);
    return 0;
}

void pending_pop_tail(LinyapsContext *ctx)
{
    PendingOp *prev = NULL;
    PendingOp *it = ctx->pending_head;
    while (it && it->next) {
        prev = it;
        it = it->next;
    }
    if (!it) {
        return;
    }
    if (prev) {
        prev->next = NULL;
        ctx->pending_tail = prev;
    } else {
        ctx->pending_head = NULL;
        ctx->pending_tail = NULL;
    }
    free(it);
}

/* ------------------------------------------------------------------ */
/* SearchOp list                                                        */
/* ------------------------------------------------------------------ */

void search_push(LinyapsContext *ctx,
                 const char *job_id,
                 LinyapsSearchCallback cb,
                 void *userdata)
{
    SearchOp *op = calloc(1, sizeof(*op));
    if (!op) {
        if (cb) {
            cb(NULL, 0, -ENOMEM, "out of memory", userdata);
        }
        return;
    }
    op->job_id = dupstr(job_id);
    op->cb = cb;
    op->userdata = userdata;
    op->next = ctx->searches;
    ctx->searches = op;
}

SearchOp *search_take(LinyapsContext *ctx, const char *job_id)
{
    SearchOp *prev = NULL;
    SearchOp *it = ctx->searches;
    while (it) {
        if (it->job_id && strcmp(it->job_id, job_id) == 0) {
            if (prev) {
                prev->next = it->next;
            } else {
                ctx->searches = it->next;
            }
            it->next = NULL;
            return it;
        }
        prev = it;
        it = it->next;
    }
    return NULL;
}

void search_op_free(SearchOp *op)
{
    if (!op) {
        return;
    }
    free(op->job_id);
    free(op);
}

/* ------------------------------------------------------------------ */
/* TaskEntry management                                                 */
/* ------------------------------------------------------------------ */

TaskEntry *find_task(LinyapsContext *ctx, const char *path)
{
    for (size_t i = 0; i < MAX_TASKS; i++) {
        if (ctx->tasks[i].active && ctx->tasks[i].path &&
            strcmp(ctx->tasks[i].path, path) == 0) {
            return &ctx->tasks[i];
        }
    }
    return NULL;
}

TaskEntry *alloc_task(LinyapsContext *ctx)
{
    for (size_t i = 0; i < MAX_TASKS; i++) {
        if (!ctx->tasks[i].active) {
            return &ctx->tasks[i];
        }
    }
    return NULL;
}

void clear_task(TaskEntry *te)
{
    if (!te) {
        return;
    }
    te->slot = sd_bus_slot_unref(te->slot);
    te->interaction_slot = sd_bus_slot_unref(te->interaction_slot);
    free_string(&te->path);
    progress_clear(&te->progress);
    memset(te, 0, sizeof(*te));
}

int read_task_properties(LinyapsContext *ctx, TaskEntry *te)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int32_t state = LINYAPS_TASK_STATE_UNKNOWN;
    double percentage = te->progress.percentage;
    const char *message = NULL;
    int32_t code = te->progress.error_code;
    int r;

    r = sd_bus_get_property(ctx->bus, PM_DEST, te->path, TASK_IFACE, "State", &err, &reply, "i");
    if (r >= 0) {
        sd_bus_message_read(reply, "i", &state);
        te->progress.state = (LinyapsTaskState)state;
    }
    sd_bus_error_free(&err);
    reply = sd_bus_message_unref(reply);

    r = sd_bus_get_property(ctx->bus, PM_DEST, te->path, TASK_IFACE, "Percentage", &err, &reply, "d");
    if (r >= 0) {
        sd_bus_message_read(reply, "d", &percentage);
        te->progress.percentage = percentage;
    }
    sd_bus_error_free(&err);
    reply = sd_bus_message_unref(reply);

    r = sd_bus_get_property(ctx->bus, PM_DEST, te->path, TASK_IFACE, "Message", &err, &reply, "s");
    if (r >= 0) {
        sd_bus_message_read(reply, "s", &message);
        progress_set_string(&te->progress.message, message);
    }
    sd_bus_error_free(&err);
    reply = sd_bus_message_unref(reply);

    r = sd_bus_get_property(ctx->bus, PM_DEST, te->path, TASK_IFACE, "Code", &err, &reply, "i");
    if (r >= 0) {
        sd_bus_message_read(reply, "i", &code);
        te->progress.error_code = code;
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return 0;
}

/* ------------------------------------------------------------------ */
/* D-Bus signal: RequestInteraction                                     */
/* ------------------------------------------------------------------ */

static int read_additional_message(sd_bus_message *m, LinyapsInteractionRequest *request)
{
    int r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) {
        return r;
    }
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(m, 's', &key);
        sd_bus_message_peek_type(m, &type, &sig);
        if (strcmp(key, "localRef") == 0 && sig && strcmp(sig, "s") == 0) {
            read_variant_string(m, sig, &request->local_ref);
        } else if (strcmp(key, "remoteRef") == 0 && sig && strcmp(sig, "s") == 0) {
            read_variant_string(m, sig, &request->remote_ref);
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    if (r < 0) {
        return r;
    }
    return sd_bus_message_exit_container(m);
}

static int on_request_interaction(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    TaskEntry *te = userdata;
    if (!te || !te->active) {
        return 0;
    }

    LinyapsContext *ctx = te->ctx;

    int32_t message_id = 0;
    int r = sd_bus_message_read_basic(m, 'i', &message_id);
    if (r < 0) {
        return 0;
    }

    LinyapsInteractionRequest request = { 0 };
    request.object_path = dupstr(te->path);
    request.message_id = message_id;
    r = read_additional_message(m, &request);
    if (r < 0) {
        linyaps_interaction_request_clear(&request);
        return 0;
    }

    request.summary = dupstr("Package Manager needs confirmation.");
    if (message_id == 1 && request.local_ref && request.remote_ref) {
        request.body = dupstr("The lower version is installed. Continue installing the newer version?");
    } else {
        request.body = dupstr("Confirm package manager operation.");
    }

    if (ctx && ctx->interaction_cb) {
        ctx->interaction_cb(&request, ctx->interaction_userdata);
    } else {
        linyaps_reply_interaction(ctx, te->path, false);
    }
    linyaps_interaction_request_clear(&request);
    return 0;
}

/* ------------------------------------------------------------------ */
/* D-Bus signal: PropertiesChanged                                      */
/* ------------------------------------------------------------------ */

static int on_properties_changed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    TaskEntry *te = userdata;
    const char *iface = NULL;
    int r = sd_bus_message_read_basic(m, 's', &iface);
    if (r < 0 || !iface || strcmp(iface, TASK_IFACE) != 0) {
        return 0;
    }
    r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) {
        return 0;
    }
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(m, 's', &key);
        sd_bus_message_peek_type(m, &type, &sig);
        if (strcmp(key, "State") == 0 && sig && strcmp(sig, "i") == 0) {
            int32_t value = 0;
            sd_bus_message_enter_container(m, 'v', "i");
            sd_bus_message_read_basic(m, 'i', &value);
            te->progress.state = (LinyapsTaskState)value;
            sd_bus_message_exit_container(m);
        } else if (strcmp(key, "Percentage") == 0 && sig && strcmp(sig, "d") == 0) {
            double value = 0.0;
            sd_bus_message_enter_container(m, 'v', "d");
            sd_bus_message_read_basic(m, 'd', &value);
            te->progress.percentage = value;
            sd_bus_message_exit_container(m);
        } else if (strcmp(key, "Message") == 0 && sig && strcmp(sig, "s") == 0) {
            const char *value = NULL;
            sd_bus_message_enter_container(m, 'v', "s");
            sd_bus_message_read_basic(m, 's', &value);
            progress_set_string(&te->progress.message, value);
            sd_bus_message_exit_container(m);
        } else if (strcmp(key, "Code") == 0 && sig && strcmp(sig, "i") == 0) {
            int32_t value = 0;
            sd_bus_message_enter_container(m, 'v', "i");
            sd_bus_message_read_basic(m, 'i', &value);
            te->progress.error_code = value;
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    sd_bus_message_skip(m, "as");
    if (te->cb) {
        te->cb(&te->progress, te->userdata);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* D-Bus signal: TaskAdded                                             */
/* ------------------------------------------------------------------ */

int on_task_added(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    LinyapsContext *ctx = userdata;
    const char *path = NULL;
    LinyapsProgressCallback cb = NULL;
    void *cb_userdata = NULL;
    int r = sd_bus_message_read(m, "o", &path);
    if (r < 0 || !path) {
        return 0;
    }
    if (pending_pop(ctx, &cb, &cb_userdata) < 0) {
        return 0;
    }
    TaskEntry *te = alloc_task(ctx);
    if (!te) {
        notify_failed(cb, cb_userdata, "too many active tasks", -ENOSPC);
        return 0;
    }
    memset(te, 0, sizeof(*te));
    te->active = 1;
    te->ctx = ctx;
    te->path = dupstr(path);
    te->cb = cb;
    te->userdata = cb_userdata;
    te->progress.object_path = dupstr(path);
    te->progress.state = LINYAPS_TASK_STATE_UNKNOWN;

    char match[512];
    snprintf(match,
             sizeof(match),
             "type='signal',interface='%s',member='PropertiesChanged',path='%s'",
             PROPS_IFACE,
             path);
    r = sd_bus_add_match(ctx->bus, &te->slot, match, on_properties_changed, te);
    if (r < 0) {
        notify_failed(cb, cb_userdata, "failed to subscribe task progress", r);
        clear_task(te);
        return 0;
    }
    snprintf(match,
             sizeof(match),
             "type='signal',interface='%s',member='RequestInteraction',path='%s'",
             TASK_IFACE,
             path);
    r = sd_bus_add_match(ctx->bus, &te->interaction_slot, match, on_request_interaction, te);
    if (r < 0) {
        notify_failed(cb, cb_userdata, "failed to subscribe task interaction", r);
        clear_task(te);
        return 0;
    }
    read_task_properties(ctx, te);
    if (te->cb) {
        te->cb(&te->progress, te->userdata);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* D-Bus signal: TaskRemoved                                           */
/* ------------------------------------------------------------------ */

int on_task_removed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    LinyapsContext *ctx = userdata;
    const char *path = NULL;
    if (sd_bus_message_read(m, "o", &path) < 0 || !path) {
        return 0;
    }
    TaskEntry *te = find_task(ctx, path);
    if (!te) {
        return 0;
    }
    read_task_properties(ctx, te);
    if (te->cb) {
        te->cb(&te->progress, te->userdata);
    }
    clear_task(te);
    return 0;
}

/* ------------------------------------------------------------------ */
/* D-Bus signal: SearchFinished                                         */
/* ------------------------------------------------------------------ */

int on_search_finished(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    LinyapsContext *ctx = userdata;
    const char *job_id = NULL;
    int r = sd_bus_message_read_basic(m, 's', &job_id);
    if (r < 0 || !job_id) {
        return 0;
    }
    SearchOp *op = search_take(ctx, job_id);
    if (!op) {
        sd_bus_message_skip(m, "a{sv}");
        return 0;
    }

    LinyapsPackageInfo **items = NULL;
    size_t count = 0;
    int64_t code = 0;
    char *message = NULL;
    r = parse_search_reply(m, &items, &count, &code, &message);
    if (r >= 0 && code == 0) {
        filter_store_search_results(&items, &count);
    }
    if (op->cb) {
        op->cb(r < 0 || code != 0 ? NULL : items,
               r < 0 || code != 0 ? 0 : count,
               r < 0 ? r : (int)code,
               r < 0 ? strerror(-r) : message,
               op->userdata);
    }
    if (r < 0 || code != 0 || !op->cb) {
        linyaps_package_info_list_free(items, count);
    }
    free(message);
    search_op_free(op);
    return 0;
}
