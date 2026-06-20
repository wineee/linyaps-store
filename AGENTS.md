# AGENTS.md — linyaps-store AI 代理指南

> **此文件由 AI 维护，随代码演进自动更新。**  
> 阅读顺序：本文件 → `docs/` → `kilnui/AGENTS.md`。

---

## 项目概览

`linyaps-store` 是第三方 Linglong 应用商店，使用原生 C23 实现。

```
UI (Clay / KilnUI)
  └── ui/              ← 应用 UI 层（store_ui.c/h, store_state.c/h, main.c）
        │
        ├── lib/       ← D-Bus 后端库（C23, sd-bus）
        │
        └── kilnui/    ← 自研 GPU UI 渲染库（Clay + SDL3 GPU，可自由修改）
```

核心职责：

- **`lib/`** — 通过 sd-bus 与 `org.deepin.linglong.PackageManager1` 通信，实现安装、卸载、搜索等操作，不依赖 GLib/GDBus。
- **`kilnui/`** — 自研 UI 渲染框架，Clay 布局 + SDL3 GPU，包含完整的设计系统和组件库。**可直接修改源码。**
- **`ui/`** — 应用层，将后端 API 和 KilnUI 组件拼装成商店界面。

---

## 目录结构

```
linyaps-store/
├── AGENTS.md               # 本文件（AI 维护）
├── CMakeLists.txt          # 根构建入口
├── lib/                    # 后端 C 库
│   ├── linyaps_types.h     # 公共数据结构 & 回调类型
│   ├── linyaps_backend.h   # 公共 API（给 UI 用）
│   ├── linyaps_private.h   # 内部结构（不对外）
│   ├── linyaps_context.c   # D-Bus 连接、事件分发
│   ├── linyaps_dbus_msg.c  # 请求/响应解析（a{sv}）
│   ├── linyaps_task.c      # 任务跟踪 & 进度回调
│   ├── linyaps_util.c      # 字符串工具
│   └── linyaps_cli.c       # CLI 辅助（ll-cli fallback）
├── ui/                     # GUI 应用层
│   ├── main.c              # 事件循环入口
│   ├── store_state.h/c     # 全局 UI 状态（StoreState）
│   ├── store_ui.h/c        # Clay 布局 & 渲染逻辑
│   ├── id_set.h/c          # 已安装应用 ID 哈希表（O(1) 查找）
├── tests/
│   ├── cli_test.c          # 手动 CLI 验证工具
│   └── test_backend.c      # 单元测试（CMake ctest）
├── kilnui/                 # 自研 UI 库（子目录，见下节）
├── 3rdparty/cjson/         # vendored cJSON
└── docs/                   # 研究文档（AI 维护，见下节）
```

---

## 后端库（`lib/`）

### 标准

- **语言**：C23（`-std=c23`）
- **依赖**：`libsystemd`（sd-bus）、vendored cJSON

### 公共 API（`linyaps_backend.h`）

```c
/* 生命周期 */
LinyapsContext *linyaps_context_new(void);
void            linyaps_context_free(LinyapsContext *ctx);
int             linyaps_process(LinyapsContext *ctx);   // 每帧调用，分发 D-Bus 信号
bool            linyaps_is_service_available(LinyapsContext *ctx);

/* 查询 */
void             linyaps_search(ctx, keyword, repos, cb, userdata);
LinyapsPackageInfo **linyaps_list_installed(ctx, *out_count);
LinyapsPackageInfo  *linyaps_info(ctx, app_id);

/* 操作 */
void linyaps_install(ctx, app_id, version, channel, cb, userdata);
void linyaps_uninstall(ctx, app_id, version, channel, cb, userdata);
void linyaps_update(ctx, app_id, cb, userdata);
void linyaps_cancel_task(ctx, task_object_path);
void linyaps_prune(ctx, cb, userdata);

/* 交互确认 */
void linyaps_set_interaction_callback(ctx, cb, userdata);
void linyaps_reply_interaction(ctx, task_object_path, accepted);

/* 内存释放 */
void linyaps_package_info_free(LinyapsPackageInfo *info);
void linyaps_package_info_list_free(LinyapsPackageInfo **list, size_t count);

/* 远端 Store API */
LinyapsRemoteAppInfo **linyaps_remote_fetch_apps(keyword, category_id, page, page_size, out_count, out_total);
LinyapsRemoteAppInfo **linyaps_remote_fetch_welcome_apps(page, page_size, out_count, out_total);
LinyapsRemoteAppInfo **linyaps_remote_fetch_ranking(type, page, page_size, out_count, out_total);
```

### 事件循环模型

库不创建线程，由调用方驱动：

```c
while (running) {
    handle_input();
    linyaps_process(ctx);   // 分发 D-Bus 信号，触发回调
    render_frame();
}
```

所有回调均在调用 `linyaps_process()` 的线程上执行。

### 关键数据结构

```c
typedef struct LinyapsPackageInfo {
    char    *id, *name, *version, *arch, *channel, *repo;
    char    *description, *kind, *module, *base, *runtime;
    char    *schema_version, *command;
    int64_t  size;
    char    *create_time;
    int64_t  download_count;
} LinyapsPackageInfo;  // 所有字符串由结构体持有，free 时一并释放

typedef struct LinyapsTaskProgress {
    char           *object_path;
    LinyapsTaskState state;   // 0=Unknown … 6=Canceled
    double          percentage;
    char           *message;
    int             error_code;
} LinyapsTaskProgress;
```

### D-Bus 服务信息

```
Bus:    system bus
Name:   org.deepin.linglong.PackageManager1
Object: /org/deepin/linglong/PackageManager1
```

请求/响应格式为 `a{sv}`（关联数组，详见 `docs/dbus-wire-format.md`）。

### 注意事项

- `linyaps_list_installed` 和 `linyaps_info` 是同步 JSON fallback（`ll-cli --json`），其余操作是异步 D-Bus。
- `linyaps_search` 是异步的；结果通过 `LinyapsSearchCallback` 回调返回，**不是**返回值。
- 图标字段在 D-Bus/`ll-cli` 中**不存在**；需要远端 API 补全或本地 `.desktop` 解析。详见 `docs/icon-source.md`。

---

## KilnUI 自研 UI 库（`kilnui/`）

> KilnUI 是项目自研库，**可以直接修改源码**以满足需求。详细参考 `kilnui/AGENTS.md`。

### 架构

```
Clay layout → Clay_RenderCommandArray → KilnUI_render() → SDL3 GPU draw calls
```

### 关键文件

| 文件 | 职责 |
|------|------|
| `kilnui/src/kilnui.h` | 公共 API：`KilnUI_init`, `KilnUI_handle_event`, `KilnUI_render`, `KilnUI_destroy` |
| `kilnui/src/kilnui.c` | 生命周期：SDL/GPU/字体/Clay 初始化，事件路由 |
| `kilnui/src/kilnui_render.c` | 热路径：正交投影、矩形/文字/阴影批处理、单次 GPU 上传 |
| `kilnui/src/glyph_cache.c/h` | 字形 GPU 纹理缓存（开放寻址哈希表） |
| `kilnui/src/ui/ui.h` | Widget 总入口（button、input、checkbox、radio、slider 等） |
| `kilnui/src/ui/design_system.h` | 设计令牌：间距、圆角、字号、Catppuccin 主题 |
| `kilnui/shaders/*.glsl` | GLSL 着色器，构建时编译为 SPIR-V |

### Widget 使用模式

```c
#include "kilnui.h"
#include "ui/ui.h"

// 渲染循环内：
Clay_BeginLayout();
UI_ROW(uid, padding) {
    UI_Button(uid, "安装", UI_BTN_PRIMARY, UI_MD, false);
    CLAY_TEXT(CLAY_STRING("Hello"), &(Clay_TextElementConfig){ .fontSize = 16 });
}
Clay_RenderCommandArray cmds = Clay_EndLayout();
KilnUI_render(&ctx, cmds);
```

### 修改 KilnUI 时的规则

- **新增 Widget**：仿照 `kilnui/src/ui/button.c` 的模式：计算 `Clay_ElementId`、检测 hover/press、使用 `ds_theme` 颜色。
- **新增 Shader**：在 `kilnui/shaders/` 添加 `.glsl`，在 `kilnui/CMakeLists.txt` 的 `shaders` target 里注册，在渲染器中通过 `KilnUICustomHeader.type` 分发。
- **颜色**：始终使用预乘 Alpha：`r = (c.r/255) * (c.a/255)`。
- **ID**：内部元素用 `CLAY_ID_LOCAL("name")`，有状态 widget 用 `Clay_GetElementIdWithIndex(CLAY_STRING("prefix"), uid)`。
- **`Clay_StringSlice`** 不以 null 结尾，始终用 `.length`。

---

## UI 层（`ui/`）

### `StoreState`（`ui/store_state.h`）

全局应用状态，由 `store_state_init()` 初始化、`store_state_free()` 释放：

```c
typedef struct {
    LinyapsContext *ctx;

    NavItem     active_nav;   // 侧边栏导航（推荐/全部/排行/更新/分类）
    CategoryTab active_cat;   // 顶部分类 Tab
    int         cat_scroll_x; // Tab 横向滚动偏移

    char search_buf[256];
    bool search_focused;

    LinyapsPackageInfo **search_results; size_t search_count;
    LinyapsPackageInfo **installed_list; size_t installed_count;
    IdSet                installed_id_set; // O(1) 查找已安装应用 ID

    /* updates */
    StoreUpdateItem *update_list;
    size_t           update_count;
    bool             checking_updates;
    float            check_updates_timer;

    bool dark_mode;
    bool dirty;   // 脏标志驱动渲染，false 时跳过 CPU 重布局
} StoreState;
```

### `IdSet`（`ui/id_set.h`）

开放寻址哈希表，用于 O(1) 查找已安装应用 ID。使用 FNV-1a 哈希，负载因子上限 0.75。

```c
typedef struct IdSet {
    char  **keys;     // NULL 表示空槽
    size_t  capacity;
    size_t  count;
} IdSet;

void id_set_init(IdSet *set);
void id_set_free(IdSet *set);
bool id_set_insert(IdSet *set, const char *key);
bool id_set_contains(const IdSet *set, const char *key);
void id_set_build_from_packages(IdSet *set, LinyapsPackageInfo **list, size_t count);
```

**使用场景**：在 `view_app_grid()` 和 `view_ranking()` 中，将原来的 O(n×m) 线性扫描替换为 O(1) 哈希查找：

```c
// 旧代码 (O(n×m)):
for (size_t k = 0; k < g_state->installed_count; k++) {
    if (strcmp(g_state->installed_list[k]->id, list[i]->id) == 0) {
        inst = true; break;
    }
}

// 新代码 (O(1)):
bool inst = id_set_contains(&g_state->installed_id_set, list[i]->id);
```

### `store_ui.c`

用 Clay 宏构建商店 UI：侧边栏、搜索栏、分类 Tab、应用卡片网格。  
每帧在 `dirty == true` 时重新布局，由后端回调或用户输入置 `dirty`。

### 脏标志驱动渲染优化

`dirty` 标志控制是否需要重布局。优化策略：

```c
// handle_sdl_event 中：只在真正需要重布局的事件上设置 dirty
switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION:
        // 鼠标移动：只更新位置，不立即设置 dirty
        input->mx = e->motion.x;
        input->my = e->motion.y;
        input->mouse_moved = true;
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_TEXT_INPUT:
        // 点击、按键、输入：立即设置 dirty
        SDL_SetAtomicInt(&store->dirty, 1);
        break;
}

// 主循环中：只在鼠标位置实际变化时设置 dirty
if (input.mouse_moved && (input.mx != prev_mx || input.my != prev_my)) {
    SDL_SetAtomicInt(&store.dirty, 1);
    prev_mx = input.mx;
    prev_my = input.my;
}
```

**效果**：鼠标静止时零 CPU 开销，只在位置变化时触发重布局。

### 字形渲染优化：纹理图集

**问题**：每个字形一次 Draw Call，一个文字命令如果有 20 个字，就是 20 次 draw call。

**解决方案**：使用字形纹理图集（texture atlas），将所有字形合并到一张大纹理。

```c
// 旧代码：每个字形一次 draw call
for (int q = 0; q < tb->quad_count; q++) {
    SDL_BindGPUFragmentSamplers(rp, 0, ...);  // 每个字形换一次纹理
    SDL_DrawGPUIndexedPrimitives(rp, 6, 1, ...);  // 每个字形一次绘制
}

// 新代码：使用纹理图集，只需 1 次 draw call
if (tb->uses_atlas) {
    SDL_BindGPUFragmentSamplers(rp, 0,
        &(SDL_GPUTextureSamplerBinding){
            .texture = GlyphAtlas_get_texture(&ctx->glyph_atlas),
            .sampler = ctx->sampler_linear }, 1);
    SDL_DrawGPUIndexedPrimitives(rp, 6 * tb->quad_count, 1, ...);
}
```

**实现细节**：
- 纹理图集大小：2048x2048 像素
- 打包算法：Shelf（Next Fit Decreasing Height）
- 自动计算 UV 坐标
- 批量上传所有字形到 GPU

**效果**：Draw Call 从 N 次/文字命令 → 1 次/文字命令，GPU 状态切换从 N 次 → 1 次。

### 窗口大小轮询优化

**问题**：每帧都调用 `SDL_GetWindowSizeInPixels` 检测窗口大小变化，这是不必要的轮询。

**解决方案**：改为事件驱动，只在 `SDL_EVENT_WINDOW_RESIZED` 和 `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` 事件中更新窗口大小。

```c
// 旧代码：每帧轮询
int pixel_w = 0, pixel_h = 0;
SDL_GetWindowSizeInPixels(ctx.window, &pixel_w, &pixel_h);
int window_w = (int)((float)pixel_w / ctx.dpi_scale);
int window_h = (int)((float)pixel_h / ctx.dpi_scale);
if (window_w != store.window_w || window_h != store.window_h) {
    store.window_w = window_w;
    store.window_h = window_h;
    SDL_SetAtomicInt(&store.dirty, 1);
}

// 新代码：事件驱动
case SDL_EVENT_WINDOW_RESIZED:
case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    {
        int pixel_w = 0, pixel_h = 0;
        SDL_GetWindowSizeInPixels(ui->window, &pixel_w, &pixel_h);
        int window_w = (int)((float)pixel_w / ui->dpi_scale);
        int window_h = (int)((float)pixel_h / ui->dpi_scale);
        if (window_w != store->window_w || window_h != store->window_h) {
            store->window_w = window_w;
            store->window_h = window_h;
            SDL_SetAtomicInt(&store->dirty, 1);
        }
    }
    break;
```

**效果**：减少每帧不必要的系统调用，只在窗口大小实际变化时更新。

### 字体大小缓存优化

**问题**：`s_last_phys_size` 在每帧开头被重置为 0，导致每帧第一个文字命令必然触发一次字体大小设置（`TTF_SetFontSize` 有系统调用开销）。

**解决方案**：移除每帧重置，让缓存保留上一帧的值。

```c
// 旧代码：每帧重置
void KilnUI_render(...) {
    s_last_phys_size = 0;  // ← 每帧重置
    ...
}

// 新代码：保留上一帧的值
void KilnUI_render(...) {
    // 不重置 s_last_phys_size：让缓存保留上一帧的值
    // measure_text_cb 在布局阶段使用逻辑像素大小，
    // build_text_batch 使用物理像素大小，两者不同
    // set_font_size 会在需要时自动更新
    ...
}
```

**效果**：减少每帧不必要的 `TTF_SetFontSize` 调用，提升渲染性能。

---

## 构建

```bash
# 依赖：cmake 3.20+, libsystemd-dev, SDL3, SDL3_ttf, glslc (Vulkan SDK)

cmake -B build
cmake --build build

# 运行 CLI 测试（需要 linglong 服务在系统总线上）
./build/linyaps_cli

# 运行单元测试
ctest --test-dir build
```

### 构建目标

| Target | 说明 |
|--------|------|
| `linyaps_store` (lib) | 后端静态库 |
| `linyaps_store_app` | GUI 应用程序 |
| `linyaps_cli` | CLI 手动测试工具 |
| `test_backend` | ctest 单元测试 |
| `kilnui` | KilnUI 核心库 |
| `kilnui::ui_components` | KilnUI Widget 库 |

着色器在构建后自动复制到可执行文件旁的 `shaders/` 目录。

---

## 参考代码库

如需了解 Linglong 的远端 API、图标获取、数据模型等，可阅读：

- **`/home/deepin/ui/flutter-linglong-store`** — 参考 Flutter 商店实现，含远端 API DTO、图标处理、安装流程。
- **`/home/deepin/ui/linyaps`** — Linglong 包管理器本体，含 D-Bus 接口定义、服务实现细节。

> 这两个库**只读参考**，不要修改。

---

## `docs/` 文档（AI 维护）

| 文件 | 内容 |
|------|------|
| `docs/architecture-and-api.md` | 整体架构、公共 API、事件循环模型 |
| `docs/dbus-wire-format.md` | D-Bus `a{sv}` 请求/响应格式、字段名差异 |
| `docs/dbus-policy-and-polkit.md` | 系统总线策略、polkit 授权、测试绕过方案 |
| `docs/icon-source.md` | 图标来源分析（本地无图标，需远端 API 补全） |
| `docs/testing.md` | 构建命令、CLI 命令、真实环境验证 |
| `docs/kilnui-pitfalls.md` | Clay / KilnUI 踩坑记录：floating 渲染顺序、BORDER 命令、uniform 对齐等 |

**AI 更新规则**：当代码中新增接口、修改架构或发现新的 D-Bus 行为时，同步更新对应的 `docs/` 文档和本文件。

---

## 编码规范

- **语言标准**：全部 C23（`-std=c23`）
- **代码格式**：`kilnui/.clang-format`（SDL 风格），后端同风格
- **警告**：`-Wall -Wextra -Wno-unused-parameter`
- **许可证**：后端 LGPL-3.0-or-later，KilnUI MIT
- **内存**：后端无 GC，所有 `LinyapsPackageInfo` 必须调用对应的 `_free` 函数释放
- **线程**：单线程，KilnUI 和后端均不是线程安全的

---

## 已知限制 / 未实现

- **图标**：D-Bus 和 `ll-cli` 均不返回图标字段；当前卡片用占位符，可通过远端 API 补全。
- **未实现**：builder 命令、run/debug helper、仓库管理、桌面快捷方式创建。
- **KilnUI 多窗口**：全局状态（`g_measure_ctx`）设计为单窗口。
