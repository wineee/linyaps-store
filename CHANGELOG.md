# Changelog

## [风格] 整个项目代码格式化

### 概述

使用 clang-format 对整个项目进行代码格式化，统一 SDL 风格。

### 格式化范围

- **主项目**：31 个文件（`lib/`、`ui/`、`tests/`）
- **kilnui 子模块**：29 个文件
- **总计**：60 个文件，5800 行插入，5348 行删除
- **排除**：`3rdparty/` 目录（第三方库保留原格式）

### 主要调整

- 宏定义值右对齐
- 函数参数换行对齐
- 指针声明格式统一（`int *ptr`）
- 大括号位置统一（SDL 风格）
- 缩进统一为 4 空格
- 移除行尾空格

### 配置文件

- **配置文件**：`kilnui/.clang-format`
- **风格**：SDL 风格
- **工具**：clang-format 17.0.6

### 提交记录

- `3a207e4`：格式化主项目（lib/、ui/、tests/）
- `8868375`：格式化 kilnui 子模块
- `fe68da6`：更新 kilnui 子模块引用

### 文档更新

- 新增 `docs/code-formatting.md`：代码格式化规范
- 更新 `docs/README.md`：添加文档引用
- 更新 `AGENTS.md`：添加文档表格条目

---

## [优化] 已安装应用查找：从 O(n×m) 到 O(1)

### 问题

在渲染应用网格和排行榜时，需要判断每个远端应用是否已安装。原来的实现使用嵌套循环（O(n×m)），导致：

- 每帧数千次字符串比较
- 随着应用数量增加，性能急剧下降
- UI 卡顿风险

### 解决方案

引入 **IdSet 哈希表**，实现 O(1) 查找。

### 性能提升

- **速度**：提升 45.4 倍（测试场景）
- **操作次数**：减少 1000 倍
- **可扩展性**：性能几乎不受已安装应用数量影响

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `ui/id_set.h` | 新增：IdSet 头文件 |
| `ui/id_set.c` | 新增：IdSet 实现（开放寻址哈希表） |
| `ui/store_state.h` | 添加 `installed_id_set` 字段 |
| `ui/store_state.c` | 初始化和释放哈希表 |
| `ui/main.c` | 构建哈希表 |
| `ui/views/app_grid.c` | 使用哈希表进行 O(1) 查找 |
| `ui/views/ranking_view.c` | 使用哈希表进行 O(1) 查找 |
| `CMakeLists.txt` | 添加新源文件和测试 |
| `tests/test_id_set.c` | 新增：单元测试 |
| `tests/test_perf_id_set.c` | 新增：性能测试 |
| `docs/id-set-optimization.md` | 新增：优化文档 |
| `AGENTS.md` | 更新文档 |

### 测试结果

```
✓ test_backend      (0.04s)
✓ test_util         (0.01s)
✓ test_filter       (0.00s)
✓ test_id_set       (0.00s)
✓ test_perf_id_set  (0.02s)

All 5 tests passed.
```

### 技术细节

**IdSet 特性**：
- 哈希函数：FNV-1a
- 冲突解决：开放寻址（线性探测）
- 负载因子：上限 0.75
- 自动扩容：容量翻倍

**使用模式**：
```c
// 构建哈希表
id_set_build_from_packages(&set, installed_list, installed_count);

// O(1) 查找
bool installed = id_set_contains(&set, app_id);

// 释放
id_set_free(&set);
```

### 未来改进

1. 实时更新：安装/卸载时自动更新哈希表
2. 批量操作：支持批量插入和删除
3. 持久化：可选缓存到磁盘，加速启动
