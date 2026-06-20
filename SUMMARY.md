# 优化总结：已安装应用查找从 O(n×m) 到 O(1)

## 问题背景

在 `linyaps-store` 应用商店中，渲染应用网格和排行榜时需要判断每个远端应用是否已安装。原来的实现使用嵌套循环进行线性扫描：

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

**问题**：
- 复杂度 O(n×m)，n 为搜索结果数，m 为已安装应用数
- 每帧数千次字符串比较
- 随着应用数量增加，性能急剧下降
- UI 卡顿风险

## 解决方案

引入 **IdSet 哈希表**，实现 O(1) 查找。

### 新代码 (O(n))

```c
// 新代码 (O(n))
for (size_t i = 0; i < search_count; i++) {
    bool inst = id_set_contains(&g_state->installed_id_set, search_results[i]->id);
    // 渲染卡片...
}
```

## 实现细节

### 1. IdSet 数据结构 (`ui/id_set.h/c`)

```c
typedef struct IdSet {
    char  **keys;     // NULL 表示空槽
    size_t  capacity;
    size_t  count;
} IdSet;
```

**特性**：
- 哈希函数：FNV-1a（快速、分布均匀）
- 冲突解决：开放寻址（线性探测）
- 负载因子：上限 0.75，超过时自动扩容
- 内存效率：每个应用 ID 只存储一次

### 2. 集成到 StoreState (`ui/store_state.h`)

```c
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
5. **类型安全**：编译时类型检查，避免运行时错误

## 修改文件列表

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
| `CHANGELOG.md` | 新增：变更日志 |

## 测试结果

```
✓ test_backend      (0.04s)
✓ test_util         (0.01s)
✓ test_filter       (0.00s)
✓ test_id_set       (0.00s)
✓ test_perf_id_set  (0.02s)

All 5 tests passed.
```

## 未来改进

1. **实时更新**：当安装/卸载应用时，自动更新哈希表
2. **批量操作**：支持批量插入和删除
3. **持久化**：可选将哈希表缓存到磁盘，加速启动
4. **监控**：添加性能监控，跟踪哈希表命中率

## 总结

通过引入 IdSet 哈希表，成功将已安装应用查找从 O(n×m) 优化为 O(1)，性能提升 45.4 倍，操作次数减少 1000 倍。这是一个简单而有效的优化，显著提升了 UI 响应速度，特别是当应用数量增加时。
