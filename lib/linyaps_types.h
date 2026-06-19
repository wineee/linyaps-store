/* SPDX-License-Identifier: LGPL-3.0-or-later */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LINYAPS_TASK_STATE_UNKNOWN = 0,
    LINYAPS_TASK_STATE_QUEUED = 1,
    LINYAPS_TASK_STATE_PENDING = 2,
    LINYAPS_TASK_STATE_PROCESSING = 3,
    LINYAPS_TASK_STATE_SUCCEED = 4,
    LINYAPS_TASK_STATE_FAILED = 5,
    LINYAPS_TASK_STATE_CANCELED = 6,
} LinyapsTaskState;

typedef struct LinyapsPackageInfo {
    char *id;
    char *name;
    char *version;
    char *arch;
    char *channel;
    char *repo;
    char *description;
    char *kind;
    char *module;
    char *base;
    char *runtime;
    char *schema_version;
    char *command;
    int64_t size;
    char *create_time;
    int64_t download_count;
} LinyapsPackageInfo;

typedef struct LinyapsTaskProgress {
    char *object_path;
    LinyapsTaskState state;
    double percentage;
    char *message;
    int error_code;
} LinyapsTaskProgress;

typedef struct LinyapsInteractionRequest {
    char *object_path;
    int message_id;
    char *local_ref;
    char *remote_ref;
    char *summary;
    char *body;
} LinyapsInteractionRequest;

typedef void (*LinyapsProgressCallback)(const LinyapsTaskProgress *progress,
                                        void *userdata);

typedef void (*LinyapsInteractionCallback)(const LinyapsInteractionRequest *request,
                                           void *userdata);

typedef void (*LinyapsSearchCallback)(LinyapsPackageInfo **results,
                                      size_t count,
                                      int error_code,
                                      const char *error_msg,
                                      void *userdata);

typedef void (*LinyapsCompletionCallback)(int error_code,
                                          const char *message,
                                          void *userdata);

#ifdef __cplusplus
}
#endif
