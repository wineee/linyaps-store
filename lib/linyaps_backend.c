/* SPDX-License-Identifier: LGPL-3.0-or-later */

#define _POSIX_C_SOURCE 200809L

#include "linyaps_backend.h"

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

typedef struct {
    char *path;
    LinyapsProgressCallback cb;
    void *userdata;
    sd_bus_slot *slot;
    LinyapsTaskProgress progress;
    int active;
} TaskEntry;

struct LinyapsContext {
    sd_bus *bus;
    sd_bus_slot *task_added_slot;
    sd_bus_slot *task_removed_slot;
    PendingOp *pending_head;
    PendingOp *pending_tail;
    TaskEntry tasks[MAX_TASKS];
};

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

static int append_options(sd_bus_message *m, int include_force, int force, int include_confirm)
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
    if (include_confirm && (r = append_sv_bool(m, "confirmInteraction", 0)) < 0) {
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
    free_string(&te->path);
    progress_clear(&te->progress);
    memset(te, 0, sizeof(*te));
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
    for (size_t i = 0; i < MAX_TASKS; i++) {
        clear_task(&ctx->tasks[i]);
    }
    ctx->task_added_slot = sd_bus_slot_unref(ctx->task_added_slot);
    ctx->task_removed_slot = sd_bus_slot_unref(ctx->task_removed_slot);
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
    if (r >= 0) {
        r = append_repos(m, repos);
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
        sd_bus_error_free(&err);
        sd_bus_message_unref(m);
        sd_bus_message_unref(reply);
        return;
    }
    LinyapsPackageInfo **items = NULL;
    size_t count = 0;
    int64_t code = 0;
    char *message = NULL;
    r = parse_search_reply(reply, &items, &count, &code, &message);
    if (cb) {
        cb(r < 0 || code != 0 ? NULL : items,
           r < 0 || code != 0 ? 0 : count,
           r < 0 ? r : (int)code,
           r < 0 ? strerror(-r) : message,
           userdata);
    }
    if (r < 0 || code != 0 || !cb) {
        linyaps_package_info_list_free(items, count);
    }
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

static int json_str(const char *obj, const char *field, char **out)
{
    char key[128];
    snprintf(key, sizeof(key), "\"%s\"", field);
    const char *p = strstr(obj, key);
    if (!p) {
        return -ENOENT;
    }
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (*p++ != ':') {
        return -EINVAL;
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (*p++ != '"') {
        return -EINVAL;
    }
    size_t cap = 64;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        return -ENOMEM;
    }
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            c = *p++;
            if (c == 'n') {
                c = '\n';
            } else if (c == 't') {
                c = '\t';
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                return -ENOMEM;
            }
            buf = tmp;
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    *out = buf;
    return 0;
}

static int64_t json_int(const char *obj, const char *field)
{
    char key[128];
    snprintf(key, sizeof(key), "\"%s\"", field);
    const char *p = strstr(obj, key);
    if (!p) {
        return 0;
    }
    p += strlen(key);
    while (*p && *p != ':') {
        p++;
    }
    if (*p == ':') {
        p++;
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return strtoll(p, NULL, 10);
}

static int json_arch_first(const char *obj, char **out)
{
    const char *p = strstr(obj, "\"arch\"");
    if (!p) {
        return -ENOENT;
    }
    p = strchr(p, '[');
    if (!p) {
        return -EINVAL;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (*p++ != '"') {
        return -EINVAL;
    }
    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p += 2;
        } else {
            p++;
        }
    }
    size_t len = (size_t)(p - start);
    char *buf = malloc(len + 1);
    if (!buf) {
        return -ENOMEM;
    }
    memcpy(buf, start, len);
    buf[len] = '\0';
    *out = buf;
    return 0;
}

static void parse_installed_object(const char *obj,
                                   LinyapsPackageInfo ***items,
                                   size_t *count,
                                   size_t *cap)
{
    LinyapsPackageInfo *info = package_info_new();
    if (!info) {
        return;
    }
    json_str(obj, "id", &info->id);
    json_str(obj, "name", &info->name);
    json_str(obj, "version", &info->version);
    json_str(obj, "channel", &info->channel);
    json_str(obj, "description", &info->description);
    json_str(obj, "kind", &info->kind);
    json_str(obj, "module", &info->module);
    json_arch_first(obj, &info->arch);
    info->repo = dupstr("local");
    info->size = json_int(obj, "size");
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

    LinyapsPackageInfo **items = NULL;
    size_t count = 0;
    size_t cap = 0;
    int depth = 0;
    int in_str = 0;
    int esc = 0;
    char *start = NULL;
    for (char *p = buf; *p; p++) {
        if (in_str) {
            if (esc) {
                esc = 0;
            } else if (*p == '\\') {
                esc = 1;
            } else if (*p == '"') {
                in_str = 0;
            }
            continue;
        }
        if (*p == '"') {
            in_str = 1;
        } else if (*p == '{') {
            if (depth == 0) {
                start = p;
            }
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0 && start) {
                char saved = p[1];
                p[1] = '\0';
                parse_installed_object(start, &items, &count, &cap);
                p[1] = saved;
                start = NULL;
            }
        }
    }
    free(buf);
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
