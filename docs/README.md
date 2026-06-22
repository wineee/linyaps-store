# linyaps-store research notes

This directory records the implementation research used to build the
`linyaps-store` C library and its test CLI.

## Documents

- [architecture-and-api.md](architecture-and-api.md)
  - D-Bus architecture, supported GUI-store subset, public C API, event loop
    model and memory ownership.
- [dbus-wire-format.md](dbus-wire-format.md)
  - `a{sv}` request/response shapes, task signals, search job behavior and
    known field-name differences.
- [dbus-policy-and-polkit.md](dbus-policy-and-polkit.md)
  - system D-Bus policy vs polkit, observed install denial and local test
    workaround.
- [icon-source.md](icon-source.md)
  - How `flutter-linglong-store` obtains application icons and what this means
    for a pure local/D-Bus store.
- [testing.md](testing.md)
  - Build commands, CLI commands and real-environment validation notes.
- [kilnui-pitfalls.md](kilnui-pitfalls.md)
  - 踩坑记录：Clay floating 渲染顺序、BORDER 命令未实现、uniform 块对齐、
    attachTo 缺失、渲染命令数量上限、Clay_String 生命周期、分页偏移等。
- [performance-optimizations.md](performance-optimizations.md)
  - 性能优化记录：已安装应用查找优化、鼠标移动重布局、字形纹理图集、
    窗口大小轮询优化、字体大小缓存、字形缓存 Transfer Buffer 优化。
- [id-set-optimization.md](id-set-optimization.md)
  - 已安装应用查找优化详解：从 O(n×m) 到 O(1) 的哈希表实现。
- [code-formatting.md](code-formatting.md)
  - 代码格式化规范：clang-format 配置、SDL 风格规则、格式化历史。

## Current scope

`linyaps-store` intentionally implements only the subset needed by a GUI
application store:

```text
service
list
info
search
install
uninstall
update
cancel
progress
interaction confirmation
```

It does not implement builder commands, run/debug helpers or repository
management commands.
