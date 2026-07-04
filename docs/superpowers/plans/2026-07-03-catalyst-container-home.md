# Catalyst Container Home Relocation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On the unsandboxed Mac Catalyst build, store all app-owned data under the app container's `Data` directory (`~/Library/Containers/com.carbjo.pocketshaver/Data`) instead of the user's visible `~/PocketShaver Home`, migrating any existing data once on launch.

**Architecture:** Two root path definitions (C/ObjC `pocketshaver_home_directory()` and Swift `FileManager.pocketShaverHome`) are re-pointed at the container `Data` dir; the internal `Documents` + `Library/Application Support` subtree is unchanged so nothing downstream needs edits. A new idempotent `pocketshaver_migrate_home_if_needed()` (dispatch_once) moves any legacy `~/PocketShaver Home` into the container, called at the top of `main()` before the C++ core or any Swift file access runs. Scoped to `#if TARGET_OS_MACCATALYST`; iOS/Designed-for-iPad builds are untouched.

**Tech Stack:** Objective-C++ (Foundation `NSFileManager`/`NSBundle`), Swift (Catalyst), C++ shared emulator core, Xcode / XcodeBuildMCP.

**Spec:** `docs/superpowers/specs/2026-07-03-catalyst-container-home-design.md`

**Commit conventions (repo standing rule — do NOT violate):** commits are authored by `sierra760`; NO `Co-Authored-By`, NO AI/assistant mentions in commit messages or code comments. Do **not** stage `SheepShaver/src/MacOSX/PocketShaver.xcodeproj/project.pbxproj` or the other pre-existing working-tree changes — stage only the files named in each task.

**Testing note:** This target has no unit-test harness for path/Foundation code (the lockstep suite is for the CPU core only). Verification is therefore (a) a green Catalyst build proving the changed translation units compile, and (b) runtime UAT of the fresh-install and upgrade scenarios in Task 3. Do not fabricate an XCTest target.

---

## File Structure

| File | Change |
|------|--------|
| `SheepShaver/src/MacOSX/PocketShaver/utils_ios.h` | Declare `pocketshaver_migrate_home_if_needed()`; update the `pocketshaver_home_directory()` doc comment. |
| `SheepShaver/src/MacOSX/PocketShaver/utils_ios.mm` | Re-point `pocketshaver_home_directory()` at the container Data dir; add bundle-id/path helpers and the migration function. |
| `SheepShaver/src/MacOSX/PocketShaver/main.mm` | Include `utils_ios.h`; call the migration once at the top of `main()`, Catalyst-guarded. |
| `SheepShaver/src/MacOSX/PocketShaver/Swift/Utils/Extensions/Extensions.swift` | Re-point `FileManager.pocketShaverHome` at the container Data dir; update its comment. |
| `SheepShaver/src/MacOSX/PocketShaver/Swift/Preferences/General/Disk Handling/DiskCreation.mm` | Comment-only correction. |
| `BasiliskII/src/Unix/xpram_unix.cpp` | Comment-only correction (real file; `SheepShaver/src/Unix/xpram_unix.cpp` is a symlink to it — edit the BasiliskII copy only). |

---

## Task 1: Core relocation + migration (single atomic commit)

All four functional files change together so no committed state has C++ and Swift resolving to different homes.

**Files:**
- Modify: `SheepShaver/src/MacOSX/PocketShaver/utils_ios.h`
- Modify: `SheepShaver/src/MacOSX/PocketShaver/utils_ios.mm`
- Modify: `SheepShaver/src/MacOSX/PocketShaver/main.mm`
- Modify: `SheepShaver/src/MacOSX/PocketShaver/Swift/Utils/Extensions/Extensions.swift`

- [ ] **Step 1: Update the header declaration + comment**

In `utils_ios.h`, replace this block:

```objc
// Mac Catalyst runs unsandboxed, so it has no per-app container like the
// Designed-for-iPad build. Instead everything the app owns lives under a
// single, stable home: ~/PocketShaver Home. Returns that path (creating it if
// needed). Only defined on Catalyst.
const char* pocketshaver_home_directory();
```

with:

```objc
// On the (unsandboxed) Mac Catalyst build we deliberately store all app-owned
// data under the app's container Data directory
// (~/Library/Containers/<bundle-id>/Data) rather than the user's visible home,
// to keep it out of casual sight and reduce accidental corruption. Returns that
// path, creating it if needed. Only defined on Catalyst.
const char* pocketshaver_home_directory();

// Moves a pre-existing ~/PocketShaver Home (from builds that stored data in the
// visible home) into the container Data directory exactly once. Idempotent and
// safe to call from multiple entry points; call as early as possible at launch,
// before any file access. Only defined on Catalyst.
void pocketshaver_migrate_home_if_needed();
```

- [ ] **Step 2: Rewrite the accessor + add helpers and migration in `utils_ios.mm`**

Replace the entire existing Catalyst block:

```objc
#if TARGET_OS_MACCATALYST
const char* pocketshaver_home_directory()
{
	static char buf[1024];
	NSString *home = [NSHomeDirectory() stringByAppendingPathComponent:@"PocketShaver Home"];
	[[NSFileManager defaultManager] createDirectoryAtPath:home
							  withIntermediateDirectories:YES
											   attributes:nil
													error:nil];
	strlcpy(buf, [home fileSystemRepresentation], sizeof(buf));
	return buf;
}
#endif
```

with:

```objc
#if TARGET_OS_MACCATALYST
// The app's bundle identifier, with a defensive fallback if -bundleIdentifier
// is ever nil. Must match the Swift side (Bundle.main.bundleIdentifier) so both
// resolve to byte-identical container paths.
static NSString *pocketshaver_bundle_identifier()
{
	NSString *bid = [[NSBundle mainBundle] bundleIdentifier];
	return bid.length ? bid : @"com.carbjo.pocketshaver";
}

// <real home>/Library/Containers/<bundle-id>/Data — the container Data dir the
// OS would manage if we were sandboxed. We are not, so NSHomeDirectory() is the
// real user home; we store here anyway to keep data out of the visible home.
static NSString *pocketshaver_container_data_path()
{
	NSString *containers = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Containers"];
	NSString *container  = [containers stringByAppendingPathComponent:pocketshaver_bundle_identifier()];
	return [container stringByAppendingPathComponent:@"Data"];
}

void pocketshaver_migrate_home_if_needed()
{
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		NSFileManager *fm = [NSFileManager defaultManager];
		NSString *newHome = pocketshaver_container_data_path();
		NSString *oldHome = [NSHomeDirectory() stringByAppendingPathComponent:@"PocketShaver Home"];

		BOOL newExists = [fm fileExistsAtPath:newHome];
		BOOL oldExists = [fm fileExistsAtPath:oldHome];

		if (!newExists && oldExists) {
			// Ensure ~/Library/Containers/<bundle-id>/ exists to receive the move.
			[fm createDirectoryAtPath:[newHome stringByDeletingLastPathComponent]
				  withIntermediateDirectories:YES attributes:nil error:nil];
			NSError *err = nil;
			if ([fm moveItemAtPath:oldHome toPath:newHome error:&err]) {
				printf("Migrated app home %s -> %s\n",
					   [oldHome fileSystemRepresentation], [newHome fileSystemRepresentation]);
			} else {
				// Same-volume move should not fail; fall back to a copy and keep
				// the original intact so no data is lost.
				NSError *cErr = nil;
				if ([fm copyItemAtPath:oldHome toPath:newHome error:&cErr]) {
					printf("Copied app home %s -> %s (move failed: %s)\n",
						   [oldHome fileSystemRepresentation], [newHome fileSystemRepresentation],
						   [[err localizedDescription] UTF8String]);
				} else {
					printf("WARNING: could not migrate app home %s -> %s (%s)\n",
						   [oldHome fileSystemRepresentation], [newHome fileSystemRepresentation],
						   [[cErr localizedDescription] UTF8String]);
				}
			}
		} else if (newExists && oldExists) {
			// Interim-build overlap: a populated container already exists. Leave
			// the stale legacy dir untouched rather than risk clobbering live data.
			printf("Note: both container home %s and legacy %s exist; leaving legacy dir in place.\n",
				   [newHome fileSystemRepresentation], [oldHome fileSystemRepresentation]);
		}

		// Ensure the container Data dir exists for first use.
		[fm createDirectoryAtPath:newHome withIntermediateDirectories:YES attributes:nil error:nil];
	});
}

const char* pocketshaver_home_directory()
{
	// Idempotent one-time relocation; calling here guarantees the move even if
	// some path resolves before main()'s explicit call.
	pocketshaver_migrate_home_if_needed();
	NSString *home = pocketshaver_container_data_path();
	// Recreate defensively each call (matches prior behavior if the dir is
	// removed at runtime); cheap and returns immediately if it already exists.
	[[NSFileManager defaultManager] createDirectoryAtPath:home
							  withIntermediateDirectories:YES attributes:nil error:nil];
	static char buf[1024];
	strlcpy(buf, [home fileSystemRepresentation], sizeof(buf));
	return buf;
}
#endif
```

- [ ] **Step 3: Wire the migration call into `main.mm`**

Add the header include next to the existing includes (after `#include "my_sdl.h"`):

```objc
#include "utils_ios.h"
```

Then, inside `int main(int argc, char * argv[])` (the SDL_main at the top of the file), immediately **after** the `PS_STDIO_FILE` capture block and **before** `[GfxAccelBackgroundLifecycleObserver.shared install];`, insert:

```objc
	/* Relocate app data from the legacy visible-home location
	 * (~/PocketShaver Home) into the app container's Data directory exactly
	 * once, before the emulator core (main_ios) or any Swift file access reads
	 * or creates anything. Idempotent; Catalyst-only. */
#if TARGET_OS_MACCATALYST
	pocketshaver_migrate_home_if_needed();
#endif
```

- [ ] **Step 4: Re-point the Swift home in `Extensions.swift`**

Replace this block:

```swift
extension FileManager {
	// Mac Catalyst runs unsandboxed and has no per-app container, so pin all
	// app data under a single stable home (~/PocketShaver Home) that mirrors
	// the Designed-for-iPad container's Data/ layout. Kept in sync with the
	// C++/ObjC path resolution in utils_ios.mm / xpram_unix.cpp.
	// NSHomeDirectory() (not FileManager.homeDirectoryForCurrentUser, which is
	// unavailable on Mac Catalyst) — matches utils_ios.mm's C++ resolution so
	// Swift and the emulator core agree on the same home.
	static var pocketShaverHome: URL {
		URL(fileURLWithPath: NSHomeDirectory(), isDirectory: true).appendingPathComponent("PocketShaver Home")
	}
```

with:

```swift
extension FileManager {
	// On the (unsandboxed) Mac Catalyst build we deliberately store all app data
	// under the app's container Data directory
	// (~/Library/Containers/<bundle-id>/Data) rather than the user's visible
	// home, to keep it out of casual sight and reduce accidental corruption.
	// Kept byte-identical to utils_ios.mm's pocketshaver_home_directory() so
	// Swift and the emulator core agree on the same container path.
	// NSHomeDirectory() (not FileManager.homeDirectoryForCurrentUser, which is
	// unavailable on Mac Catalyst) returns the real user home here because the
	// build is unsandboxed.
	static var pocketShaverHome: URL {
		let home = URL(fileURLWithPath: NSHomeDirectory(), isDirectory: true)
		let bundleID = Bundle.main.bundleIdentifier ?? "com.carbjo.pocketshaver"
		return home
			.appendingPathComponent("Library/Containers", isDirectory: true)
			.appendingPathComponent(bundleID, isDirectory: true)
			.appendingPathComponent("Data", isDirectory: true)
	}
```

Leave the `documentUrl` and `appSupportUrl` computed properties below it unchanged — they derive from `pocketShaverHome` and keep the same `Documents` / `Library/Application Support` subpaths.

- [ ] **Step 5: Build the Catalyst target (green)**

Use XcodeBuildMCP. First confirm defaults with `session_show_defaults`; the PocketShaver app scheme built for the **Mac Catalyst / My Mac** destination is the target. Build and confirm:
- The build **succeeds**.
- The build log shows `CompileC` lines for `utils_ios.mm` and `main.mm`, and a Swift compile of `Extensions.swift` (per the XcodeBuildMCP workspace-DerivedData note, this proves the edits actually recompiled).

Expected: build SUCCEEDED, no errors in the four changed files.

- [ ] **Step 6: Commit (functional change, atomic)**

Stage only the four files — not the pbxproj or other dirty files:

```bash
git add SheepShaver/src/MacOSX/PocketShaver/utils_ios.h \
        SheepShaver/src/MacOSX/PocketShaver/utils_ios.mm \
        SheepShaver/src/MacOSX/PocketShaver/main.mm \
        "SheepShaver/src/MacOSX/PocketShaver/Swift/Utils/Extensions/Extensions.swift"
git commit -m "catalyst: store app data in container Data dir, migrate legacy home"
```

Verify the commit touched exactly those four files: `git show --stat HEAD`.

---

## Task 2: Comment cleanup (separate commit)

Pure comment corrections; no behavior change.

**Files:**
- Modify: `SheepShaver/src/MacOSX/PocketShaver/Swift/Preferences/General/Disk Handling/DiskCreation.mm`
- Modify: `BasiliskII/src/Unix/xpram_unix.cpp`

- [ ] **Step 1: Fix the `DiskCreation.mm` comment**

Replace:

```objc
	// Create new disk images in the same Documents the emulator/prefs use
	// (the container's Documents on iOS, ~/PocketShaver Home/Documents on
	// Mac Catalyst) rather than the user's real ~/Documents.
```

with:

```objc
	// Create new disk images in the same Documents the emulator/prefs use
	// (the OS container's Documents on iOS and, on Mac Catalyst, the app
	// container's Data/Documents) rather than the user's real ~/Documents.
```

- [ ] **Step 2: Fix the `xpram_unix.cpp` comment**

In `BasiliskII/src/Unix/xpram_unix.cpp`, replace:

```cpp
// Defined in utils_ios.mm. On Mac Catalyst the NVRAM lives under the app's
// PocketShaver home rather than $HOME (which is the real user home there).
// Forward-declared to avoid coupling this shared BasiliskII source to the
// PocketShaver-only utils_ios.h.
```

with:

```cpp
// Defined in utils_ios.mm. On Mac Catalyst the NVRAM lives under the app's
// container Data directory (~/Library/Containers/<bundle-id>/Data) rather than
// $HOME (which is the real user home there, since the build is unsandboxed).
// Forward-declared to avoid coupling this shared BasiliskII source to the
// PocketShaver-only utils_ios.h.
```

- [ ] **Step 3: Build (green) to confirm the comment edits didn't break anything**

Rebuild the Catalyst target via XcodeBuildMCP. Expected: build SUCCEEDED. (`xpram_unix.cpp` is a shared core file; a comment change must still compile clean.)

- [ ] **Step 4: Commit**

```bash
git add "SheepShaver/src/MacOSX/PocketShaver/Swift/Preferences/General/Disk Handling/DiskCreation.mm" \
        BasiliskII/src/Unix/xpram_unix.cpp
git commit -m "catalyst: correct app-home comments for container Data relocation"
```

---

## Task 3: Runtime UAT (user-assisted, no commit)

Automated tests aren't available for this path; verify the two real scenarios on the Mac. The user runs the built app; the assistant inspects the filesystem and the `PS_STDIO_FILE` capture.

- [ ] **Step 1: Fresh-install scenario**

With no `~/PocketShaver Home` and no `~/Library/Containers/com.carbjo.pocketshaver/Data` present, launch the app. Confirm:
- `~/Library/Containers/com.carbjo.pocketshaver/Data/` is created.
- After configuring a ROM + disk, `Data/Documents/` holds the ROM/disk/`.sheepshaver_prefs`, `Data/.sheepshaver_nvram` appears, and `Data/Library/Application Support/` holds settings.
- The emulator boots.

Check:
```bash
find ~/Library/Containers/com.carbjo.pocketshaver/Data -maxdepth 2 -print
test -d "$HOME/PocketShaver Home" && echo "UNEXPECTED: legacy dir exists" || echo "ok: no legacy dir"
```

- [ ] **Step 2: Upgrade scenario**

Restore a populated legacy `~/PocketShaver Home` (with `Documents/` ROM+disk+prefs, `.sheepshaver_nvram`, `Library/Application Support/`), remove the container Data dir, then launch. Confirm:
- The migration log line (`Migrated app home ... -> ...`) appears in the `PS_STDIO_FILE` capture.
- `~/PocketShaver Home` is **gone** and its contents now live under `~/Library/Containers/com.carbjo.pocketshaver/Data/`.
- The emulator boots against the migrated ROM/disk/prefs (settings and nvram preserved).

Check:
```bash
test -d "$HOME/PocketShaver Home" && echo "FAIL: legacy dir still present" || echo "ok: legacy migrated away"
find ~/Library/Containers/com.carbjo.pocketshaver/Data -maxdepth 2 -print
```

- [ ] **Step 3: Second-launch idempotency**

Launch again. Confirm no migration log line is emitted the second time and no duplicate directories were created.

---

## Self-Review (completed by plan author)

- **Spec coverage:** New location (Task 1 Steps 2/4) ✓; internal layout unchanged / only two roots (Task 1) ✓; migration function + dispatch_once + move-with-copy-fallback + guard rule + printf notice (Task 1 Step 2) ✓; trigger at top of `main()` before observers/`main_ios` (Task 1 Step 3) ✓; comment cleanup across the five sites — utils_ios.h/.mm in Task 1, Extensions.swift in Task 1, DiskCreation.mm + xpram in Task 2 ✓; assumptions/non-goals honored (Catalyst-guarded; iOS untouched; no Container.plist) ✓.
- **Placeholder scan:** none — every code step contains full code and exact commands.
- **Type/name consistency:** `pocketshaver_migrate_home_if_needed()`, `pocketshaver_home_directory()`, `pocketshaver_container_data_path()`, `pocketshaver_bundle_identifier()`, `FileManager.pocketShaverHome` used identically across header, .mm, main.mm, and Swift. Both C++ and Swift build `NSHomeDirectory()/Library/Containers/<bundle-id>/Data` with the same `com.carbjo.pocketshaver` fallback → byte-identical.
- **Atomicity:** the split-brain risk (C++ vs Swift homes diverging at a commit boundary) is avoided by grouping all four functional files into Task 1's single commit; Task 2 is comments only.
