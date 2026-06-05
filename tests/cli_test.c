/* SPDX-License-Identifier: LGPL-3.0-or-later */

#define _POSIX_C_SOURCE 200809L

#include "linyaps_backend.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char last_task[512];
    int active;
} CliState;

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return s;
}

static void print_package(const LinyapsPackageInfo *info, size_t index)
{
    printf("%3zu  %-42s  %-12s  %-8s  %s\n",
           index,
           info->id ? info->id : "",
           info->version ? info->version : "",
           info->channel ? info->channel : "",
           info->name ? info->name : "");
}

static void print_info(const LinyapsPackageInfo *info)
{
    printf("id: %s\n", info->id ? info->id : "");
    printf("name: %s\n", info->name ? info->name : "");
    printf("version: %s\n", info->version ? info->version : "");
    printf("channel: %s\n", info->channel ? info->channel : "");
    printf("module: %s\n", info->module ? info->module : "");
    printf("kind: %s\n", info->kind ? info->kind : "");
    printf("arch: %s\n", info->arch ? info->arch : "");
    printf("base: %s\n", info->base ? info->base : "");
    printf("runtime: %s\n", info->runtime ? info->runtime : "");
    printf("command: %s\n", info->command ? info->command : "");
    printf("schema_version: %s\n", info->schema_version ? info->schema_version : "");
    printf("size: %lld\n", (long long)info->size);
    const char *description = info->description ? info->description : "";
    printf("description: %s", description);
    size_t description_len = strlen(description);
    if (description_len == 0 || description[description_len - 1] != '\n') {
        putchar('\n');
    }
}

static void search_cb(LinyapsPackageInfo **results,
                      size_t count,
                      int error_code,
                      const char *error_msg,
                      void *userdata)
{
    int *pending = userdata;
    if (error_code != 0) {
        fprintf(stderr, "search failed: %s (%d)\n", error_msg ? error_msg : "unknown", error_code);
        if (pending) {
            *pending = 0;
        }
        return;
    }
    printf("found %zu packages\n", count);
    for (size_t i = 0; i < count; i++) {
        print_package(results[i], i + 1);
    }
    linyaps_package_info_list_free(results, count);
    if (pending) {
        *pending = 0;
    }
}

static void progress_cb(const LinyapsTaskProgress *progress, void *userdata)
{
    CliState *state = userdata;
    if (!progress) {
        fprintf(stderr, "task failed before progress was available\n");
        return;
    }
    if (progress->object_path) {
        snprintf(state->last_task, sizeof(state->last_task), "%s", progress->object_path);
    }
    state->active = progress->state != LINYAPS_TASK_STATE_SUCCEED &&
                    progress->state != LINYAPS_TASK_STATE_FAILED &&
                    progress->state != LINYAPS_TASK_STATE_CANCELED;
    printf("[%s] %3.0f%% %s%s%s\n",
           linyaps_task_state_string(progress->state),
           progress->percentage * 100.0,
           progress->object_path ? progress->object_path : "",
           progress->message ? "  " : "",
           progress->message ? progress->message : "");
}

static void interaction_cb(const LinyapsInteractionRequest *request, void *userdata)
{
    LinyapsContext *ctx = userdata;
    printf("interaction: %s -> %s\n",
           request->local_ref ? request->local_ref : "",
           request->remote_ref ? request->remote_ref : "");
    linyaps_reply_interaction(ctx, request->object_path, true);
}

static void completion_cb(int error_code, const char *message, void *userdata)
{
    (void)userdata;
    printf("complete: code=%d message=%s\n", error_code, message ? message : "");
}

static void pump_until_idle(LinyapsContext *ctx)
{
    for (int i = 0; i < 10; i++) {
        int r = linyaps_process(ctx);
        if (r <= 0) {
            break;
        }
    }
}

static void wait_task(LinyapsContext *ctx, CliState *state)
{
    while (state->active) {
        const struct timespec delay = { .tv_sec = 0, .tv_nsec = 100000000L };
        linyaps_process(ctx);
        nanosleep(&delay, NULL);
    }
}

static void print_help(void)
{
    puts("commands:");
    puts("  service");
    puts("  list");
    puts("  info <app_id>");
    puts("  search <keyword> [repo1,repo2]");
    puts("  install <app_id> [version] [channel]");
    puts("  uninstall <app_id> <version> [channel]");
    puts("  update <app_id>");
    puts("  prune");
    puts("  cancel");
    puts("  wait");
    puts("  quit");
}

int main(void)
{
    LinyapsContext *ctx = linyaps_context_new();
    if (!ctx) {
        fputs("failed to open system D-Bus\n", stderr);
        return 1;
    }

    CliState state = { 0 };
    linyaps_set_interaction_callback(ctx, interaction_cb, ctx);
    char line[1024];
    print_help();
    for (;;) {
        pump_until_idle(ctx);
        fputs("linyaps-cli> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        char *cmd = trim(line);
        if (*cmd == '\0') {
            continue;
        }
        char *argv[5] = { 0 };
        int argc = 0;
        for (char *tok = strtok(cmd, " \t"); tok && argc < 5; tok = strtok(NULL, " \t")) {
            argv[argc++] = tok;
        }
        if (argc == 0) {
            continue;
        }
        if (strcmp(argv[0], "quit") == 0 || strcmp(argv[0], "exit") == 0) {
            break;
        } else if (strcmp(argv[0], "help") == 0) {
            print_help();
        } else if (strcmp(argv[0], "service") == 0) {
            puts(linyaps_is_service_available(ctx) ? "available" : "unavailable");
        } else if (strcmp(argv[0], "list") == 0) {
            size_t count = 0;
            LinyapsPackageInfo **items = linyaps_list_installed(ctx, &count);
            if (!items) {
                puts("no installed package data");
                continue;
            }
            for (size_t i = 0; i < count; i++) {
                print_package(items[i], i + 1);
            }
            linyaps_package_info_list_free(items, count);
        } else if (strcmp(argv[0], "info") == 0 && argc >= 2) {
            LinyapsPackageInfo *info = linyaps_info(ctx, argv[1]);
            if (!info) {
                puts("no package info");
                continue;
            }
            print_info(info);
            linyaps_package_info_free(info);
        } else if (strcmp(argv[0], "search") == 0 && argc >= 2) {
            int pending = 1;
            linyaps_search(ctx, argv[1], argc >= 3 ? argv[2] : NULL, search_cb, &pending);
            while (pending) {
                linyaps_process(ctx);
                const struct timespec delay = { .tv_sec = 0, .tv_nsec = 100000000L };
                nanosleep(&delay, NULL);
            }
        } else if (strcmp(argv[0], "install") == 0 && argc >= 2) {
            state.active = 1;
            linyaps_install(ctx,
                            argv[1],
                            argc >= 3 ? argv[2] : NULL,
                            argc >= 4 ? argv[3] : "main",
                            progress_cb,
                            &state);
        } else if (strcmp(argv[0], "uninstall") == 0 && argc >= 3) {
            state.active = 1;
            linyaps_uninstall(ctx,
                              argv[1],
                              argv[2],
                              argc >= 4 ? argv[3] : "main",
                              progress_cb,
                              &state);
        } else if (strcmp(argv[0], "update") == 0 && argc >= 2) {
            state.active = 1;
            linyaps_update(ctx, argv[1], progress_cb, &state);
        } else if (strcmp(argv[0], "prune") == 0) {
            linyaps_prune(ctx, completion_cb, NULL);
        } else if (strcmp(argv[0], "cancel") == 0) {
            if (state.last_task[0]) {
                linyaps_cancel_task(ctx, state.last_task);
            } else {
                puts("no known task");
            }
        } else if (strcmp(argv[0], "wait") == 0) {
            wait_task(ctx, &state);
        } else {
            print_help();
        }
    }

    linyaps_context_free(ctx);
    return 0;
}
