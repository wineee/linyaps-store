# D-Bus wire format and behavior

## Interfaces

Package manager:

```text
Destination: org.deepin.linglong.PackageManager1
Path:        /org/deepin/linglong/PackageManager1
Interface:   org.deepin.linglong.PackageManager1
```

Task:

```text
Interface: org.deepin.linglong.Task1
```

## PackageManager1 methods used

```text
Search(a{sv})    -> a{sv}
Install(a{sv})   -> a{sv}
Uninstall(a{sv}) -> a{sv}
Update(a{sv})    -> a{sv}
Prune()          -> a{sv}
```

Read-only property:

```text
Configuration: a{sv}
```

Signals:

```text
TaskAdded(o)
TaskRemoved(o)
SearchFinished(s jobID, a{sv} result)
```

## Task1 methods and signals

Methods:

```text
Cancel()
ReplyInteraction(a{sv})
```

Properties:

```text
State:      i
Percentage: d
Message:    s
Code:       i
```

Signal:

```text
RequestInteraction(i messageID, a{sv} additionalMessage)
```

`linyaps-store` subscribes to `PropertiesChanged` and `RequestInteraction` per
task object path.

## QVariantMap to D-Bus mapping

Linglong serializes C++ API structs through Qt `QVariantMap`, which appears on
D-Bus as `a{sv}`.

Observed mapping:

```text
string          -> variant("s")
integer number  -> variant("x")
floating number -> variant("d")
boolean         -> variant("b")
array<string>   -> variant("as")
array<variant>  -> variant("av")
object/map      -> variant("a{sv}")
```

## Install parameters

Current implementation sends:

```text
a{sv}:
  "package" -> variant("a{sv}"):
    "id"                  -> variant("s")
    "version"             -> variant("s") optional
    "channel"             -> variant("s")
    "packageInfoV2Module" -> variant("s") "binary"
  "options" -> variant("a{sv}"):
    "force"           -> variant("b") false
    "skipInteraction" -> variant("b") false
```

Important correction: the real field in current Linglong sources is
`skipInteraction`, not `confirmInteraction`.

## Uninstall parameters

```text
a{sv}:
  "package" -> variant("a{sv}"):
    "id"      -> variant("s")
    "version" -> variant("s") optional
    "channel" -> variant("s")
  "options" -> variant("a{sv}"):
    "force"           -> variant("b") false
    "skipInteraction" -> variant("b") false
```

## Update parameters

`ll-cli` only needs the package id:

```text
a{sv}:
  "package" -> variant("a{sv}"):
    "id" -> variant("s")
  "options" -> variant("a{sv}"):
    empty map
```

The library avoids adding channel/force for update.

## Search behavior

Initial research assumed `Search` synchronously returned package results. Real
daemon behavior is different:

1. `Search(a{sv})` returns `PackageManager1JobInfo` with a job id.
2. The result arrives later through `SearchFinished(jobID, result)`.

Request:

```text
a{sv}:
  "id"    -> variant("s")  keyword
  "repos" -> variant("as") repo aliases/names
```

`repos` must not be empty. The daemon loops over `params.repos`; an empty array
means zero repositories and therefore zero results.

When `repos == NULL`, `linyaps-store` reads `Configuration.repos` and expands:

```text
repo.alias if present, otherwise repo.name
```

Fallback is `"stable"` if configuration cannot be read.

Search result:

```text
a{sv}:
  "code"     -> variant("x")
  "message"  -> variant("s")
  "packages" -> variant("a{sv}"):
    repo_name -> variant("av"):
      variant("a{sv}") PackageInfoV2
```

`linyaps-store` applies the same default store-facing filters as `ll-cli`:

```text
filter packageInfoV2Module == "develop"
keep only the newest version for repo + id + module
```

## Info behavior

There is no PackageManager1 D-Bus `Info` method. `ll-cli info` reads local layer
metadata from the local repository.

`linyaps-store` currently uses:

```bash
ll-cli --json info <app_id>
```

Example JSON:

```json
{
  "arch": ["x86_64"],
  "base": "main:org.deepin.base/25.2.2/x86_64",
  "channel": "main",
  "command": ["deepin-draw"],
  "description": "Draw for deepin os.\n",
  "id": "org.deepin.draw",
  "kind": "app",
  "module": "binary",
  "name": "deepin-draw",
  "runtime": "main:org.deepin.runtime.dtk/25.2.2/x86_64",
  "schema_version": "1.0",
  "size": 55138038,
  "version": "6.5.38.1"
}
```

## Installed list behavior

There is no PackageManager1 D-Bus `List` method. `ll-cli list --json` reads local
repository state and formats it.

`linyaps-store` currently uses:

```bash
ll-cli list --json
```

The output is an array of objects with fields similar to `info`.

## Task flow

Package operations return quickly. Real progress comes through task signals:

```text
linyaps_install()
  push PendingOp(cb, userdata)
  call PackageManager1.Install()
  daemon emits TaskAdded(path)
  pop PendingOp and bind it to TaskEntry
  subscribe path-specific PropertiesChanged
  subscribe path-specific RequestInteraction
  read initial Task1 properties
  call progress callback
```

Progress updates:

```text
org.freedesktop.DBus.Properties.PropertiesChanged
  interface: org.deepin.linglong.Task1
  changed:
    State
    Percentage
    Message
    Code
```

End of task:

```text
TaskRemoved(path)
  final property read
  final progress callback
  cleanup task entry
```

## Interaction confirmation

`Task1.RequestInteraction` is used for cases such as installing a newer version
over an installed lower version.

Observed ll-cli logic:

```text
messageID == Upgrade:
  localRef and remoteRef are shown to the user
  reply action must be "yes" to continue
otherwise:
  reserve/future behavior
```

Reply shape:

```text
a{sv}:
  "action" -> variant("s") "yes" | "no"
```

`linyaps-store` behavior:

```text
if UI interaction callback is set:
  call it and let UI reply
else:
  reply "no"
```

The test CLI registers a callback and automatically replies `yes` for manual
testing.

## Field-name caveats

Different paths use different names:

```text
D-Bus PackageInfoV2 module field: packageInfoV2Module
ll-cli list/info JSON module field: module
```

The code maps both into `LinyapsPackageInfo.module`.
