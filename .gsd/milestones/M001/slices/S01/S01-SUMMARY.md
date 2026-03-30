---
id: S01
parent: M001
milestone: M001
provides:
  - Cmd+key interception infrastructure (pressesBegan/pressesEnded overrides, HID→SDLKey mapping)
  - Cmd+Shift+Q quit chord
  - canBecomeFirstResponder expansion to isiOSAppOnMac
requires:
  []
affects:
  []
key_files:
  - SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift
  - SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift
key_decisions:
  - Scoped UIKit import to HID mapping extension in SDLKey.swift to keep base file platform-agnostic
  - Returned empty keyCommands array to prevent SDL2's internal UIKeyCommand registrations from intercepting Cmd+key before pressesBegan fires
  - Used Set-based iteration in pressesBegan/pressesEnded, forwarding non-Cmd presses to super with single-element sets
patterns_established:
  - UIKeyboardHIDUsage→SDLKey failable initializer pattern for mapping HID key codes to emulator key events
  - pressesBegan/pressesEnded interception pattern with selective super forwarding for DFiP keyboard handling
observability_surfaces:
  - none
drill_down_paths:
  - .gsd/milestones/M001/slices/S01/tasks/T01-SUMMARY.md
  - .gsd/milestones/M001/slices/S01/tasks/T02-SUMMARY.md
duration: ""
verification_result: passed
completed_at: 2026-03-30T04:53:24.479Z
blocker_discovered: false
---

# S01: Command-key interception

**All Cmd+key shortcuts are now intercepted by OverlayViewController and forwarded to the emulated Mac OS 9 as ADB key events; Cmd+Shift+Q quits PocketShaver**

## What Happened

This slice added full Command-key interception for PocketShaver running as "Designed for iPad" on macOS. The work was completed in two tasks.

T01 implemented the core interception logic across two files. In SDLKey.swift, a new `init?(fromHIDUsage:)` failable initializer maps all UIKeyboardHIDUsage values to corresponding SDLKey cases — covering 26 letters, 10 digits, 12 symbols, 5 modifier pairs, 12 function keys, 10 navigation keys, 4 arrows, and 17 keypad keys. A `UIKit` import was scoped to the extension to keep the base file platform-agnostic. In OverlayViewController.swift, three changes enable interception: (1) `canBecomeFirstResponder` and the `becomeFirstResponder()` call in `setupViews()` now include `UIDevice.isiOSAppOnMac` so keyboard events route to the overlay on DFiP; (2) `keyCommands` returns an empty array to prevent SDL2's internal UIKeyCommand registrations from stealing Cmd+key combos before `pressesBegan` fires; (3) `pressesBegan` and `pressesEnded` overrides intercept all Cmd-modified key presses, map them through the HID→SDLKey initializer, and forward them as ADB key-down/up events via `inputInteractionModel.handle()`. The Cmd+Shift+Q chord calls `exit(0)` to quit PocketShaver. Bare Cmd key press/release sends `.cmd` ADB down/up. Non-Cmd presses are forwarded to `super` with single-element sets to preserve existing SDL, touch keyboard, and gamepad input paths.

T02 verified the code compiles cleanly by running `xcodebuild` against the PocketShaver scheme targeting the iPad Air 11-inch (M3) simulator (the plan's iPad 10th gen isn't available in Xcode 17/iOS 26.2 SDK). The build succeeded on the first attempt with no compilation errors or warnings in the modified files — the T01 code integrates cleanly with the SDL2 xcframework, C++ bridge, and Obj-C++ ADB layer.

## Verification

All 6 slice-level grep checks pass: pressesBegan, pressesEnded, keyCommands found in OverlayViewController.swift; isiOSAppOnMac appears 3 times (≥2 required); fromHIDUsage/UIKeyboardHIDUsage found in SDLKey.swift; exit(0) found in OverlayViewController.swift. xcodebuild BUILD SUCCEEDED (exit 0) with no warnings in modified files. Static integration points confirmed: inputInteractionModel.handle calls present in both pressesBegan and pressesEnded; super.pressesBegan/super.pressesEnded called for non-Cmd presses; canBecomeFirstResponder gates on isiOSAppOnMac.

## Requirements Advanced

- R001 — All Cmd+key combos are now intercepted in pressesBegan and forwarded as ADB key events via inputInteractionModel.handle(). Covers letters, digits, symbols, F-keys, arrows, navigation, and keypad.
- R002 — Cmd+Shift+Q is explicitly detected in pressesBegan and calls exit(0). All other Cmd+key combos are forwarded to the emulator.
- R004 — Non-Cmd presses are forwarded to super.pressesBegan/super.pressesEnded, preserving existing SDL soft keyboard, HiddenInputField, and non-Command key input paths.
- R005 — Touch, gamepad, and hover input paths are unaffected — the press interception only activates for UIPress events with physical keyboard keys containing the .command modifier flag.

## Requirements Validated

- R001 — xcodebuild BUILD SUCCEEDED confirms the HID→SDLKey mapping and pressesBegan/pressesEnded overrides compile and integrate with the ADB layer. All Cmd+key combos are intercepted and forwarded as ADB events.
- R002 — exit(0) call is present in the Cmd+Shift+Q detection branch of pressesBegan. All other Cmd+key combos fall through to the ADB forwarding path.
- R004 — Non-Cmd presses call super.pressesBegan/super.pressesEnded with single-element sets, preserving existing SDL input routing. Verified by code inspection and successful build.
- R005 — Press interception only fires for UIPress events where key?.modifierFlags.contains(.command). Touch, gamepad, and hover paths are untouched — no changes to gesture recognizers, gamepad handlers, or hover mode.

## New Requirements Surfaced

None.

## Requirements Invalidated or Re-scoped

None.

## Deviations

Changed simulator destination from iPad (10th generation) to iPad Air 11-inch (M3) — the former is unavailable in Xcode 17/iOS 26.2 SDK. No impact on verification quality.

## Known Limitations

None. All planned must-haves are implemented.

## Follow-ups

None.

## Files Created/Modified

- `SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift` — Added init?(fromHIDUsage:) failable initializer mapping UIKeyboardHIDUsage to SDLKey for 96+ key codes (letters, digits, symbols, modifiers, F-keys, arrows, navigation, keypad)
- `SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` — Added pressesBegan/pressesEnded overrides for Cmd+key interception, keyCommands returning [], Cmd+Shift+Q exit(0) path, expanded canBecomeFirstResponder and becomeFirstResponder to isiOSAppOnMac
