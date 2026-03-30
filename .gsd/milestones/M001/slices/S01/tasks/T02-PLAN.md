---
estimated_steps: 31
estimated_files: 3
skills_used: []
---

# T02: Build project and verify Cmd-key interception compiles cleanly

Build the PocketShaver Xcode project to prove the new code integrates with the existing codebase (SDL2 xcframework, C++ bridge, Obj-C++ ADB layer). Fix any compilation errors.

## Steps

1. **Run `xcodebuild` for the PocketShaver scheme targeting iOS Simulator.**
   Execute: `xcodebuild -project PocketShaver.xcodeproj -scheme PocketShaver -destination 'platform=iOS Simulator,name=iPad (10th generation)' -configuration Debug build 2>&1 | tail -30`
   The simulator destination avoids code-signing issues. The build must succeed (exit code 0, "BUILD SUCCEEDED" in output).

2. **If build fails, diagnose and fix.**
   Common issues:
   - `UIKeyboardHIDUsage` requires `import UIKit` (already present in both files, but verify)
   - `UIPress.Key` property `.keyCode` is `UIKeyboardHIDUsage` type — ensure the mapping function's parameter type matches
   - `inputInteractionModel` is a `lazy var` on OverlayViewController — the press handler accesses it as `self.inputInteractionModel` which is fine
   - `@MainActor` isolation — `InputInteractionModel` is `@MainActor`, and `pressesBegan`/`pressesEnded` are called on the main thread, but if the compiler complains, may need `@MainActor` annotation or `Task { @MainActor in ... }` wrapper
   - `exit(0)` — this is a C function available via Foundation, no import needed
   Fix any issues in the source files and re-run `xcodebuild` until build succeeds.

3. **Run static verification checks.**
   Confirm key integration points:
   - `pressesBegan` and `pressesEnded` both reference `inputInteractionModel.handle`
   - `pressesBegan` contains `exit(0)` for the Cmd+Shift+Q path
   - `canBecomeFirstResponder` references `isiOSAppOnMac`
   - Non-Cmd presses call `super.pressesBegan` / `super.pressesEnded`
   - `keyCommands` returns `[]`

## Must-Haves

- [ ] `xcodebuild` exits with code 0 and output contains `BUILD SUCCEEDED`
- [ ] No compiler warnings in the modified files related to the new code

## Verification

- `xcodebuild -project PocketShaver.xcodeproj -scheme PocketShaver -destination 'platform=iOS Simulator,name=iPad (10th generation)' -configuration Debug build 2>&1 | grep -q 'BUILD SUCCEEDED'`

## Inputs

- `SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift` — T01 output: HID mapping extension
- `SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` — T01 output: press interception overrides

## Expected Output

- `SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift` — compiles cleanly (may have minor fixes)
- `SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` — compiles cleanly (may have minor fixes)

## Inputs

- ``SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift` — T01 output with HID-to-SDLKey mapping`
- ``SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` — T01 output with press interception overrides`

## Expected Output

- ``SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift` — compiles cleanly, possibly with minor fixes`
- ``SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` — compiles cleanly, possibly with minor fixes`

## Verification

cd /Users/sierraburkhart/Workspace/PocketShaver && xcodebuild -project PocketShaver.xcodeproj -scheme PocketShaver -destination 'platform=iOS Simulator,name=iPad (10th generation)' -configuration Debug build 2>&1 | grep -q 'BUILD SUCCEEDED'
