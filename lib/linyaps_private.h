/* SPDX-License-Identifier: MIT */

#pragma once

#define _POSIX_C_SOURCE 200809L

#include "linyaps_backend.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* D-Bus well-known names                                              */
/* ------------------------------------------------------------------ */

#define PM_DEST          "org.deepin.linglong.PackageManager1"
#define PM_PATH          "/org/deepin/linglong/PackageManager1"
#define PM_IFACE         "org.deepin.linglong.PackageManager1"
#define TASK_IFACE       "org.deepin.linglong.Task1"
#define PROPS_IFACE      "org.freedesktop.DBus.Properties"
#define CALL_TIMEOUT_USEC (120ULL * 1000000ULL)
#define MAX_TASKS 16

/* ------------------------------------------------------------------ */
/* Internal types                                                       */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* linyaps_util.c                                                       */
/* ------------------------------------------------------------------ */

void free_string(char **s);
char *dupstr(const char *s);
void progress_clear(LinyapsTaskProgress *p);
void progress_set_string(char **dst, const char *src);
void package_list_append(LinyapsPackageInfo ***items,
                         size_t *count,
                         size_t *cap,
                         LinyapsPackageInfo *info);
void notify_failed(LinyapsProgressCallback cb,
                   void *userdata,
                   const char *message,
                   int code);
void linyaps_interaction_request_clear(LinyapsInteractionRequest *request);

/* ------------------------------------------------------------------ */
/* linyaps_task.c                                                       */
/* ------------------------------------------------------------------ */

void pending_push(LinyapsContext *ctx, LinyapsProgressCallback cb, void *userdata);
int  pending_pop(LinyapsContext *ctx, LinyapsProgressCallback *cb, void **userdata);
void pending_pop_tail(LinyapsContext *ctx);

void      search_push(LinyapsContext *ctx, const char *job_id,
                      LinyapsSearchCallback cb, void *userdata);
SearchOp *search_take(LinyapsContext *ctx, const char *job_id);
void      search_op_free(SearchOp *op);

TaskEntry *find_task(LinyapsContext *ctx, const char *path);
TaskEntry *alloc_task(LinyapsContext *ctx);
void       clear_task(TaskEntry *te);
int        read_task_properties(LinyapsContext *ctx, TaskEntry *te);

/* Signal handlers registered via sd_bus_add_match */
int on_task_added(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
int on_task_removed(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
int on_search_finished(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/* ------------------------------------------------------------------ */
/* linyaps_dbus_msg.c                                                   */
/* ------------------------------------------------------------------ */

int append_sv_string(sd_bus_message *m, const char *key, const char *value);
int append_sv_bool(sd_bus_message *m, const char *key, int value);
int append_repos(sd_bus_message *m, const char *repos);
int append_package(sd_bus_message *m,
                   const char *id,
                   const char *version,
                   const char *channel,
                   const char *module,
                   int include_module);
int append_options(sd_bus_message *m, int include_force, int force, int include_skip);

int parse_common_result(sd_bus_message *reply, int64_t *code, char **message);
int parse_job_info(sd_bus_message *reply, int64_t *code, char **message, char **job_id);

int read_variant_string(sd_bus_message *m, const char *sig, char **out);
int read_variant_int64(sd_bus_message *m, const char *sig, int64_t *out);
int read_variant_first_string_array(sd_bus_message *m, const char *sig, char **out);

int parse_package_map(sd_bus_message *m, const char *repo, LinyapsPackageInfo **out_info);
int parse_search_reply(sd_bus_message *reply,
                       LinyapsPackageInfo ***out_items,
                       size_t *out_count,
                       int64_t *out_code,
                       char **out_message);

void filter_store_search_results(LinyapsPackageInfo ***items, size_t *count);
char *configured_repos_csv(LinyapsContext *ctx);
