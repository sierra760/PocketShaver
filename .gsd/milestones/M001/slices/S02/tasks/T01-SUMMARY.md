---
id: T01
parent: S02
milestone: M001
provides: []
requires: []
affects: []
key_files: ["SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift"]
key_decisions: ["Used flatMap to unwrap double-optional from UIApplicationDelegate.window", "Guarded width and height > 0 separately for clarity"]
patterns_established: []
drill_down_paths: []
observability_surfaces: []
duration: ""
verification_result: "All four slice-level checks pass: xcodebuild build exits 0, grep finds lockWindowSize (2 occurrences: definition + call site) and sizeRestrictions in the target file."
completed_at: 2026-03-30T05:00:30.542Z
blocker_discovered: false
---

# T01: Added lockWindowSize() that pins sizeRestrictions min/max to current window bounds, disabling iPadOS 26 drag-to-resize

> Added lockWindowSize() that pins sizeRestrictions min/max to current window bounds, disabling iPadOS 26 drag-to-resize

## What Happened
---
id: T01
parent: S02
milestone: M001
key_files:
  - SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift
key_decisions:
  - Used flatMap to unwrap double-optional from UIApplicationDelegate.window
  - Guarded width and height > 0 separately for clarity
duration: ""
verification_result: passed
completed_at: 2026-03-30T05:00:30.543Z
blocker_discovered: false
---

# T01: Added lockWindowSize() that pins sizeRestrictions min/max to current window bounds, disabling iPadOS 26 drag-to-resize

**Added lockWindowSize() that pins sizeRestrictions min/max to current window bounds, disabling iPadOS 26 drag-to-resize**

## What Happened

Added a private static func lockWindowSize() to the Cmd+key interception extension of OverlayViewController. The method gets the window via UIApplication.shared.delegate?.window (flatMap for double-optional), guards non-zero bounds, then sets windowScene.sizeRestrictions minimumSize and maximumSize to the current window size. Called from injectOverlayViewController() after sdlVC.embed(vc). On iPhone, sizeRestrictions is nil so the optional chain is a no-op. On macOS DFiP, windows are already fixed-size.

## Verification

All four slice-level checks pass: xcodebuild build exits 0, grep finds lockWindowSize (2 occurrences: definition + call site) and sizeRestrictions in the target file.

## Verification Evidence

| # | Command | Exit Code | Verdict | Duration |
|---|---------|-----------|---------|----------|
| 1 | `xcodebuild build -project PocketShaver.xcodeproj -scheme PocketShaver -destination 'platform=iOS Simulator,name=iPad Air 11-inch (M3)' CODE_SIGNING_ALLOWED=NO -quiet 2>&1` | 0 | ✅ pass | 5700ms |
| 2 | `grep -q 'lockWindowSize' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` | 0 | ✅ pass | 50ms |
| 3 | `grep -q 'sizeRestrictions' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` | 0 | ✅ pass | 50ms |
| 4 | `grep -c 'lockWindowSize' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` | 0 | ✅ pass | 50ms |


## Deviations

None.

## Known Issues

None.

## Files Created/Modified

- `SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift`


## Deviations
None.

## Known Issues
None.
