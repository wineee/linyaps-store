# linyaps-store

A native app store for [Linglong](https://linglong.dev) package manager, written in C23.

## Architecture

```
┌─────────────────────────────────────────┐
│            UI Layer (ui/)               │
│  Clay layout + KilnUI components        │
├──────────────┬──────────────────────────┤
│  lib/        │  kilnui/                 │
│  D-Bus API   │  GPU renderer (SDL3)     │
└──────────────┴──────────────────────────┘
```

- **`lib/`** — Communicates with `org.deepin.linglong.PackageManager1` via sd-bus. Provides install, uninstall, search, update operations.
- **`kilnui/`** — UI rendering framework built on Clay layout + SDL3 GPU, with a design system and component library.
- **`ui/`** — Application layer that assembles the store interface from backend APIs and KilnUI components.

## Features

- App browsing (recommended, all categories, category tabs)
- App search (remote API)
- Rankings (newest, most downloaded)
- Install / uninstall apps
- Update check & upgrade (batch API + D-Bus)
- Dark / light theme toggle

## Dependencies

```bash
# Build tools
cmake >= 3.20
gcc >= 13 (C23 support)

# Libraries
libsystemd-dev    # sd-bus
libcurl           # remote API client
libsdl3           # window & GPU
libsdl3-ttf       # font rendering
glslc             # GLSL → SPIR-V shader compiler
```

## Build

```bash
cmake -B build
cmake --build build
```

## Run

```bash
# Launch the store (requires linglong daemon on system bus)
./build/linyaps_store_app

# CLI test tool
./build/linyaps_cli

# Unit tests
ctest --test-dir build
```

## Project Structure

```
linyaps-store/
├── lib/                    # Backend C library
│   ├── linyaps_backend.h   # Public API
│   ├── linyaps_types.h     # Data structures
│   ├── linyaps_context.c   # D-Bus connection & event dispatch
│   ├── linyaps_remote.c    # Remote store HTTP API
│   └── linyaps_cli.c       # ll-cli command wrapper
├── ui/                     # GUI application
│   ├── main.c              # Event loop entry point
│   ├── store_state.h/c     # Global application state
│   ├── store_ui.h/c        # Clay layout composition
│   └── views/              # Page views
├── kilnui/                 # UI rendering library (submodule)
├── 3rdparty/cjson/         # cJSON (vendored)
├── tests/                  # Tests
└── docs/                   # Documentation
```

## License

[MIT](LICENSE)
