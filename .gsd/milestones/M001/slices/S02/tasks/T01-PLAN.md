---
estimated_steps: 28
estimated_files: 1
skills_used: []
---

# T01: Add lockWindowSize() to OverlayViewController and call it from injectOverlayViewController()

Add a static `lockWindowSize()` method to `OverlayViewController` that pins the window's `sizeRestrictions.minimumSize` and `maximumSize` to the current window size, preventing iPadOS 26 drag-to-resize. Call it from `injectOverlayViewController()` after the overlay is embedded.

This is a ~15-line change in a single file. The method uses the existing `UIApplication.shared.delegate?.window` access pattern already present in `injectOverlayViewController()`.

## Context for Executor

The app is a Mac OS 9 emulator ("PocketShaver") that runs as an iOS app. On iPadOS 26, apps can be freely resized via drag handles. This is bad for the emulator because the user accidentally triggers resize when trying to use the Mac OS 9 menu bar at the top edge. We need to lock the window size.

`OverlayViewController` is a Swift UIViewController that gets injected over SDL2's root view controller. The `injectOverlayViewController()` static method already accesses the UIWindow — we just need to also grab `window.windowScene?.sizeRestrictions` from there.

On macOS DFiP (Designed for iPad), iOS apps already have fixed windows, so this is harmless there. On iPhone, `sizeRestrictions` is nil, so optional chaining handles that case.

## Steps

1. Open `SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift`
2. Add a `private static func lockWindowSize()` method in the extension that contains `injectOverlayViewController()` (the one at the bottom of the file with `// MARK: - Cmd+key interception`)
3. The method should:
   - Get the window from `UIApplication.shared.delegate?.window` (flatMap the double-optional)
   - Guard that `window.bounds.size` is non-zero (safety check per research)
   - Access `window.windowScene?.sizeRestrictions`
   - Set `sizeRestrictions?.minimumSize = windowSize` and `sizeRestrictions?.maximumSize = windowSize`
4. Call `lockWindowSize()` at the end of `injectOverlayViewController()`, after `sdlVC.embed(vc)`
5. Build the project: `xcodebuild build -project PocketShaver.xcodeproj -scheme PocketShaver -destination 'platform=iOS Simulator,name=iPad Air 11-inch (M3)' CODE_SIGNING_ALLOWED=NO -quiet 2>&1`
6. Verify build succeeds with no errors

## Must-Haves

- [ ] `lockWindowSize()` static method exists in `OverlayViewController`
- [ ] Method sets `sizeRestrictions?.minimumSize` and `sizeRestrictions?.maximumSize` to the window's current bounds size
- [ ] Method guards against zero-size window bounds
- [ ] `lockWindowSize()` is called from `injectOverlayViewController()` after `sdlVC.embed(vc)`
- [ ] `xcodebuild build` succeeds with exit code 0

## Verification

- `xcodebuild build -project PocketShaver.xcodeproj -scheme PocketShaver -destination 'platform=iOS Simulator,name=iPad Air 11-inch (M3)' CODE_SIGNING_ALLOWED=NO -quiet 2>&1` exits 0
- `grep -q 'lockWindowSize' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` exits 0
- `grep -q 'sizeRestrictions' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` exits 0
- `grep -c 'lockWindowSize' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` returns >= 2 (definition + call site)

## Inputs

- ``SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` — existing file containing `injectOverlayViewController()` where lockWindowSize() will be added and called`

## Expected Output

- ``SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` — modified with lockWindowSize() method and call site in injectOverlayViewController()`

## Verification

xcodebuild build -project PocketShaver.xcodeproj -scheme PocketShaver -destination 'platform=iOS Simulator,name=iPad Air 11-inch (M3)' CODE_SIGNING_ALLOWED=NO -quiet 2>&1 && grep -q 'lockWindowSize' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift && grep -q 'sizeRestrictions' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift && test $(grep -c 'lockWindowSize' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift) -ge 2
