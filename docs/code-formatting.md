# 代码格式化规范

> 本文档记录 linyaps-store 项目的代码格式化规范和历史。

---

## 格式化工具

- **工具**：clang-format
- **配置文件**：`kilnui/.clang-format`（SDL 风格）
- **适用范围**：整个项目（`lib/`、`ui/`、`tests/`、`kilnui/`）
- **排除目录**：`3rdparty/`（第三方库保留原格式）

---

## 格式化规则摘要

### 基本设置

- **缩进**：4 空格（不使用 Tab）
- **列宽限制**：无限制（`ColumnLimit: 0`）
- **指针对齐**：右侧（`int *ptr`）
- **大括号风格**：SDL 风格（函数后换行，控制语句不换行）

### 宏和枚举

- **宏对齐**：连续对齐（`AlignConsecutiveMacros: Consecutive`）
- **短枚举单行**：允许（`AllowShortEnumsOnASingleLine: true`）

### 函数和控制语句

- **短函数单行**：允许（`AllowShortFunctionsOnASingleLine: All`）
- **短 if 语句单行**：不允许（`AllowShortIfStatementsOnASingleLine: Never`）
- **短循环单行**：不允许（`AllowShortLoopsOnASingleLine: false`）

### 大括号换行

- **函数后**：换行（`AfterFunction: true`）
- **控制语句后**：不换行（`AfterControlStatement: Never`）
- **else 前**：不换行（`BeforeElse: false`）

---

## 格式化历史

### 2026-06-21：整个项目格式化

**提交哈希**：`3a207e4`（主项目）、`8868375`（kilnui 子模块）

**格式化范围**：
- 主项目：31 个文件（`lib/`、`ui/`、`tests/`）
- kilnui 子模块：29 个文件
- 总计：60 个文件，5800 行插入，5348 行删除

**主要调整**：
- 宏定义值右对齐
- 函数参数换行对齐
- 指针声明格式统一（`int *ptr`）
- 大括号位置统一（SDL 风格）
- 缩进统一为 4 空格
- 移除行尾空格

**排除目录**：
- `3rdparty/`（第三方库保留原格式）

---

## 使用方法

### 格式化整个项目

```bash
# 格式化所有 C/H 文件（排除 3rdparty）
find . -path ./3rdparty -prune -o -name "*.c" -o -name "*.h" | grep -v "^./3rdparty" | xargs clang-format -i
```

### 格式化特定目录

```bash
# 格式化 lib/ 目录
clang-format -i lib/*.c lib/*.h

# 格式化 ui/ 目录
clang-format -i ui/*.c ui/*.h ui/views/*.c

# 格式化 tests/ 目录
clang-format -i tests/*.c
```

### 检查格式（不修改）

```bash
# 检查整个项目
find . -path ./3rdparty -prune -o -name "*.c" -o -name "*.h" | grep -v "^./3rdparty" | xargs clang-format --dry-run -Werror

# 检查特定文件
clang-format --dry-run -Werror ui/main.c
```

---

## IDE 集成

### VS Code

1. 安装 C/C++ 扩展
2. 设置中启用格式化：
   ```json
   {
     "C_Cpp.clang_format_path": "/usr/bin/clang-format",
     "editor.formatOnSave": true
   }
   ```

### CLion

1. 设置 → Editor → Code Style → C/C++
2. 选择 "ClangFormat" 作为格式化器
3. 指定配置文件路径：`kilnui/.clang-format`

---

## 注意事项

1. **第三方库**：`3rdparty/` 目录下的代码不格式化，保留原许可证风格
2. **子模块**：`kilnui/` 是 Git 子模块，格式化后需要更新子模块引用
3. **配置文件位置**：配置文件在 `kilnui/.clang-format`，clang-format 会自动向上查找
4. **行宽限制**：设置为 0（无限制），避免破坏宏定义
5. **包含排序**：禁用（`SortIncludes: Never`），避免破坏依赖顺序

---

## 相关文件

- `kilnui/.clang-format` — clang-format 配置文件
- `AGENTS.md` — 项目编码规范
- `docs/performance-optimizations.md` — 性能优化记录
- `docs/kilnui-pitfalls.md` — KilnUI 开发踩坑记录
