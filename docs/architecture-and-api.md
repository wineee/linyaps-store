# Architecture and public API

## Goal

`linyaps-store` is a small C library for a GUI application store. It replaces
the fragile legacy pattern:

```text
UI -> popen/fork -> ll-cli -> stdout parsing -> regex state machine
```

with direct communication to the Linglong package manager daemon:

```text
UI -> linyaps-store C API -> sd-bus -> org.deepin.linglong.PackageManager1
```

The UI will be implemented later. The library is designed so an immediate-mode
UI can call `linyaps_process()` once per frame to dispatch D-Bus signals.

## Runtime service

Observed service:

```text
Bus name:  org.deepin.linglong.PackageManager1
Object:    /org/deepin/linglong/PackageManager1
Bus:       system bus
Process:   ll-package-manager
```

The service is on the system bus, not the session bus.

## Dependency choice

The library uses:

```text
sd-bus from libsystemd
cJSON vendored under 3rdparty/cjson
```

`sd-bus` was chosen instead of GDBus because it keeps the implementation C-only,
avoids GObject, and integrates cleanly with a caller-driven event loop.

cJSON is used for the local JSON fallback paths:

```text
ll-cli list --json
ll-cli --json info <app_id>
```

`nlohmann-json3` was not used because it would make the implementation C++ and
introduce C/C++ ABI and exception/linking considerations for little benefit.

## Public API

Main lifecycle:

```c
LinyapsContext *linyaps_context_new(void);
void linyaps_context_free(LinyapsContext *ctx);
int linyaps_process(LinyapsContext *ctx);
bool linyaps_is_service_available(LinyapsContext *ctx);
```

Read/query operations:

```c
void linyaps_search(LinyapsContext *ctx,
                    const char *keyword,
                    const char *repos,
                    LinyapsSearchCallback cb,
                    void *userdata);

LinyapsPackageInfo **linyaps_list_installed(LinyapsContext *ctx,
                                            size_t *out_count);

LinyapsPackageInfo *linyaps_info(LinyapsContext *ctx, const char *app_id);
```

Package operations:

```c
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
```

Interaction confirmation:

```c
void linyaps_set_interaction_callback(LinyapsContext *ctx,
                                      LinyapsInteractionCallback cb,
                                      void *userdata);

void linyaps_reply_interaction(LinyapsContext *ctx,
                               const char *task_object_path,
                               bool accepted);
```

Memory management:

```c
void linyaps_package_info_free(LinyapsPackageInfo *info);
void linyaps_package_info_list_free(LinyapsPackageInfo **list, size_t count);
```

## Data structures

`LinyapsPackageInfo` is shared by list, info and search results:

```c
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
```

All strings are heap-owned by the package info object and freed by
`linyaps_package_info_free()`.

Task state maps directly to `org.deepin.linglong.Task1.State`:

```text
0 Unknown
1 Queued
2 Pending
3 Processing
4 Succeed
5 Failed
6 Canceled
```

## Event loop model

The library does not spawn a background thread. The caller drives D-Bus signal
dispatch:

```c
while (running) {
    handle_input();
    linyaps_process(ctx);
    render_frame();
}
```

Callbacks are invoked from the thread that calls `linyaps_process()`.

## Implemented GUI-store subset

Implemented:

```text
service availability
installed list
installed package info
remote search
install
uninstall
update
cancel
task progress
interaction confirmation
prune test hook
remote rankings (newest / downloads)
```

### Remote Store API (libcurl based)

We communicate with the remote community store API `https://storeapi.linyaps.org.cn` to fetch catalog search results and rankings.

```c
typedef enum {
    LINYAPS_RANKING_NEWEST = 0,
    LINYAPS_RANKING_DOWNLOADS,
} LinyapsRankingType;

LinyapsRemoteAppInfo **linyaps_remote_fetch_apps(
    const char *keyword,
    const char *category_id,
    int page,
    int page_size,
    size_t *out_count,
    long   *out_total);

LinyapsRemoteAppInfo **linyaps_remote_fetch_ranking(
    LinyapsRankingType type,
    int                page,
    int                page_size,
    size_t            *out_count,
    long              *out_total);
```

Intentionally not implemented:

```text
builder commands
run/debug helpers
repository add/remove/modify
layer file inspection beyond ll-cli info fallback
desktop shortcut creation
process management UI
```
