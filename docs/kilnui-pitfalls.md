# KilnUI / Clay 开发踩坑记录

> **适用范围**：`kilnui/` 渲染层、`kilnui/src/ui/` Widget 层、`ui/store_ui.c` Clay 布局层。  
> 按踩坑时间先后顺序记录，每条都包含症状、根因、修复方法。

---

## 1. Clay Floating 元素永远在普通子元素之后渲染

### 症状

给 `UI_BTN_SECONDARY` 按钮添加了一个 floating border overlay 子元素（`CLAY_ATTACH_TO_PARENT`），结果按钮文字消失，只看到一个实心灰色方块。无论把 `zIndex` 设为 `0`、`-1` 还是 `-100` 都无法让文字显示出来。同时按钮无法点击（`Clay_PointerOver` 检测被覆盖层吸收）。

### 根因

Clay 的渲染命令数组（`Clay_RenderCommandArray`）的生成顺序是：

```
普通子元素（按树序 depth-first） → floating 元素（按 zIndex 排序，统一追加在末尾）
```

`zIndex` 只决定**多个 floating 元素之间**的顺序，无法让 floating 元素排在普通非浮动元素（TEXT）之前。所以 CUSTOM floating border 的渲染命令永远在 TEXT 命令之后，必然覆盖文字。

### 修复

**不要用 floating 子元素做边框**。改用 Clay 原生的 `.border` 字段：

```c
Clay_BorderElementConfig border_cfg = {0};
if (variant == UI_BTN_SECONDARY) {
    uint16_t bw = pressed ? 2 : 1;
    border_cfg = (Clay_BorderElementConfig){
        .color = ds_theme->surface2,
        .width = { .left = bw, .right = bw, .top = bw, .bottom = bw }
    };
}

CLAY(id, {
    ...
    .border = border_cfg,   // ← 原生边框，BORDER 命令在 TEXT 之前生成
})
```

原生 `.border` 的渲染命令（`CLAY_RENDER_COMMAND_TYPE_BORDER`）在树序中、TEXT 命令之前出现，因此文字永远显示在边框上层。

### 教训

> **规则**：凡是需要显示在内容（文字/图标）**下方**的装饰元素（边框、阴影背景等），必须使用 Clay 原生配置字段（`.border`、`.backgroundColor` 等），而不是 floating 子元素。只有确定要显示在**所有内容上方**的浮层（遮罩、Tooltip、下拉菜单）才应使用 floating。

---

## 2. KilnUI 渲染器未实现 `CLAY_RENDER_COMMAND_TYPE_BORDER`

### 症状

使用 Clay 原生 `.border` 字段后，边框完全不显示——按钮看起来和没有边框一样，只有背景色。

### 根因

`kilnui_render.c` 的渲染 switch 语句只处理了：

```c
case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
case CLAY_RENDER_COMMAND_TYPE_TEXT:
case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
```

`CLAY_RENDER_COMMAND_TYPE_BORDER` 落入 `default: break`，直接被忽略。

### 修复

在 `kilnui_render.c` 的 pre-pass 和 render-pass 两处同时添加 BORDER 处理：

**pre-pass**（分配 rect slot）：

```c
} else if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_BORDER) {
    int ri = s_rect_count++;
    cmd_to_rect[i] = ri;
    Clay_BoundingBox bb = cmd->boundingBox;
    Clay_Color c = cmd->renderData.border.color;
    Clay_CornerRadius cr = {0};  // BORDER render data 不携带 cornerRadius
    push_rect_at(ri, bb.x, bb.y, bb.width, bb.height, c, cr, scale);
}
```

**render-pass**（用 `pipeline_border` 绘制）：

```c
case CLAY_RENDER_COMMAND_TYPE_BORDER: {
    Clay_BorderRenderData *bd = &cmd->renderData.border;
    float udata[8] = {
        (float)bd->width.top * scale,
        (float)bd->width.right * scale,
        (float)bd->width.bottom * scale,
        (float)bd->width.left * scale,
        0.0f, 0.0f, 0.0f, 0.0f
    };
    SDL_PushGPUFragmentUniformData(cmdbuf, 0, udata, sizeof(udata));
    SDL_DrawGPUIndexedPrimitives(rp, 6, 1, (Uint32)(ri * 6), 0, 0);
    break;
}
```

### 当前状态：⚠️ 未完全解决（已有 workaround）

`Clay_BorderRenderData` 不携带 `cornerRadius`，无法直接获取。

**尝试过但放弃的方案**：在 pre-pass 向前查找 bounding box 相同的 `RECTANGLE` 命令来借用其 `cornerRadius`。该方案在简单场景下有效，但当元素没有 `backgroundColor`（不生成 RECTANGLE）或 scissor/floating 命令在两者之间插入时会读到错误数据，不够可靠。

**当前 workaround**：`UI_BTN_SECONDARY` 不使用 `.border` 字段，改为使用更醒目的背景色（`surface2` / `overlay0`）与其他变体区分视觉。

**TODO**（`kilnui_render.c` 和 `kilnui/src/ui/button.c` 中均有注释）：  
找到可靠的方式将 `cornerRadius` 传递给 BORDER 渲染命令。可能的思路：
- 在 Clay 自定义扩展中扩展 `Clay_BorderRenderData` 结构体（需要修改 clay.h）
- 用单独的全局哈希表按 element ID 缓存 cornerRadius，RECTANGLE 时写入、BORDER 时读取

---

## 3. GLSL `uniform` 块字段顺序与 C 结构体不对齐

### 症状

secondary 按钮的边框渲染为实心色块（整个按钮区域填充边框色），而非仅显示边框线。

### 根因

原始 `border.frag.glsl` 的 uniform 块用 `vec4` 聚合四个方向的宽度：

```glsl
layout(set = 3, binding = 0) uniform BorderUniforms {
    vec4  widths;      // top, right, bottom, left
    float dashLength;
    float dashGap;
} border;
```

而 C 侧推送的数据是 6 个独立 `float`（通过 `SDL_PushGPUFragmentUniformData`）：

```c
float udata[8] = { top, right, bottom, left, dashLen, dashGap, 0, 0 };
```

`vec4` 在 std140 布局下占 16 字节（4×4），独立 `float` 占 4 字节。C 侧推送的 `top` 对应 GLSL 的 `widths.x`，但 `right` 对应的是 `widths.y`……实际上由于对齐，`widths` 读取的是 `[top, right, bottom, left]` 拼成的 16 字节——这恰好**正确**，但之后的 `dashLength` 和 `dashGap` 各自占 4 字节，总计 `16 + 4 + 4 = 24` 字节，与 `float[8] = 32` 字节不匹配，导致字段读错位。

实际表现：`widths` 四个分量被赋为正确值，但 `innerHalfSize` 计算时读到了垃圾值，造成 inner SDF 完全消失（内部全被裁剪为 0），边框变成填满整个矩形的实心块。

### 修复

将 `vec4 widths` 拆成 4 个独立 `float`，使 GLSL 布局与 C 侧完全匹配：

```glsl
layout(set = 3, binding = 0) uniform BorderUniforms {
    float top;
    float right;
    float bottom;
    float left;
    float dashLength;
    float dashGap;
} border;
```

### 教训

> **规则**：向 GPU push fragment uniform 时，GLSL 结构体的字段布局必须与 C 侧的字节序列完全一致。`vec4` 在 std140 下是 16 字节对齐，容易与 C 侧的 `float[4]` 产生隐式偏移。**优先使用独立 `float` 字段**，避免 `vec2`/`vec4` 聚合带来的对齐问题，除非你明确理解 std140 的填充规则。

---

## 4. Clay floating 元素缺少 `attachTo` 导致渲染为普通子元素

### 症状

为按钮添加了 shadow floating 子元素，但实际渲染时 shadow 出现在按钮容器内的左侧，占据布局空间，把文字挤到右边，看起来像是一个深色方块贴在文字左边。

### 根因

```c
CLAY(CLAY_ID_LOCAL("shadow"), {
    .floating = {
        .attachPoints = { ... },
        .zIndex = -1
        // 缺少 .attachTo
    },
    ...
})
```

Clay 的 floating 元素**必须设置 `.attachTo`** 才能激活浮动行为。若 `.attachTo` 为默认值（`CLAY_ATTACH_TO_NONE`），该元素退化为普通子元素，参与父容器的正常 flow 布局。

### 修复

```c
.floating = {
    .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER,
                      .parent  = CLAY_ATTACH_POINT_CENTER_CENTER },
    .zIndex  = -1,
    .attachTo = CLAY_ATTACH_TO_PARENT,  // ← 必须设置
},
```

### 教训

> **规则**：创建任何 floating 元素时，`.floating.attachTo` 是**必填字段**。缺少它等价于没有写 `.floating` 配置。参见 `clay.h` 注释：  
> *"Note: in order to activate floating, `.floating.attachTo` must be set to something other than the default value."*

---

## 5. Clay 渲染命令数量上限导致后半部分界面不渲染

### 症状

应用列表网格中，超过一定数量的卡片（约 20 张）之后的内容完全消失，既无背景也无文字。

### 根因

`kilnui_render.c` 中的静态上限常量过低：

```c
#define MAX_CMDS  8192
#define MAX_RECTS 4096
```

在渲染多张卡片（每张含背景矩形、图标占位、文字、按钮等多个 Clay 元素）时，每帧生成的命令数轻易超过 `MAX_CMDS`，超出部分被截断（`cmds.length > MAX_CMDS` 时记录警告但仍继续）。

### 修复

根据实际场景调大上限：

```c
#define MAX_CMDS  16384   // 每帧最大渲染命令数
#define MAX_RECTS 8192    // 最大矩形顶点槽
```

同时，Clay 自身的内存池也需要相应调大（在 `kilnui.c` 初始化时传入的 `Clay_MinMemorySize()` 和 `Clay_Initialize()` 参数）：

```c
// kilnui.c 初始化
Clay_SetMaxElementCount(8192);
```

### 教训

> **规则**：开发初期将 Clay 元素数量、渲染命令数量相关的所有上限都设大一个数量级，否则渲染截断问题会伪装成"布局 bug"而难以排查。可以通过检查日志中的 `"command count X exceeds MAX_CMDS"` 来确认是否触发截断。

---

## 6. Clay `Clay_StringSlice` 不以 null 结尾，生命周期须注意

### 症状

渲染应用列表时，某些 APP 名称显示为乱码或截断为其他字符串的内容。在翻页后症状随机变化。

### 根因

`Clay_String`（即 `Clay_StringSlice`）**不保证 null 结尾**，仅用 `.length` 标记字符串长度。在 `store_ui.c` 中直接用栈上临时 `char[]` 格式化字符串后传给 `UI__str()` / `CLAY_TEXT()`：

```c
char buf[64];
snprintf(buf, sizeof(buf), "%s v%s", info->name, info->version);
CLAY_TEXT(CLAY_STRING(buf), ...);  // ← buf 在下一帧/下一循环后失效！
```

`CLAY_STRING(buf)` 只保存指针，不复制内容。Clay 在布局计算阶段读取字符串时，栈帧已经退出，指针指向垃圾数据。

### 修复

使用**帧内字符串 arena**：每帧在固定静态缓冲区中顺序分配字符串，下一帧重置。

```c
/* store_ui.c */
static char  s_str_arena[65536];
static size_t s_str_arena_pos = 0;

/* 每帧渲染开始时重置 */
s_str_arena_pos = 0;

/* 格式化时从 arena 分配 */
static const char *store_ui_frame_str(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    size_t remaining = sizeof(s_str_arena) - s_str_arena_pos;
    int n = vsnprintf(s_str_arena + s_str_arena_pos, remaining, fmt, ap);
    va_end(ap);
    const char *result = s_str_arena + s_str_arena_pos;
    s_str_arena_pos += (size_t)(n + 1);
    return result;
}
```

### 教训

> **规则**：所有传给 `CLAY_TEXT` / `UI__str()` 的字符串，其生命周期必须覆盖整个 Clay `BeginLayout → EndLayout → Render` 周期。栈上临时变量、函数返回的局部 `char[]` 都不满足条件。使用帧内 arena 或确保字符串来自持久化存储（`info->name` 等常驻指针）。

---

## 7. 推荐页加载圈一直转：`current_cat` 状态未同步

### 症状

切换到「推荐」侧边栏导航项后，页面显示旋转加载指示器，永远不停止，即使远端 API 已经返回数据并打印了日志。

### 根因

`ui/main.c` 在处理后端回调时，用 `current_cat`（当前分类 ID）来判断数据是否属于当前页面：

```c
if (strcmp(fetched_cat, g_state->current_cat) == 0) {
    // 更新数据，停止加载圈
}
```

但在切换到 NAV_RECOMMENDED 时，`g_state->current_cat` 没有被设置为特殊标识符 `"__welcome__"`（推荐 API 的内部 category key）。`current_cat` 仍然是上一次全部应用的分类 ID，导致推荐数据的回调比较字符串时永远不匹配，加载圈永远不停。

### 修复

在切换导航到 `NAV_RECOMMENDED` 时同步设置 `current_cat`：

```c
case NAV_RECOMMENDED:
    free(g_state->current_cat);
    g_state->current_cat = strdup("__welcome__");
    break;
```

### 教训

> **规则**：凡是用字符串 key 关联"发出请求"和"处理回调"的模式，都必须确保两侧的 key 在所有状态分支下严格一致。推荐在常量文件中定义这些 key 字符串而不是分散写字符串字面量。

---

## 8. 分类 Tab 在推荐页不应显示

### 症状

切换到「推荐」导航项后，页面顶部仍然显示「全部 / 办公 / 系统 / 开发 / 娱乐」分类 Tab 栏，但推荐页的数据来源（`linyaps_remote_fetch_welcome_apps`）与分类无关。

### 根因

`store_ui.c` 中的分类 Tab 渲染逻辑没有根据 `active_nav` 条件化：

```c
/* 错误写法：无论什么页面都渲染 Tab 栏 */
render_category_tabs(g_state);
```

### 修复

在 Tab 渲染前检查当前导航项：

```c
if (g_state->active_nav == NAV_ALL || g_state->active_nav == NAV_CATEGORY) {
    render_category_tabs(g_state);
}
```

---

## 9. 远端 API 推荐页与全部页使用不同端点

### 观察

开发时发现「推荐」页和「全部」页都调用了相同的 `linyaps_remote_fetch_apps()` 函数，导致推荐页显示的是普通搜索结果而非编辑精选内容。

### 正确 API 端点对应关系

| 页面 | 函数 | 端点 |
|------|------|------|
| 推荐 | `linyaps_remote_fetch_welcome_apps()` | `GET /api/v1/apps?category=welcome` |
| 全部/分类 | `linyaps_remote_fetch_apps()` | `GET /api/v1/apps?category=<id>&keyword=<kw>` |
| 排行 | `linyaps_remote_fetch_ranking()` | `GET /api/v1/apps?sort=newest` 或 `sort=downloads` |

参考 Flutter 实现：`/home/deepin/ui/flutter-linglong-store/lib/data/`。

---

## 10. 分页：`current_page` 初始值与 API 页码的偏移

### 症状

「下一页」翻页后，服务器返回的数据与页面显示的「第 N 页」对不上。有时显示「第 2 页」但拿到的是第 1 页数据。

### 根因

UI 侧 `current_page` 是 0-indexed（第一页 = 0），远端 API 的 `page` 参数是 1-indexed（第一页 = 1）。调用时忘记加 1：

```c
/* 错误 */
linyaps_remote_fetch_apps(..., g_state->current_page, ...);

/* 正确 */
linyaps_remote_fetch_apps(..., g_state->current_page + 1, ...);
```

### 修复

统一在调用远端 API 处加 `+ 1` 转换，UI 内部始终保持 0-indexed。在 `linyaps_remote.c` 中的参数名也应注释为 1-indexed。

---

## 11. 多个 UI_Input 互相干扰导致键盘输入失效

### 症状

组件画廊（`component_gallery`）中，点击 "Click to focus name" 输入框后，键盘输入无效。`SDL_EVENT_TEXT_INPUT` 事件完全不产生。而单独的 `input_demo`（只有一个输入框）正常工作。

### 根因

`UI_Input` 内部用**全局静态变量** `s_text_input_active` 跟踪 SDL 文本输入状态。当页面有多个 `UI_Input` 时，每帧按顺序调用：

```
frame N:
  UI_Input(name)  focused=true  → SDL_StartTextInput() → s_text_input_active = true
  UI_Input(email) focused=false → SDL_StopTextInput()  → s_text_input_active = false  ← 把 name 刚开的关掉了！

frame N+1:
  UI_Input(name)  focused=true  → SDL_StartTextInput() → s_text_input_active = true
  UI_Input(email) focused=false → SDL_StopTextInput()  → s_text_input_active = false
  ... 每帧重复
```

结果：`SDL_StartTextInput` 每帧都被调用，但下一帧又被另一个输入框的 `StopTextInput` 关掉，`SDL_EVENT_TEXT_INPUT` 永远不会产生。

### 修复

用 `uid` 跟踪**哪个输入框持有焦点**，只有焦点持有者才能控制 SDL 文本输入：

```c
static int  s_focused_uid = -1;  /* -1 = 没有输入框持有焦点 */
static bool s_text_input_active = false;

// UI_Input 内部：
bool new_focused = clicked ? true : focused;
if (new_focused && s_focused_uid != uid) {
    s_focused_uid = uid;  /* 抢占焦点 */
}
bool is_owner = (s_focused_uid == uid);

if (new_focused && is_owner && !s_text_input_active) {
    SDL_StartTextInput(window);
    s_text_input_active = true;
} else if (!new_focused && s_focused_uid == uid) {
    s_focused_uid = -1;  /* 释放焦点 */
    if (s_text_input_active) {
        SDL_StopTextInput(window);
        s_text_input_active = false;
    }
}
```

### 事件处理时序陷阱

此外，`SDL_EVENT_TEXT_INPUT` 事件在**事件循环**中到达，但输入框的焦点状态在 `ui_build()`（事件循环之后）才更新。因此 TEXT_INPUT 事件到达时，焦点标志可能还是旧值。

**解决**：将 TEXT_INPUT 和 KEY_DOWN 事件放入队列，在 `ui_build()` 之后处理：

```c
/* 事件循环中：只排队 */
} else if (e.type == SDL_EVENT_TEXT_INPUT) {
    text_queue[len++] = e.text.text;
}

/* ui_build() 之后：处理队列（此时焦点状态已更新） */
for (int i = 0; i < len; i++) {
    if (g_focused) append_to_buffer(text_queue[i]);
}
```

### 教训

> **规则**：当页面有多个可聚焦 Widget 且共享全局状态（如 `s_text_input_active`）时，必须用 **owner id** 跟踪谁持有焦点，避免其他 Widget 意外修改全局状态。另外，**事件处理**（在事件循环中）和**状态更新**（在 UI 布局中）存在时序差，需要排队延迟处理。

---

## 参考

- `kilnui/3rdparty/clay/clay.h` — floating 元素行为、`Clay_BorderElementConfig`
- `kilnui/src/kilnui_render.c` — 渲染命令 switch 的完整实现
- `kilnui/shaders/border.frag.glsl` — 边框 SDF shader（uniform 布局）
- `kilnui/src/ui/button.c` — 按钮 Widget 实现
- `ui/store_ui.c` — 帧内字符串 arena、页面导航状态
