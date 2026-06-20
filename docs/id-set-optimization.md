# 已安装应用查找优化：从 O(n×m) 到 O(1)

## 问题描述

在 `view_app_grid()` 和 `view_ranking()` 中，需要判断每个远端应用是否已安装。原来的实现使用嵌套循环：

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

**复杂度分析**：
- 假设有 30 个搜索结果（n=30），41 个已安装应用（m=41）
- 每帧需要 30 × 41 = 1,230 次字符串比较
- 如果搜索结果更多（如 100 个），则需要 4,100 次比较

## 解决方案

使用**开放寻址哈希表**（`IdSet`）存储已安装应用的 ID，实现 O(1) 查找。

### 新代码 (O(n))

```c
// 新代码 (O(n))
for (size_t i = 0; i < search_count; i++) {
    bool inst = id_set_contains(&g_state->installed_id_set, search_results[i]->id);
    // 渲染卡片...
}
```

**复杂度分析**：
- 每帧只需 30 次哈希查找
- 每次查找平均 O(1) 时间

## 实现细节

### 1. IdSet 数据结构

```c
// ui/id_set.h
typedef struct IdSet {
    char  **keys;     // NULL 表示空槽
    size_t  capacity;
    size_t  count;
} IdSet;
```

- **哈希函数**：FNV-1a（快速、分布均匀）
- **冲突解决**：开放寻址（线性探测）
- **负载因子**：上限 0.75，超过时自动扩容

### 2. 集成到 StoreState

```c
// ui/store_state.h
typedef struct {
    // ...
    LinyapsPackageInfo **installed_list;
    size_t               installed_count;
    IdSet                installed_id_set;  // 新增
    // ...
} StoreState;
```

### 3. 生命周期管理

- **初始化**：`store_state_init()` 中调用 `id_set_init()`
- **构建**：在 `main.c` 中调用 `id_set_build_from_packages()`
- **释放**：`store_state_free()` 中调用 `id_set_free()`

### 4. 使用位置

- `ui/views/app_grid.c`：应用网格卡片
- `ui/views/ranking_view.c`：排行榜卡片

## 性能测试结果

```
Performance comparison: IdSet vs Linear Scan
============================================
Installed apps: 1000
Search results: 1000
Queries:        10000

Results:
  IdSet:      0.336 ms (10000 found)
  Linear:     15.271 ms (10000 found)
  Speedup:    45.4x faster

Complexity analysis:
  Old (linear): O(n × m) = 1000 × 1000 = 1000000 operations per frame
  New (hash):   O(n) = 1000 operations per frame
  Reduction:    1000.0x fewer operations
```

## 优势

1. **性能提升**：45.4 倍速度提升（测试场景）
2. **可扩展性**：当已安装应用数量增加时，性能几乎不受影响
3. **内存效率**：哈希表内存开销小，每个应用 ID 只存储一次
4. **代码简洁**：一行代码替代嵌套循环

## 未来改进

1. **实时更新**：当安装/卸载应用时，自动更新哈希表
2. **批量操作**：支持批量插入和删除
3. **持久化**：可选将哈希表缓存到磁盘，加速启动

## 相关文件

- `ui/id_set.h`：IdSet 头文件
- `ui/id_set.c`：IdSet 实现
- `ui/store_state.h`：StoreState 定义（包含 IdSet）
- `ui/main.c`：哈希表构建逻辑
- `ui/views/app_grid.c`：应用网格使用示例
- `ui/views/ranking_view.c`：排行榜使用示例
- `tests/test_id_set.c`：单元测试
- `tests/test_perf_id_set.c`：性能测试
