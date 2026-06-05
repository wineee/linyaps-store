# D-Bus policy and polkit notes

See also:

- [architecture-and-api.md](architecture-and-api.md)
- [dbus-wire-format.md](dbus-wire-format.md)
- [testing.md](testing.md)

`linyaps-store` talks directly to `org.deepin.linglong.PackageManager1` on the
system bus. Search and read-only queries can work even when package operations
fail, because there are two separate permission layers:

1. system D-Bus policy decides whether the method call is allowed to reach
   `ll-package-manager`.
2. `ll-package-manager` then performs polkit authorization for privileged
   operations such as install, update and uninstall.

## Observed failure

When running `./build/linyaps_cli` as a normal user, install may fail with:

```text
Rejected send message, 2 matched rules; type="method_call" ... interface="org.deepin.linglong.PackageManager1" member="Install" ...
```

This is not a polkit denial. The method call is rejected by system D-Bus before
the daemon sees it, so no authentication dialog can appear.

## Cause

Some installed systems have a strict policy at:

```text
/usr/share/dbus-1/system.d/org.deepin.linglong.PackageManager1.conf
```

The strict policy only allows normal users to call:

```text
org.freedesktop.DBus.Peer
org.freedesktop.DBus.Properties.Get
org.freedesktop.DBus.Properties.GetAll
org.deepin.linglong.PackageManager1.Search
```

Only `root` and `deepin-linglong` may send all methods. That means normal users
cannot call `Install`, `Uninstall`, `Update`, `Prune` or `SetConfiguration`,
even though those methods have polkit actions.

The newer project policy allows normal users to send messages to the service and
lets the daemon/polkit make the authorization decision.

## Temporary test options

For quick library validation, run the CLI as root:

```bash
cd /home/deepin/ui/linyaps-store
sudo ./build/linyaps_cli
```

For normal-user testing with polkit, install a local policy override based on
the upstream policy:

```bash
cd /home/deepin/ui
sed 's/@LINGLONG_USERNAME@/deepin-linglong/g' \
  linyaps/misc/share/dbus-1/system.d/org.deepin.linglong.PackageManager1.conf \
  | sudo tee /etc/dbus-1/system.d/org.deepin.linglong.PackageManager1.conf >/dev/null

sudo busctl call org.freedesktop.DBus /org/freedesktop/DBus \
  org.freedesktop.DBus ReloadConfig
```

After that, restart `linyaps_cli` and test install/update/uninstall. The expected
path is:

```text
linyaps_cli -> system D-Bus allows method -> ll-package-manager -> polkit dialog
```

The GUI should follow the same path. It should not use `pkexec` for package
operations.

## Store subset behavior

`linyaps-store` intentionally implements only the GUI store subset:

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
