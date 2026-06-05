# Application icon source

## Flutter store behavior

`flutter-linglong-store` does not parse icons from local Linglong packages,
`.desktop` files or hicolor icon directories.

Its primary icon source is the remote application store API.

DTO parsing:

```dart
Object? _readAppIcon(Map json, String _) =>
    _readFirstNonNull(json, ['icon', 'appIcon']);
```

Relevant file:

```text
flutter-linglong-store/lib/data/models/api_dto.dart
```

List mapping:

```dart
InstalledApp(
  ...
  icon: dto.appIcon,
)
```

Relevant file:

```text
flutter-linglong-store/lib/data/repositories/app_repository_impl.dart
```

Detail mapping:

```dart
AppDetail(
  ...
  icon: dto.appIcon,
)
```

## Installed app enrichment

`ll-cli list --json` does not normally include an icon field. The Flutter store
parses local installed packages first, then calls the remote detail API to
enrich installed entries:

```text
enrichInstalledAppsWithDetails()
  local installed list
  build AppDetailsBO requests
  call getAppDetails()
  merge detail.appIcon into InstalledApp.icon
```

If the enrichment API fails, the installed list still renders, but icons fall
back to placeholders.

## UI rendering

`AppIcon` only consumes a URL. It does not discover icons.

Supported render paths:

```text
data:image/svg+xml or .svg -> SvgPicture.network
URL without extension      -> Image.network first, SVG fallback on decode path
normal raster URL          -> CachedNetworkImage
empty or failed URL        -> placeholder / broken-image icon
```

Relevant file:

```text
flutter-linglong-store/lib/presentation/widgets/app_icon.dart
```

## Implication for linyaps-store

Pure local/D-Bus data does not provide app icons:

```text
PackageManager1.Search -> no icon field observed
ll-cli list --json     -> no icon field observed
ll-cli --json info     -> no icon field observed
```

To support icons in the new native store, one of these is needed:

1. Keep using the remote app detail API for icon URLs.
2. Add local icon discovery:
   - locate exported `.desktop` files
   - parse `Icon=...`
   - resolve icon names through Linglong export dirs and hicolor icon themes
   - support absolute paths and named theme icons
3. Add an icon cache/index maintained by the store.

For a first GUI implementation, the most compatible path with
`flutter-linglong-store` is still remote API enrichment. Local discovery can be
added later as an offline fallback.
