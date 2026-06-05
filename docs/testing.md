# Build and test notes

## Build

```bash
cd /home/deepin/ui/linyaps-store
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
```

## Automated smoke test

```bash
ctest --test-dir build --output-on-failure
```

The test is environment-tolerant:

- If system D-Bus is unavailable, D-Bus-dependent checks are skipped.
- If `org.deepin.draw` is not installed, the `info` check is skipped.

On the observed development machine, the test reports:

```text
PackageManager1 service: available
installed packages parsed: 39
test_backend: ok
```

## Test CLI

Run:

```bash
cd /home/deepin/ui/linyaps-store
./build/linyaps_cli
```

Commands:

```text
service
list
info <app_id>
search <keyword> [repo1,repo2]
install <app_id> [version] [channel]
uninstall <app_id> <version> [channel]
update <app_id>
prune
cancel
wait
quit
```

Suggested read-only test sequence:

```text
service
list
info org.deepin.draw
search calculator
quit
```

Non-interactive equivalent:

```bash
printf 'service\nlist\ninfo org.deepin.draw\nsearch calculator\nquit\n' \
  | ./build/linyaps_cli
```

Observed `search calculator` result after default store filtering:

```text
found 18 packages
```

This matches `ll-cli search calculator` default behavior after filtering out
develop modules and old versions.

## Package operation testing

Install/update/uninstall may fail as a normal user if system D-Bus policy blocks
the method before polkit. See [dbus-policy-and-polkit.md](dbus-policy-and-polkit.md).

Quick root test:

```bash
sudo ./build/linyaps_cli
```

Then:

```text
install <app_id>
wait
```

Normal-user test requires a D-Bus policy that allows the method to reach the
daemon, after which polkit handles authentication.

## Real D-Bus checks

Service:

```bash
busctl list | rg linglong
```

Configuration:

```bash
busctl get-property \
  org.deepin.linglong.PackageManager1 \
  /org/deepin/linglong/PackageManager1 \
  org.deepin.linglong.PackageManager1 \
  Configuration
```

Observed shape:

```text
a{sv}
  "defaultRepo" s "stable"
  "repos" av [a{sv} repo objects]
  "version" x 2
```

`busctl introspect` may fail with `Access denied` due to system D-Bus policy.
That does not necessarily block the methods the library uses.
