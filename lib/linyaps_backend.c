/* SPDX-License-Identifier: LGPL-3.0-or-later */

#define _POSIX_C_SOURCE 200809L

#include "linyaps_backend.h"

#include <cJSON.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#define PM_DEST "org.deepin.linglong.PackageManager1"
#define PM_PATH "/org/deepin/linglong/PackageManager1"
#define PM_IFACE "org.deepin.linglong.PackageManager1"
#define TASK_IFACE "org.deepin.linglong.Task1"
#define PROPS_IFACE "org.freedesktop.DBus.Properties"
#define CALL_TIMEOUT_USEC (120ULL * 1000000ULL)
#define MAX_TASKS 16

typedef struct PendingOp {
    LinyapsProgressCallback cb;
    void *userdata;
    struct PendingOp *next;
} PendingOp;

typedef struct SearchOp {
    char *job_id;
    LinyapsSearchCallback cb;
    void *userdata;
    struct SearchOp *next;
} SearchOp;

struct LinyapsContext;

typedef struct {
    char *path;
    struct LinyapsContext *ctx;
    LinyapsProgressCallback cb;
    void *userdata;
    sd_bus_slot *slot;
    sd_bus_slot *interaction_slot;
    LinyapsTaskProgress progress;
    int active;
} TaskEntry;

struct LinyapsContext {
    sd_bus *bus;
    sd_bus_slot *task_added_slot;
    sd_bus_slot *task_removed_slot;
    sd_bus_slot *search_finished_slot;
    LinyapsInteractionCallback interaction_cb;
    void *interaction_userdata;
    PendingOp *pending_head;
    PendingOp *pending_tail;
    SearchOp *searches;
    TaskEntry tasks[MAX_TASKS];
};

static int parse_search_reply(sd_bus_message *reply,
                              LinyapsPackageInfo ***out_items,
                              size_t *out_count,
                              int64_t *out_code,
                              char **out_message);
static int read_variant_string(sd_bus_message *m, const char *sig, char **out);
static void filter_store_search_results(LinyapsPackageInfo ***items, size_t *count);
static char *configured_repos_csv(LinyapsContext *ctx);

static void linyaps_interaction_request_clear(LinyapsInteractionRequest *request)
{
    free(request->object_path);
    free(request->local_ref);
    free(request->remote_ref);
    free(request->summary);
    free(request->body);
    memset(request, 0, sizeof(*request));
}

static void free_string(char **s)
{
    if (*s) {
        free(*s);
        *s = NULL;
    }
}

static char *dupstr(const char *s)
{
    return s ? strdup(s) : NULL;
}

static void progress_clear(LinyapsTaskProgress *p)
{
    free_string(&p->object_path);
    free_string(&p->message);
    memset(p, 0, sizeof(*p));
}

static void progress_set_string(char **dst, const char *src)
{
    free_string(dst);
    *dst = dupstr(src);
}

static void package_list_append(LinyapsPackageInfo ***items,
                                size_t *count,
                                size_t *cap,
                                LinyapsPackageInfo *info)
{
    if (*count == *cap) {
        size_t next = *cap ? *cap * 2 : 16;
        LinyapsPackageInfo **tmp = realloc(*items, next * sizeof(*tmp));
        if (!tmp) {
            linyaps_package_info_free(info);
            return;
        }
        *items = tmp;
        *cap = next;
    }
    (*items)[(*count)++] = info;
}

static void pending_push(LinyapsContext *ctx,
                         LinyapsProgressCallback cb,
                         void *userdata)
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

static int pending_pop(LinyapsContext *ctx,
                       LinyapsProgressCallback *cb,
                       void **userdata)
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

static void pending_pop_tail(LinyapsContext *ctx)
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

static void search_push(LinyapsContext *ctx,
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

static SearchOp *search_take(LinyapsContext *ctx, const char *job_id)
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

static void search_op_free(SearchOp *op)
{
    if (!op) {
        return;
    }
    free(op->job_id);
    free(op);
}

static void notify_failed(LinyapsProgressCallback cb,
                          void *userdata,
                          const char *message,
                          int code)
{
    if (!cb) {
        return;
    }
    LinyapsTaskProgress p = { 0 };
    p.state = LINYAPS_TASK_STATE_FAILED;
    p.percentage = 0.0;
    p.message = (char *)(message ? message : "operation failed");
    p.error_code = code ? code : -1;
    cb(&p, userdata);
}

static int append_sv_string(sd_bus_message *m, const char *key, const char *value)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_append_basic(m, 's', key);
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_open_container(m, 'v', "s");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_append_basic(m, 's', value ? value : "");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
    return sd_bus_message_close_container(m);
}

static int append_sv_bool(sd_bus_message *m, const char *key, int value)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_append_basic(m, 's', key);
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_open_container(m, 'v', "b");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_append_basic(m, 'b', &value);
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
    return sd_bus_message_close_container(m);
}

static int append_repos(sd_bus_message *m, const char *repos)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_append_basic(m, 's', "repos");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_open_container(m, 'v', "as");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_open_container(m, 'a', "s");
    if (r < 0) {
        return r;
    }
    if (repos && *repos) {
        char *copy = dupstr(repos);
        char *save = NULL;
        for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            while (*tok == ' ' || *tok == '\t') {
                tok++;
            }
            if (*tok) {
                r = sd_bus_message_append_basic(m, 's', tok);
                if (r < 0) {
                    free(copy);
                    return r;
                }
            }
        }
        free(copy);
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
    return sd_bus_message_close_container(m);
}

static int append_package(sd_bus_message *m,
                          const char *id,
                          const char *version,
                          const char *channel,
                          const char *module,
                          int include_module)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_append_basic(m, 's', "package");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_open_container(m, 'v', "a{sv}");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r < 0) {
        return r;
    }
    if ((r = append_sv_string(m, "id", id)) < 0) {
        return r;
    }
    if (version && *version && (r = append_sv_string(m, "version", version)) < 0) {
        return r;
    }
    if (channel && *channel && (r = append_sv_string(m, "channel", channel)) < 0) {
        return r;
    }
    if (include_module && module && *module &&
        (r = append_sv_string(m, "packageInfoV2Module", module)) < 0) {
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
    return sd_bus_message_close_container(m);
}

static int append_options(sd_bus_message *m, int include_force, int force, int include_skip)
{
    int r = sd_bus_message_open_container(m, 'e', "sv");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_append_basic(m, 's', "options");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_open_container(m, 'v', "a{sv}");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r < 0) {
        return r;
    }
    if (include_force && (r = append_sv_bool(m, "force", force)) < 0) {
        return r;
    }
    if (include_skip && (r = append_sv_bool(m, "skipInteraction", 0)) < 0) {
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_close_container(m);
    if (r < 0) {
        return r;
    }
    return sd_bus_message_close_container(m);
}

static int parse_common_result(sd_bus_message *reply, int64_t *code, char **message)
{
    int r;
    if (code) {
        *code = 0;
    }
    if (message) {
        *message = NULL;
    }
    r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if (r < 0) {
        return r;
    }
    while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        r = sd_bus_message_read_basic(reply, 's', &key);
        if (r < 0) {
            return r;
        }
        r = sd_bus_message_peek_type(reply, &type, &sig);
        if (r < 0) {
            return r;
        }
        if (strcmp(key, "code") == 0 && sig && strcmp(sig, "x") == 0) {
            int64_t value = 0;
            if ((r = sd_bus_message_enter_container(reply, 'v', "x")) < 0) {
                return r;
            }
            if ((r = sd_bus_message_read_basic(reply, 'x', &value)) < 0) {
                return r;
            }
            if (code) {
                *code = value;
            }
            if ((r = sd_bus_message_exit_container(reply)) < 0) {
                return r;
            }
        } else if (strcmp(key, "message") == 0 && sig && strcmp(sig, "s") == 0) {
            const char *value = NULL;
            if ((r = sd_bus_message_enter_container(reply, 'v', "s")) < 0) {
                return r;
            }
            if ((r = sd_bus_message_read_basic(reply, 's', &value)) < 0) {
                return r;
            }
            if (message) {
                *message = dupstr(value);
            }
            if ((r = sd_bus_message_exit_container(reply)) < 0) {
                return r;
            }
        } else {
            if ((r = sd_bus_message_skip(reply, "v")) < 0) {
                return r;
            }
        }
        if ((r = sd_bus_message_exit_container(reply)) < 0) {
            return r;
        }
    }
    if (r < 0) {
        return r;
    }
    return sd_bus_message_exit_container(reply);
}

static int parse_job_info(sd_bus_message *reply, int64_t *code, char **message, char **job_id)
{
    int r;
    if (code) {
        *code = 0;
    }
    if (message) {
        *message = NULL;
    }
    if (job_id) {
        *job_id = NULL;
    }
    r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if (r < 0) {
        return r;
    }
    while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        if ((r = sd_bus_message_read_basic(reply, 's', &key)) < 0) {
            return r;
        }
        if ((r = sd_bus_message_peek_type(reply, &type, &sig)) < 0) {
            return r;
        }
        if (strcmp(key, "id") == 0 && sig && strcmp(sig, "s") == 0) {
            const char *value = NULL;
            if ((r = sd_bus_message_enter_container(reply, 'v', "s")) < 0) {
                return r;
            }
            if ((r = sd_bus_message_read_basic(reply, 's', &value)) < 0) {
                return r;
            }
            if (job_id) {
                *job_id = dupstr(value);
            }
            if ((r = sd_bus_message_exit_container(reply)) < 0) {
                return r;
            }
        } else if (strcmp(key, "code") == 0 && sig && strcmp(sig, "x") == 0) {
            int64_t value = 0;
            if ((r = sd_bus_message_enter_container(reply, 'v', "x")) < 0) {
                return r;
            }
            if ((r = sd_bus_message_read_basic(reply, 'x', &value)) < 0) {
                return r;
            }
            if (code) {
                *code = value;
            }
            if ((r = sd_bus_message_exit_container(reply)) < 0) {
                return r;
            }
        } else if (strcmp(key, "message") == 0 && sig && strcmp(sig, "s") == 0) {
            const char *value = NULL;
            if ((r = sd_bus_message_enter_container(reply, 'v', "s")) < 0) {
                return r;
            }
            if ((r = sd_bus_message_read_basic(reply, 's', &value)) < 0) {
                return r;
            }
            if (message) {
                *message = dupstr(value);
            }
            if ((r = sd_bus_message_exit_container(reply)) < 0) {
                return r;
            }
        } else {
            if ((r = sd_bus_message_skip(reply, "v")) < 0) {
                return r;
            }
        }
        if ((r = sd_bus_message_exit_container(reply)) < 0) {
            return r;
        }
    }
    if (r < 0) {
        return r;
    }
    return sd_bus_message_exit_container(reply);
}

static int read_task_properties(LinyapsContext *ctx, TaskEntry *te)
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

static TaskEntry *find_task(LinyapsContext *ctx, const char *path)
{
    for (size_t i = 0; i < MAX_TASKS; i++) {
        if (ctx->tasks[i].active && ctx->tasks[i].path && strcmp(ctx->tasks[i].path, path) == 0) {
            return &ctx->tasks[i];
        }
    }
    return NULL;
}

static TaskEntry *alloc_task(LinyapsContext *ctx)
{
    for (size_t i = 0; i < MAX_TASKS; i++) {
        if (!ctx->tasks[i].active) {
            return &ctx->tasks[i];
        }
    }
    return NULL;
}

static void clear_task(TaskEntry *te)
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
        const char *prefix = "The lower version is installed. Continue installing the newer version?";
        request.body = dupstr(prefix);
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

static int on_task_added(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
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

static int on_task_removed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
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

static int on_search_finished(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
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

LinyapsContext *linyaps_context_new(void)
{
    LinyapsContext *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }
    int r = sd_bus_open_system(&ctx->bus);
    if (r < 0) {
        free(ctx);
        return NULL;
    }
    r = sd_bus_add_match(ctx->bus,
                         &ctx->task_added_slot,
                         "type='signal',sender='" PM_DEST "',interface='" PM_IFACE "',member='TaskAdded'",
                         on_task_added,
                         ctx);
    if (r < 0) {
        linyaps_context_free(ctx);
        return NULL;
    }
    r = sd_bus_add_match(ctx->bus,
                         &ctx->task_removed_slot,
                         "type='signal',sender='" PM_DEST "',interface='" PM_IFACE "',member='TaskRemoved'",
                         on_task_removed,
                         ctx);
    if (r < 0) {
        linyaps_context_free(ctx);
        return NULL;
    }
    r = sd_bus_add_match(ctx->bus,
                         &ctx->search_finished_slot,
                         "type='signal',sender='" PM_DEST "',interface='" PM_IFACE "',member='SearchFinished'",
                         on_search_finished,
                         ctx);
    if (r < 0) {
        linyaps_context_free(ctx);
        return NULL;
    }
    return ctx;
}

void linyaps_context_free(LinyapsContext *ctx)
{
    if (!ctx) {
        return;
    }
    while (ctx->pending_head) {
        pending_pop(ctx, NULL, NULL);
    }
    while (ctx->searches) {
        SearchOp *next = ctx->searches->next;
        search_op_free(ctx->searches);
        ctx->searches = next;
    }
    for (size_t i = 0; i < MAX_TASKS; i++) {
        clear_task(&ctx->tasks[i]);
    }
    ctx->task_added_slot = sd_bus_slot_unref(ctx->task_added_slot);
    ctx->task_removed_slot = sd_bus_slot_unref(ctx->task_removed_slot);
    ctx->search_finished_slot = sd_bus_slot_unref(ctx->search_finished_slot);
    ctx->bus = sd_bus_unref(ctx->bus);
    free(ctx);
}

int linyaps_process(LinyapsContext *ctx)
{
    if (!ctx || !ctx->bus) {
        return -EINVAL;
    }
    int processed = 0;
    for (;;) {
        int r = sd_bus_process(ctx->bus, NULL);
        if (r < 0) {
            return r;
        }
        if (r == 0) {
            break;
        }
        processed += r;
    }
    return processed;
}

bool linyaps_is_service_available(LinyapsContext *ctx)
{
    if (!ctx || !ctx->bus) {
        return false;
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(ctx->bus,
                               PM_DEST,
                               PM_PATH,
                               "org.freedesktop.DBus.Peer",
                               "Ping",
                               &err,
                               &reply,
                               "");
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return r >= 0;
}

static LinyapsPackageInfo *package_info_new(void)
{
    return calloc(1, sizeof(LinyapsPackageInfo));
}

static int read_variant_string(sd_bus_message *m, const char *sig, char **out)
{
    if (!sig || strcmp(sig, "s") != 0) {
        return sd_bus_message_skip(m, "v");
    }
    const char *value = NULL;
    int r = sd_bus_message_enter_container(m, 'v', "s");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_read_basic(m, 's', &value);
    if (r >= 0) {
        *out = dupstr(value);
    }
    if (r < 0) {
        return r;
    }
    return sd_bus_message_exit_container(m);
}

static int read_variant_int64(sd_bus_message *m, const char *sig, int64_t *out)
{
    if (!sig || strcmp(sig, "x") != 0) {
        return sd_bus_message_skip(m, "v");
    }
    int64_t value = 0;
    int r = sd_bus_message_enter_container(m, 'v', "x");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_read_basic(m, 'x', &value);
    if (r >= 0) {
        *out = value;
    }
    if (r < 0) {
        return r;
    }
    return sd_bus_message_exit_container(m);
}

static int read_variant_first_string_array(sd_bus_message *m, const char *sig, char **out)
{
    if (!sig || strcmp(sig, "as") != 0) {
        return sd_bus_message_skip(m, "v");
    }
    int r = sd_bus_message_enter_container(m, 'v', "as");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_enter_container(m, 'a', "s");
    if (r < 0) {
        return r;
    }
    const char *value = NULL;
    r = sd_bus_message_read_basic(m, 's', &value);
    if (r > 0 && value) {
        *out = dupstr(value);
        while (sd_bus_message_read_basic(m, 's', &value) > 0) {
        }
    }
    sd_bus_message_exit_container(m);
    return sd_bus_message_exit_container(m);
}

static int parse_package_map(sd_bus_message *m,
                             const char *repo,
                             LinyapsPackageInfo **out_info)
{
    int r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) {
        return r;
    }
    LinyapsPackageInfo *info = package_info_new();
    if (!info) {
        return -ENOMEM;
    }
    info->repo = dupstr(repo);
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(m, 's', &key);
        sd_bus_message_peek_type(m, &type, &sig);
        if (strcmp(key, "id") == 0) {
            r = read_variant_string(m, sig, &info->id);
        } else if (strcmp(key, "name") == 0) {
            r = read_variant_string(m, sig, &info->name);
        } else if (strcmp(key, "version") == 0) {
            r = read_variant_string(m, sig, &info->version);
        } else if (strcmp(key, "channel") == 0) {
            r = read_variant_string(m, sig, &info->channel);
        } else if (strcmp(key, "kind") == 0) {
            r = read_variant_string(m, sig, &info->kind);
        } else if (strcmp(key, "packageInfoV2Module") == 0) {
            r = read_variant_string(m, sig, &info->module);
        } else if (strcmp(key, "description") == 0) {
            r = read_variant_string(m, sig, &info->description);
        } else if (strcmp(key, "arch") == 0) {
            r = read_variant_first_string_array(m, sig, &info->arch);
        } else if (strcmp(key, "size") == 0) {
            r = read_variant_int64(m, sig, &info->size);
        } else {
            r = sd_bus_message_skip(m, "v");
        }
        if (r < 0) {
            linyaps_package_info_free(info);
            return r;
        }
        if ((r = sd_bus_message_exit_container(m)) < 0) {
            linyaps_package_info_free(info);
            return r;
        }
    }
    if (r < 0) {
        linyaps_package_info_free(info);
        return r;
    }
    r = sd_bus_message_exit_container(m);
    if (r < 0) {
        linyaps_package_info_free(info);
        return r;
    }
    *out_info = info;
    return 0;
}

static int parse_search_reply(sd_bus_message *reply,
                              LinyapsPackageInfo ***out_items,
                              size_t *out_count,
                              int64_t *out_code,
                              char **out_message)
{
    int r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if (r < 0) {
        return r;
    }
    LinyapsPackageInfo **items = NULL;
    size_t count = 0;
    size_t cap = 0;
    int64_t code = 0;
    char *message = NULL;
    while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(reply, 's', &key);
        sd_bus_message_peek_type(reply, &type, &sig);
        if (strcmp(key, "code") == 0) {
            r = read_variant_int64(reply, sig, &code);
        } else if (strcmp(key, "message") == 0) {
            r = read_variant_string(reply, sig, &message);
        } else if (strcmp(key, "packages") == 0 && sig && strcmp(sig, "a{sv}") == 0) {
            r = sd_bus_message_enter_container(reply, 'v', "a{sv}");
            if (r >= 0) {
                r = sd_bus_message_enter_container(reply, 'a', "{sv}");
            }
            while (r >= 0 && (r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
                const char *repo = NULL;
                sd_bus_message_read_basic(reply, 's', &repo);
                r = sd_bus_message_enter_container(reply, 'v', "av");
                if (r >= 0) {
                    r = sd_bus_message_enter_container(reply, 'a', "v");
                }
                while (r >= 0 && (r = sd_bus_message_enter_container(reply, 'v', "a{sv}")) > 0) {
                    LinyapsPackageInfo *info = NULL;
                    r = parse_package_map(reply, repo, &info);
                    if (r >= 0 && info) {
                        package_list_append(&items, &count, &cap, info);
                    }
                    if (r >= 0) {
                        r = sd_bus_message_exit_container(reply);
                    }
                }
                if (r == 0) {
                    r = sd_bus_message_exit_container(reply);
                }
                if (r >= 0) {
                    r = sd_bus_message_exit_container(reply);
                }
                if (r >= 0) {
                    r = sd_bus_message_exit_container(reply);
                }
            }
            if (r == 0) {
                r = sd_bus_message_exit_container(reply);
            }
            if (r >= 0) {
                r = sd_bus_message_exit_container(reply);
            }
        } else {
            r = sd_bus_message_skip(reply, "v");
        }
        if (r < 0) {
            linyaps_package_info_list_free(items, count);
            free(message);
            return r;
        }
        if ((r = sd_bus_message_exit_container(reply)) < 0) {
            linyaps_package_info_list_free(items, count);
            free(message);
            return r;
        }
    }
    if (r < 0) {
        linyaps_package_info_list_free(items, count);
        free(message);
        return r;
    }
    r = sd_bus_message_exit_container(reply);
    if (r < 0) {
        linyaps_package_info_list_free(items, count);
        free(message);
        return r;
    }
    *out_items = items;
    *out_count = count;
    *out_code = code;
    *out_message = message;
    return 0;
}

static int read_version_number(const char **p, long long *value)
{
    while (**p && (**p < '0' || **p > '9')) {
        (*p)++;
    }
    if (!**p) {
        return 0;
    }
    char *end = NULL;
    *value = strtoll(*p, &end, 10);
    *p = end;
    return 1;
}

static int compare_versions(const char *a, const char *b)
{
    if (!a && !b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }

    const char *pa = a;
    const char *pb = b;
    for (;;) {
        long long va = 0;
        long long vb = 0;
        int ha = read_version_number(&pa, &va);
        int hb = read_version_number(&pb, &vb);
        if (!ha || !hb) {
            break;
        }
        if (va < vb) {
            return -1;
        }
        if (va > vb) {
            return 1;
        }
    }
    return strcmp(a, b);
}

static bool same_package_slot(const LinyapsPackageInfo *a, const LinyapsPackageInfo *b)
{
    const char *ar = a->repo ? a->repo : "";
    const char *br = b->repo ? b->repo : "";
    const char *ai = a->id ? a->id : "";
    const char *bi = b->id ? b->id : "";
    const char *am = a->module ? a->module : "";
    const char *bm = b->module ? b->module : "";
    return strcmp(ar, br) == 0 && strcmp(ai, bi) == 0 && strcmp(am, bm) == 0;
}

static void filter_store_search_results(LinyapsPackageInfo ***items, size_t *count)
{
    if (!items || !*items || !count) {
        return;
    }

    LinyapsPackageInfo **list = *items;
    size_t out = 0;
    for (size_t i = 0; i < *count; i++) {
        LinyapsPackageInfo *candidate = list[i];
        if (!candidate) {
            continue;
        }
        if (candidate->module && strcmp(candidate->module, "develop") == 0) {
            linyaps_package_info_free(candidate);
            continue;
        }

        size_t found = out;
        for (size_t j = 0; j < out; j++) {
            if (same_package_slot(list[j], candidate)) {
                found = j;
                break;
            }
        }
        if (found == out) {
            list[out++] = candidate;
            continue;
        }

        if (compare_versions(list[found]->version, candidate->version) < 0) {
            linyaps_package_info_free(list[found]);
            list[found] = candidate;
        } else {
            linyaps_package_info_free(candidate);
        }
    }
    *count = out;
}

static int csv_append(char **csv, const char *repo)
{
    if (!repo || !*repo) {
        return 0;
    }
    size_t old_len = *csv ? strlen(*csv) : 0;
    size_t add_len = strlen(repo);
    char *next = realloc(*csv, old_len + add_len + (old_len ? 2 : 1));
    if (!next) {
        return -ENOMEM;
    }
    if (old_len) {
        next[old_len++] = ',';
    }
    memcpy(next + old_len, repo, add_len + 1);
    *csv = next;
    return 0;
}

static int parse_repo_variant(sd_bus_message *m, char **csv)
{
    int r = sd_bus_message_enter_container(m, 'v', "a{sv}");
    if (r < 0) {
        return r;
    }
    r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) {
        return r;
    }
    char *name = NULL;
    char *alias = NULL;
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(m, 's', &key);
        sd_bus_message_peek_type(m, &type, &sig);
        if (strcmp(key, "alias") == 0) {
            r = read_variant_string(m, sig, &alias);
        } else if (strcmp(key, "name") == 0) {
            r = read_variant_string(m, sig, &name);
        } else {
            r = sd_bus_message_skip(m, "v");
        }
        if (r < 0) {
            free(name);
            free(alias);
            return r;
        }
        if ((r = sd_bus_message_exit_container(m)) < 0) {
            free(name);
            free(alias);
            return r;
        }
    }
    if (r < 0) {
        free(name);
        free(alias);
        return r;
    }
    r = sd_bus_message_exit_container(m);
    if (r >= 0) {
        r = sd_bus_message_exit_container(m);
    }
    if (r >= 0) {
        r = csv_append(csv, alias ? alias : name);
    }
    free(name);
    free(alias);
    return r;
}

static char *configured_repos_csv(LinyapsContext *ctx)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    char *csv = NULL;
    int r = sd_bus_get_property(ctx->bus,
                                PM_DEST,
                                PM_PATH,
                                PM_IFACE,
                                "Configuration",
                                &err,
                                &reply,
                                "a{sv}");
    if (r < 0) {
        sd_bus_error_free(&err);
        sd_bus_message_unref(reply);
        return NULL;
    }

    r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    while (r >= 0 && (r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        char type = 0;
        const char *sig = NULL;
        sd_bus_message_read_basic(reply, 's', &key);
        sd_bus_message_peek_type(reply, &type, &sig);
        if (strcmp(key, "repos") == 0 && sig && strcmp(sig, "av") == 0) {
            r = sd_bus_message_enter_container(reply, 'v', "av");
            if (r >= 0) {
                r = sd_bus_message_enter_container(reply, 'a', "v");
            }
            while (r >= 0 && (r = sd_bus_message_peek_type(reply, &type, &sig)) > 0) {
                if (type != 'v') {
                    break;
                }
                r = parse_repo_variant(reply, &csv);
                if (r < 0) {
                    break;
                }
            }
            if (r == 0) {
                r = sd_bus_message_exit_container(reply);
            }
            if (r >= 0) {
                r = sd_bus_message_exit_container(reply);
            }
        } else {
            r = sd_bus_message_skip(reply, "v");
        }
        if (r < 0) {
            break;
        }
        r = sd_bus_message_exit_container(reply);
    }
    if (r >= 0) {
        sd_bus_message_exit_container(reply);
    }
    if (r < 0) {
        free(csv);
        csv = NULL;
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return csv;
}

void linyaps_search(LinyapsContext *ctx,
                    const char *keyword,
                    const char *repos,
                    LinyapsSearchCallback cb,
                    void *userdata)
{
    if (!ctx || !ctx->bus || !keyword || !*keyword) {
        if (cb) {
            cb(NULL, 0, -EINVAL, "invalid search request", userdata);
        }
        return;
    }
    sd_bus_message *m = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_message_new_method_call(ctx->bus, &m, PM_DEST, PM_PATH, PM_IFACE, "Search");
    if (r >= 0) {
        r = sd_bus_message_open_container(m, 'a', "{sv}");
    }
    if (r >= 0) {
        r = append_sv_string(m, "id", keyword);
    }
    char *auto_repos = NULL;
    if (r >= 0) {
        const char *repo_arg = repos;
        if (!repo_arg || !*repo_arg) {
            auto_repos = configured_repos_csv(ctx);
            repo_arg = auto_repos && *auto_repos ? auto_repos : "stable";
        }
        r = append_repos(m, repo_arg);
    }
    if (r >= 0) {
        r = sd_bus_message_close_container(m);
    }
    if (r >= 0) {
        r = sd_bus_call(ctx->bus, m, CALL_TIMEOUT_USEC, &err, &reply);
    }
    if (r < 0) {
        if (cb) {
            cb(NULL, 0, r, err.message ? err.message : strerror(-r), userdata);
        }
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
        if (cb) {
            cb(NULL,
               0,
               r < 0 ? r : (code != 0 ? (int)code : -EINVAL),
               r < 0 ? strerror(-r) : (message ? message : "missing search job id"),
               userdata);
        }
    } else {
        search_push(ctx, job_id, cb, userdata);
    }
    free(job_id);
    free(auto_repos);
    free(message);
    sd_bus_error_free(&err);
    sd_bus_message_unref(m);
    sd_bus_message_unref(reply);
}

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
    if (r >= 0) {
        r = sd_bus_message_open_container(m, 'a', "{sv}");
    }
    if (r >= 0) {
        int is_install = strcmp(method, "Install") == 0;
        int is_update = strcmp(method, "Update") == 0;
        r = append_package(m,
                           app_id,
                           version,
                           is_update ? NULL : (channel ? channel : "main"),
                           "binary",
                           is_install);
    }
    if (r >= 0) {
        int is_install = strcmp(method, "Install") == 0;
        int is_update = strcmp(method, "Update") == 0;
        r = append_options(m, !is_update, 0, is_install);
    }
    if (r >= 0) {
        r = sd_bus_message_close_container(m);
    }
    if (r >= 0) {
        r = sd_bus_call(ctx->bus, m, CALL_TIMEOUT_USEC, &err, &reply);
    }
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

void linyaps_cancel_task(LinyapsContext *ctx, const char *task_object_path)
{
    if (!ctx || !ctx->bus || !task_object_path) {
        return;
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    sd_bus_call_method(ctx->bus, PM_DEST, task_object_path, TASK_IFACE, "Cancel", &err, &reply, "");
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
}

void linyaps_reply_interaction(LinyapsContext *ctx, const char *task_object_path, bool accepted)
{
    if (!ctx || !ctx->bus || !task_object_path) {
        return;
    }
    sd_bus_message *m = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    const char *action = accepted ? "yes" : "no";
    int r = sd_bus_message_new_method_call(ctx->bus,
                                           &m,
                                           PM_DEST,
                                           task_object_path,
                                           TASK_IFACE,
                                           "ReplyInteraction");
    if (r >= 0) {
        r = sd_bus_message_open_container(m, 'a', "{sv}");
    }
    if (r >= 0) {
        r = append_sv_string(m, "action", action);
    }
    if (r >= 0) {
        r = sd_bus_message_close_container(m);
    }
    if (r >= 0) {
        sd_bus_call(ctx->bus, m, CALL_TIMEOUT_USEC, &err, &reply);
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(m);
    sd_bus_message_unref(reply);
}

void linyaps_set_interaction_callback(LinyapsContext *ctx,
                                      LinyapsInteractionCallback cb,
                                      void *userdata)
{
    if (!ctx) {
        return;
    }
    ctx->interaction_cb = cb;
    ctx->interaction_userdata = userdata;
}

void linyaps_prune(LinyapsContext *ctx, LinyapsCompletionCallback cb, void *userdata)
{
    if (!ctx || !ctx->bus) {
        if (cb) {
            cb(-EINVAL, "invalid context", userdata);
        }
        return;
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method(ctx->bus, PM_DEST, PM_PATH, PM_IFACE, "Prune", &err, &reply, "");
    int64_t code = r;
    char *message = NULL;
    if (r >= 0) {
        r = parse_common_result(reply, &code, &message);
    }
    if (cb) {
        cb(r < 0 ? r : (int)code,
           r < 0 ? strerror(-r) : (message ? message : ""),
           userdata);
    }
    free(message);
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
}

static char *json_dup_string(const cJSON *obj, const char *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
    if (!cJSON_IsString(item) || !item->valuestring) {
        return NULL;
    }
    return dupstr(item->valuestring);
}

static int64_t json_int64(const cJSON *obj, const char *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, field);
    if (!cJSON_IsNumber(item)) {
        return 0;
    }
    return (int64_t)item->valuedouble;
}

static char *json_first_string(const cJSON *obj, const char *field)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(obj, field);
    const cJSON *item = cJSON_GetArrayItem(array, 0);
    if (!cJSON_IsString(item) || !item->valuestring) {
        return NULL;
    }
    return dupstr(item->valuestring);
}

static void parse_installed_object(const cJSON *obj,
                                   LinyapsPackageInfo ***items,
                                   size_t *count,
                                   size_t *cap)
{
    LinyapsPackageInfo *info = package_info_new();
    if (!info) {
        return;
    }
    info->id = json_dup_string(obj, "id");
    info->name = json_dup_string(obj, "name");
    info->version = json_dup_string(obj, "version");
    info->channel = json_dup_string(obj, "channel");
    info->description = json_dup_string(obj, "description");
    info->kind = json_dup_string(obj, "kind");
    info->module = json_dup_string(obj, "module");
    info->arch = json_first_string(obj, "arch");
    info->repo = dupstr("local");
    info->size = json_int64(obj, "size");
    if (!info->id) {
        linyaps_package_info_free(info);
        return;
    }
    package_list_append(items, count, cap, info);
}

LinyapsPackageInfo **linyaps_list_installed(LinyapsContext *ctx, size_t *out_count)
{
    (void)ctx;
    if (out_count) {
        *out_count = 0;
    }
    FILE *fp = popen("ll-cli list --json", "r");
    if (!fp) {
        return NULL;
    }
    size_t capbuf = 8192;
    size_t len = 0;
    char *buf = malloc(capbuf);
    if (!buf) {
        pclose(fp);
        return NULL;
    }
    for (;;) {
        if (len + 4096 + 1 > capbuf) {
            capbuf *= 2;
            char *tmp = realloc(buf, capbuf);
            if (!tmp) {
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = tmp;
        }
        size_t n = fread(buf + len, 1, 4096, fp);
        len += n;
        if (n < 4096) {
            if (feof(fp)) {
                break;
            }
            if (ferror(fp)) {
                free(buf);
                pclose(fp);
                return NULL;
            }
        }
    }
    pclose(fp);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return NULL;
    }

    LinyapsPackageInfo **items = NULL;
    size_t count = 0;
    size_t cap = 0;
    const cJSON *obj = NULL;
    cJSON_ArrayForEach(obj, root)
    {
        if (cJSON_IsObject(obj)) {
            parse_installed_object(obj, &items, &count, &cap);
        }
    }
    cJSON_Delete(root);
    if (out_count) {
        *out_count = count;
    }
    return items;
}

void linyaps_package_info_free(LinyapsPackageInfo *info)
{
    if (!info) {
        return;
    }
    free(info->id);
    free(info->name);
    free(info->version);
    free(info->arch);
    free(info->channel);
    free(info->repo);
    free(info->description);
    free(info->kind);
    free(info->module);
    free(info);
}

void linyaps_package_info_list_free(LinyapsPackageInfo **list, size_t count)
{
    if (!list) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        linyaps_package_info_free(list[i]);
    }
    free(list);
}

const char *linyaps_task_state_string(LinyapsTaskState state)
{
    switch (state) {
    case LINYAPS_TASK_STATE_UNKNOWN:
        return "Unknown";
    case LINYAPS_TASK_STATE_QUEUED:
        return "Queued";
    case LINYAPS_TASK_STATE_PENDING:
        return "Pending";
    case LINYAPS_TASK_STATE_PROCESSING:
        return "Processing";
    case LINYAPS_TASK_STATE_SUCCEED:
        return "Succeed";
    case LINYAPS_TASK_STATE_FAILED:
        return "Failed";
    case LINYAPS_TASK_STATE_CANCELED:
        return "Canceled";
    default:
        return "Invalid";
    }
}
