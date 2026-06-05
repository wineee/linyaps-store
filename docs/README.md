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
