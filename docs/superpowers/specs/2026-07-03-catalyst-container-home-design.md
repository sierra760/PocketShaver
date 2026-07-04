# Relocate Catalyst app home to the container Data directory

**Date:** 2026-07-03
**Scope:** Mac Catalyst build only (`#if TARGET_OS_MACCATALYST`)
**Status:** Approved — ready for implementation planning

## Problem

On the Mac Catalyst build, all app-owned data (prefs, nvram, ROM, disk images,
extfs root, settings, gamepad thumbnails) currently lives under
`~/PocketShaver Home` in the user's **visible** home folder. Because the Catalyst
build is unsandboxed, `NSHomeDirectory()` returns the real user home, so this
directory sits in plain sight and is easy for the user to move, rename, or delete —
leaving too much room for accidental corruption. We want to move it out of sight
before launch.

## Decision

Store app data under the app's **container `Data/` directory** —
`~/Library/Containers/<bundle-id>/Data` — *without* enabling the App Sandbox.

Rationale:
- It is the literal path the OS would manage if the app were sandboxed, so it is
  the natural "app container," and it future-proofs a real sandbox flip later
  (`NSHomeDirectory()` would then already resolve to that Data dir).
- Staying unsandboxed keeps the JIT and all file access working unchanged. The
  JIT requires `com.apple.security.cs.allow-unsigned-executable-memory`, which the
  Mac App Store forbids, so store distribution (the only thing full sandboxing
  buys) is off the table anyway. Direct notarized (Developer ID) distribution does
  not require the sandbox.
- `~/Library/Containers/...` is out of casual sight, which addresses the corruption
  concern with minimal risk right before launch.

Alternatives considered and rejected: enabling the real App Sandbox (heavier,
must re-validate JIT + every file path, buys little); relocating to
`~/Library/Application Support/com.carbjo.pocketshaver` (valid, but the existing
internal `Library/Application Support` subdir nests awkwardly and it does not map
onto a future sandbox container as cleanly).

## Design

### 1. New location

`pocketshaver_home_directory()` changes from:

```
NSHomeDirectory()/PocketShaver Home
```

to:

```
NSHomeDirectory()/Library/Containers/<bundle-id>/Data
```

- `<bundle-id>` is read dynamically from `[[NSBundle mainBundle] bundleIdentifier]`
  (currently `com.carbjo.pocketshaver`), with a hardcoded fallback if it is nil.
- The `PocketShaver Home` name segment is removed. The container's `Data/` **is**
  the home.
- `NSHomeDirectory()` still returns the real user home because the build stays
  unsandboxed.

### 2. Internal layout unchanged — no derived-path edits

`Data/` mirrors a home directory, which is exactly the existing subdir shape, so
everything downstream keeps working with zero changes:

- `document_directory()` → `<home>/Documents` (prefs, ROM, disk images, extfs root)
- Swift `appSupportUrl` → `<home>/Library/Application Support` (settings, gamepad
  thumbnails)
- Swift `documentUrl` → `<home>/Documents`
- xpram/nvram → `<home>/.sheepshaver_nvram`
- prefs → `<home>/Documents/.sheepshaver_prefs`

Only **two root definitions** change:

- `pocketshaver_home_directory()` in
  `SheepShaver/src/MacOSX/PocketShaver/utils_ios.mm`
- `FileManager.pocketShaverHome` in
  `SheepShaver/src/MacOSX/PocketShaver/Swift/Utils/Extensions/Extensions.swift`

Both derive the bundle id from the same `NSBundle`/`Bundle.main` API so the two
strings are byte-identical. The dual C++/Swift definitions are **kept in sync** as
today (the existing established pattern) rather than consolidated — lower risk.

### 3. One-time migration of existing data

Existing testers have data in `~/PocketShaver Home`. A new function
`pocketshaver_migrate_home_if_needed()` (defined in `utils_ios.mm`, declared in
`utils_ios.h`), guarded by `dispatch_once`:

```
old = NSHomeDirectory()/PocketShaver Home
new = NSHomeDirectory()/Library/Containers/<bundle-id>/Data

if (new does NOT exist) and (old exists):
    create parent of `new`   (~/Library/Containers/<bundle-id>/)
    move old -> new           (fallback: copy; leave old intact if move fails)
ensure `new` exists
```

- **Move, not copy** — both paths are on the same volume (under the real home),
  so `moveItemAtPath:` is fast and near-atomic and avoids duplicating gigabytes of
  disk images. Copy is only a fallback.
- **Guard rule** — migrate only when the new dir is absent. If a tester somehow has
  a populated new container *and* a stale old dir (an interim-build overlap), skip
  and leave the old dir untouched rather than risk clobbering; emit a one-line
  `printf` notice so it is visible in the `PS_STDIO_FILE` capture.

**Trigger point** — a single call to `pocketshaver_migrate_home_if_needed()` at the
very top of `main()` in `SheepShaver/src/MacOSX/PocketShaver/main.mm`, before the
lifecycle-observer installs and before `main_ios()`. That is the earliest
app-specific code (it is SDL_main, invoked from SDL's UIKit delegate
`postFinishLaunch`) and runs before both the C++ core and any lazy Swift
`Storage.shared` file access, so nothing pre-creates the empty new dir and defeats
the guard.

The path accessors (`pocketshaver_home_directory()` and the Swift computed
property) remain pure compute-and-ensure; they do not carry migration logic.

### 4. Comment/doc cleanup

Several comments currently assert that Catalyst "runs unsandboxed, so it has no
per-app container ... everything lives under `~/PocketShaver Home`." That premise
is now inverted. Correct the existing comments (do not add new noise) in:

- `utils_ios.h` (the `pocketshaver_home_directory()` doc block)
- `utils_ios.mm`
- `Extensions.swift` (the `pocketShaverHome` doc block)
- `DiskCreation.mm`
- `xpram_unix.cpp` (the Catalyst home note)

New wording: we deliberately store under the container `Data/` directory *even
though* the build is unsandboxed, to keep app data out of the user's visible home
and reduce accidental corruption.

## Assumptions & non-goals

- Scoped to the **unsandboxed** Catalyst build. If the sandbox is ever enabled,
  `NSHomeDirectory()` itself becomes the container Data dir and the appended path
  would be wrong — a deliberate future revert, not handled here.
- iOS / Designed-for-iPad builds are untouched; they already use the real OS
  container.
- The "Open folder" button keeps working — Finder can open
  `~/Library/Containers/...` on demand even though it is out of casual sight.
- No `Container.plist` is written; that is a sandbox artifact and could confuse
  `containermanagerd` on a future sandbox flip.

## Affected files

- `SheepShaver/src/MacOSX/PocketShaver/utils_ios.mm` — new path + migration function
- `SheepShaver/src/MacOSX/PocketShaver/utils_ios.h` — migration declaration + comment
- `SheepShaver/src/MacOSX/PocketShaver/main.mm` — migration call site
- `SheepShaver/src/MacOSX/PocketShaver/Swift/Utils/Extensions/Extensions.swift` — new path + comment
- `SheepShaver/src/MacOSX/PocketShaver/Swift/Preferences/General/Disk Handling/DiskCreation.mm` — comment
- `BasiliskII/src/Unix/xpram_unix.cpp` — comment (the real file; `SheepShaver/src/Unix/xpram_unix.cpp` is a symlink to it — edit the BasiliskII copy only)

## Verification

- Fresh install (no `~/PocketShaver Home`): app creates and uses
  `~/Library/Containers/com.carbjo.pocketshaver/Data`; boot + prefs work.
- Upgrade path (existing `~/PocketShaver Home` with ROM/disk/prefs): data is moved
  into the container on first launch; boot uses the migrated data; old dir is gone.
- Second launch after migration: no re-migration, no duplicate work.
- C++ core (nvram/prefs) and Swift (settings/thumbnails) resolve to the same
  container.
