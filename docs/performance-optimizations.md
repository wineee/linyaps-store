# 性能优化记录

> 本文档记录 linyaps-store 项目的性能优化历史，按时间顺序排列。

---

## 1. 已安装应用查找：O(n×m) → O(1)

**提交哈希**：`6d3636b`

### 问题

在 `view_app_grid()` 和 `view_ranking()` 中，需要判断每个远端应用是否已安装。原来的实现使用嵌套循环进行线性扫描，复杂度为 O(n×m)。

```c
// 旧代码 (O(n×m))
for (size_t i = 0; i < search_count; i++) {
    bool inst = false;
    for (size_t k = 0; k < installed_count; k++) {
        if (strcmp(installed_list[k]->id, search_results[i]->id) == 0) {
            inst = true;
            break;
        }
    }
    // 渲染卡片...
}
```

### 解决方案

引入 `IdSet` 哈希表（开放寻址 + FNV-1a），实现 O(1) 查找。

```c
// 新代码 (O(1))
bool inst = id_set_contains(&g_state->installed_id_set, list[i]->id);
```

### 性能提升

- 速度提升：**39.8 倍**
- 操作次数减少：**1000 倍**（1000×1000 → 1000）

### 相关文件

- `ui/id_set.h/c` — 哈希表实现
- `ui/store_state.h` — 添加 `installed_id_set` 字段
- `ui/views/app_grid.c` — 使用哈希表
- `ui/views/ranking_view.c` — 使用哈希表

---

## 2. 鼠标移动每帧触发全量重布局

**提交哈希**：`2b25d4c`

### 问题

`handle_sdl_event` 中每个事件都设置 `dirty = 1`，鼠标移动等频繁事件会每帧触发全量重布局。

```c
// 旧代码
SDL_SetAtomicInt(&store->dirty, 1);  // 每个事件都设 dirty
if (e->type == SDL_EVENT_MOUSE_MOTION) {
    input->mx = e->motion.x;
    input->my = e->motion.y;
}
```

### 解决方案

只在真正需要重布局的事件上设置 `dirty`，鼠标移动只更新位置标记。

```c
// 新代码
switch (e->type) {
case SDL_EVENT_MOUSE_MOTION:
    input->mx = e->motion.x;
    input->my = e->motion.y;
    input->mouse_moved = true;  // 不设 dirty
    break;
case SDL_EVENT_MOUSE_BUTTON_DOWN:
case SDL_EVENT_KEY_DOWN:
    SDL_SetAtomicInt(&store->dirty, 1);
    break;
}

// 主循环中检测位置变化
if (input.mouse_moved && (input.mx != prev_mx || input.my != prev_my)) {
    SDL_SetAtomicInt(&store.dirty, 1);
    prev_mx = input.mx;
    prev_my = input.my;
}
```

### 效果

- 鼠标静止时零 CPU 开销
- 只在位置变化时触发重布局

---

## 3. 字形渲染：纹理图集

**提交哈希**：`cbadaa7`（kilnui 子模块）

### 问题

每个字形一次 Draw Call，一个文字命令如果有 20 个字，就是 20 次 draw call。

```c
// 旧代码：每个字形一次 draw call
for (int q = 0; q < tb->quad_count; q++) {
    SDL_BindGPUFragmentSamplers(rp, 0, ...);  // 每个字形换一次纹理
    SDL_DrawGPUIndexedPrimitives(rp, 6, 1, ...);  // 每个字形一次绘制
}
```

### 解决方案

使用字形纹理图集（texture atlas），将所有字形合并到一张大纹理。

```c
// 新代码：使用纹理图集，只需 1 次 draw call
if (tb->uses_atlas) {
    SDL_BindGPUFragmentSamplers(rp, 0,
        &(SDL_GPUTextureSamplerBinding){
            .texture = GlyphAtlas_get_texture(&ctx->glyph_atlas),
            .sampler = ctx->sampler_linear }, 1);
    SDL_DrawGPUIndexedPrimitives(rp, 6 * tb->quad_count, 1, ...);
}
```

### 实现细节

- 纹理图集大小：2048×2048 像素
- 打包算法：Shelf（Next Fit Decreasing Height）
- 自动计算 UV 坐标
- 批量上传所有字形到 GPU

### 效果

- Draw Call：N 次/文字命令 → **1 次/文字命令**
- GPU 状态切换：N 次 → **1 次**

### 相关文件

- `kilnui/src/glyph_cache_atlas.h/c` — 纹理图集实现
- `kilnui/src/kilnui_render.c` — 渲染逻辑

---

## 4. 窗口大小轮询改为事件驱动

**提交哈希**：`f9fb53e`

### 问题

每帧都调用 `SDL_GetWindowSizeInPixels` 检测窗口大小变化，这是不必要的轮询。

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
```

### 解决方案

改为事件驱动，只在 `SDL_EVENT_WINDOW_RESIZED` 和 `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` 事件中更新。

```c
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

### 效果

- 减少每帧不必要的系统调用
- 只在窗口大小实际变化时更新

---

## 5. 字体大小缓存优化

**提交哈希**：`2a6a819`（kilnui 子模块）

### 问题

`s_last_phys_size` 在每帧开头被重置为 0，导致每帧第一个文字命令必然触发一次字体大小设置（`TTF_SetFontSize` 有系统调用开销）。

```c
// 旧代码：每帧重置
void KilnUI_render(...) {
    s_last_phys_size = 0;  // ← 每帧重置
    ...
}
```

### 解决方案

移除每帧重置，让缓存保留上一帧的值。

```c
// 新代码：保留上一帧的值
void KilnUI_render(...) {
    // 不重置 s_last_phys_size：让缓存保留上一帧的值
    // measure_text_cb 在布局阶段使用逻辑像素大小，
    // build_text_batch 使用物理像素大小，两者不同
    // set_font_size 会在需要时自动更新
    ...
}
```

### 效果

- 减少每帧不必要的 `TTF_SetFontSize` 调用
- 提升渲染性能

---

## 总结

| 优化 | 提交哈希 | 效果 |
|------|----------|------|
| 已安装应用查找 | `6d3636b` | O(n×m) → O(1)，39.8x 提速 |
| 鼠标移动重布局 | `2b25d4c` | 鼠标静止时零 CPU 开销 |
| 字形纹理图集 | `cbadaa7` | Draw Call N → 1 |
| 窗口大小轮询 | `f9fb53e` | 事件驱动替代轮询 |
| 字体大小缓存 | `2a6a819` | 跨帧缓存 TTF_SetFontSize |
