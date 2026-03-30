# S01: Command-key interception

**Goal:** Intercept all Cmd+key shortcuts in the UIKit responder chain and forward them to the emulated Mac OS 9 as ADB key events. Cmd+Shift+Q quits PocketShaver. All other Cmd+key combos reach the emulator. Non-Cmd keyboard input, soft keyboard, touch, and gamepad remain unaffected.
**Demo:** After this: After this: pressing Cmd+Q in the emulator closes the Mac OS 9 app, not PocketShaver. Cmd+Shift+Q quits PocketShaver. All other Cmd+key combos reach Mac OS 9.

## Tasks
- [x] **T01: Added UIKeyboardHIDUsage→SDLKey failable initializer and pressesBegan/pressesEnded/keyCommands overrides to intercept all Cmd+key combos on DFiP, forwarding them as ADB events to the emulated Mac OS 9** — Build the `UIKeyboardHIDUsage` → `SDLKey` mapping and implement `pressesBegan`/`pressesEnded`/`keyCommands` overrides on `OverlayViewController` to intercept all Cmd+key combos and forward them as ADB key events. Update `canBecomeFirstResponder` to return `true` on DFiP.

## Steps

1. **Add `UIKeyboardHIDUsage` → `SDLKey` mapping to `SDLKey.swift`.**
   Add a static function or `init?(from:)` on `SDLKey` that maps `UIKeyboardHIDUsage` values to `SDLKey` cases. Cover: all 26 letters (`.keyboardA` through `.keyboardZ`), digits 0-9 (`.keyboard0` through `.keyboard9`), common symbols (`.keyboardHyphen`, `.keyboardEqualSign`, `.keyboardOpenBracket`, `.keyboardCloseBracket`, `.keyboardBackslash`, `.keyboardSemicolon`, `.keyboardQuote`, `.keyboardComma`, `.keyboardPeriod`, `.keyboardSlash`, `.keyboardGraveAccentAndTilde`, `.keyboardSpacebar`), modifiers (`.keyboardLeftShift`/`.keyboardRightShift` → `.shift`, `.keyboardLeftAlt`/`.keyboardRightAlt` → `.alt`, `.keyboardLeftControl`/`.keyboardRightControl` → `.ctrl`, `.keyboardLeftGUI`/`.keyboardRightGUI` → `.cmd`, `.keyboardCapsLock` → `.capslock`), function keys (`.keyboardF1` through `.keyboardF12`), navigation (`.keyboardReturnOrEnter` → `.enter`, `.keyboardDeleteOrBackspace` → `.backspace`, `.keyboardDeleteForward` → `.delete`, `.keyboardTab` → `.tab`, `.keyboardEscape` → `.escape`, `.keyboardInsert` → `.insert`, `.keyboardHome` → `.home`, `.keyboardEnd` → `.end`, `.keyboardPageUp` → `.pageup`, `.keyboardPageDown` → `.pagedown`), arrows (`.keyboardUpArrow`, `.keyboardDownArrow`, `.keyboardLeftArrow`, `.keyboardRightArrow`), and keypad keys (`.keypad0` through `.keypad9`, `.keypadPeriod`, `.keypadPlus`, `.keypadHyphen`, `.keypadAsterisk`, `.keypadSlash`, `.keypadEnter`, `.keypadEqualSign`). Return `nil` for unmapped HID codes.

2. **Update `canBecomeFirstResponder` in `OverlayViewController.swift`.**
   Change from `return UIDevice.isSimulator` to `return UIDevice.isSimulator || UIDevice.isiOSAppOnMac`. This ensures presses route to OverlayViewController in DFiP mode. Also update the `setupViews()` call site: change `if UIDevice.isSimulator { becomeFirstResponder() }` to `if UIDevice.isSimulator || UIDevice.isiOSAppOnMac { becomeFirstResponder() }`.

3. **Add `keyCommands` override to `OverlayViewController`.**
   Override `var keyCommands: [UIKeyCommand]?` to return an empty array `[]`. This prevents SDL2's internal `UIKeyCommand` registrations from intercepting Cmd+key combos via the responder chain before `pressesBegan` fires. Place this in the existing `OverlayViewController` extension that has `canPerformAction`.

4. **Add `pressesBegan(_:with:)` override to `OverlayViewController`.**
   In the same extension, override `pressesBegan`. For each press in the set:
   - Guard that `press.key` is non-nil (physical keyboard press)
   - If `press.key!.modifierFlags.contains(.command)` AND the key is NOT a bare modifier (i.e., the keyCode is not `.keyboardLeftGUI`/`.keyboardRightGUI`/`.keyboardLeftShift`/`.keyboardRightShift`/`.keyboardLeftAlt`/`.keyboardRightAlt`/`.keyboardLeftControl`/`.keyboardRightControl`):
     - Check if Shift is also held (`press.key!.modifierFlags.contains(.shift)`) AND the keyCode maps to Q (`.keyboardQ`). If so → call `exit(0)` to terminate PocketShaver.
     - Otherwise → map the keyCode to `SDLKey` via the mapping from step 1. If mapped, call `inputInteractionModel.handle(.cmd, isDown: true, hapticAllowed: false)` then `inputInteractionModel.handle(mappedKey, isDown: true, hapticAllowed: false)`. Do NOT call `super` — this prevents the event from reaching SDL or the system.
   - If `press.key!.keyCode` is `.keyboardLeftGUI` or `.keyboardRightGUI` (bare Cmd press) → call `inputInteractionModel.handle(.cmd, isDown: true, hapticAllowed: false)`. Do NOT call `super`.
   - For all other presses (no `.command` modifier and not a Cmd key) → call `super.pressesBegan(presses, with: event)` to preserve existing input paths.

5. **Add `pressesEnded(_:with:)` override to `OverlayViewController`.**
   Mirror the `pressesBegan` logic for key-up events:
   - For Cmd-modified non-modifier presses → map keyCode, call `inputInteractionModel.handle(mappedKey, isDown: false, hapticAllowed: false)` then `inputInteractionModel.handle(.cmd, isDown: false, hapticAllowed: false)`. Do NOT call `super`.
   - For bare Cmd key release (`.keyboardLeftGUI`/`.keyboardRightGUI`) → call `inputInteractionModel.handle(.cmd, isDown: false, hapticAllowed: false)`. Do NOT call `super`.
   - For all other presses → call `super.pressesEnded(presses, with: event)`.

## Must-Haves

- [ ] `SDLKey.init?(fromHIDUsage:)` or equivalent static mapping covers letters, digits, symbols, F-keys, arrows, modifiers, navigation, and keypad
- [ ] `canBecomeFirstResponder` returns `true` when `UIDevice.isiOSAppOnMac` is true
- [ ] `keyCommands` returns empty array `[]`
- [ ] `pressesBegan` intercepts Cmd-modified presses and sends ADB key-down events
- [ ] `pressesBegan` detects Cmd+Shift+Q and calls `exit(0)`
- [ ] `pressesBegan` calls `super` for non-Cmd presses
- [ ] `pressesEnded` sends matching ADB key-up events for Cmd-modified presses
- [ ] `pressesEnded` calls `super` for non-Cmd presses
- [ ] Bare Cmd key press/release sends `.cmd` ADB down/up

## Verification

- `grep -q 'pressesBegan' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` succeeds
- `grep -q 'pressesEnded' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` succeeds
- `grep -q 'keyCommands' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` succeeds
- `grep -q 'isiOSAppOnMac' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` returns at least 2 matches (canBecomeFirstResponder + setupViews)
- `grep -q 'fromHIDUsage\|UIKeyboardHIDUsage' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift` succeeds
- `grep -q 'exit(0)' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` succeeds
  - Estimate: 1h
  - Files: SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift, SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift
  - Verify: grep -q 'pressesBegan' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift && grep -q 'pressesEnded' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift && grep -q 'keyCommands' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift && grep -q 'fromHIDUsage\|UIKeyboardHIDUsage' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift && grep -q 'exit(0)' SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift && echo 'All checks passed'
- [ ] **T02: Build project and verify Cmd-key interception compiles cleanly** — Build the PocketShaver Xcode project to prove the new code integrates with the existing codebase (SDL2 xcframework, C++ bridge, Obj-C++ ADB layer). Fix any compilation errors.

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
  - Estimate: 30m
  - Files: SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/Model/SDLKey.swift, SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift, PocketShaver.xcodeproj/project.pbxproj
  - Verify: cd /Users/sierraburkhart/Workspace/PocketShaver && xcodebuild -project PocketShaver.xcodeproj -scheme PocketShaver -destination 'platform=iOS Simulator,name=iPad (10th generation)' -configuration Debug build 2>&1 | grep -q 'BUILD SUCCEEDED'
