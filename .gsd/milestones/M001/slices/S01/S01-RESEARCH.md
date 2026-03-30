# S01 — Command-key interception — Research

**Date:** 2026-03-29
**Depth:** Targeted — known UIKit APIs applied to a well-understood codebase, with one integration risk (SDL2 responder chain).

## Summary

The goal is to intercept all Cmd+key shortcuts in the UIKit responder chain so they are forwarded to the emulated Mac OS 9 as ADB key events, instead of being consumed by macOS or iPadOS. Cmd+Shift+Q must be reserved as the PocketShaver quit chord.

The approach is straightforward: override `pressesBegan(_:with:)` and `pressesEnded(_:with:)` on `OverlayViewController` to detect presses where `.key?.modifierFlags` contains `.command`. For Cmd+Shift+Q, call `exit(0)` (the pattern already used in the codebase). For all other Cmd+key combos, send the Command modifier and the letter key as paired ADB down/up events via `InputInteractionModel.handle(_:isDown:hapticAllowed:)`. Non-Cmd presses must call `super` to preserve existing keyboard paths (soft keyboard, HiddenInputField).

SDL2's compiled binary already contains `pressesBegan:withEvent:`, `pressesEnded:withEvent:`, and `keyCommands` implementations internally. However, OverlayViewController is a **child** VC of SDL's root VC (injected via `injectOverlayViewController()`), so in UIKit's responder chain the child fires *before* the parent. This means `pressesBegan`/`pressesEnded` overrides on OverlayViewController will intercept Cmd events before SDL sees them — exactly the ordering we need. We do **not** call `super` for intercepted Cmd events, preventing them from reaching SDL or the system.

## Recommendation

Add `pressesBegan(_:with:)` and `pressesEnded(_:with:)` overrides to `OverlayViewController.swift`. Build a `UIKeyboardHIDUsage` → `SDLKey` mapping function to translate physical key codes to ADB scancodes. Gate all interception behind `UIDevice.isiOSAppOnMac` (DFiP mode only — on a real iPad with hardware keyboard the standard behavior should remain, though this is a judgment call; the context doc says "All Cmd+key combos" without platform restriction, so this should match the requirements' intent). Keep the existing `canPerformAction`/`performClose` override for Cmd+W — it prevents the system from closing the window, which is a separate concern from forwarding the key event. The `SpecialButton.cmdW` gamepad button should remain functional for touch-based users.

## Implementation Landscape

### Key Files

- `OverlayViewController.swift` (`…/Swift/Overlay/OverlayViewController.swift`) — **Primary change.** Add `pressesBegan`/`pressesEnded` overrides. This is where Cmd+key interception logic lives. Already has `canPerformAction`/`performClose` for Cmd+W. Already has `canBecomeFirstResponder` (returns true only on simulator — needs to return true on DFiP too so presses route here).
- `InputInteractionModel.swift` (`…/Swift/Overlay/Model/InputInteractionModel.swift`) — Already has `handle(_:isDown:hapticAllowed:)` which calls `objc_ADBKeyDown`/`objc_ADBKeyUp`. This is the canonical path for sending key events. The `pressesBegan`/`pressesEnded` implementation should call this.
- `SDLKey.swift` (`…/Swift/Overlay/Model/SDLKey.swift`) — Contains the `SDLKey` enum with all keys and their ADB scancodes (`.enValue`). A new mapping function/extension is needed: `UIKeyboardHIDUsage` → `SDLKey`. The `.cmd` case already exists with ADB scancode `0x37`.
- `ADBObjC.mm` / `ADBObjC.h` (`…/Swift/Overlay/ADB/`) — C bridge functions `objc_ADBKeyDown()`/`objc_ADBKeyUp()`. No changes needed — the existing bridge is sufficient.
- `SpecialButton.swift` (`…/Swift/Models and Persistence/Gamepad/SpecialButton.swift`) — Has `.cmdW` case that sends paired Cmd+W ADB events via `InputInteractionModel.handle(_:isDown:)`. No changes needed — this touch-gamepad feature is independent.
- `HiddenInputFieldDelegate.swift` (`…/Swift/Overlay/Hidden input field/HiddenInputFieldDelegate.swift`) — Soft keyboard input path. **Must remain unaffected.** The `pressesBegan`/`pressesEnded` implementation must only intercept Cmd-modified presses and call `super` for everything else.
- `HiddenInputField.swift` (`…/Swift/Overlay/Hidden input field/HiddenInputField.swift`) — UITextField subclass for soft keyboard. No changes needed.
- `Info.plist` (`…/PocketShaver/Info.plist`) — Already has `UIRequiresFullScreen: true`. No changes for S01.

### Build Order

1. **HID-to-SDLKey mapping** — Create a function or extension that maps `UIKeyboardHIDUsage` values to `SDLKey` cases. This is a pure data mapping with no dependencies — can be built and unit-tested independently. Without this, nothing else works.

2. **`pressesBegan`/`pressesEnded` on OverlayViewController** — Add the overrides. For each press whose `.key?.modifierFlags` contains `.command`:
   - Check if Shift is also held and the key is Q → terminate app (`exit(0)`)
   - Otherwise → send `SDLKey.cmd` down + mapped letter key down (in `pressesBegan`), and the corresponding ups (in `pressesEnded`)
   - Do NOT call `super` for intercepted Cmd events (prevents SDL and the system from seeing them)
   - For all non-Cmd presses → call `super` to preserve existing input paths

3. **Update `canBecomeFirstResponder`** — Currently returns `true` only on simulator. Must return `true` on DFiP (`UIDevice.isiOSAppOnMac`) so that `pressesBegan`/`pressesEnded` are delivered to the OverlayViewController. Without this, presses may not route to the overlay.

4. **Verification** — Build and run on macOS (DFiP), press Cmd+Q / Cmd+C / Cmd+V / Cmd+Shift+Q and verify behavior.

### Verification Approach

- **Build verification:** Project compiles without errors (`xcodebuild` or Xcode build)
- **Static verification:** `pressesBegan`/`pressesEnded` overrides exist on OverlayViewController; `canBecomeFirstResponder` returns true on DFiP
- **Behavioral verification (manual):** Run PocketShaver on macOS via DFiP:
  - Press Cmd+Q → should close the active Mac OS 9 application window (not PocketShaver)
  - Press Cmd+C → should trigger Copy in Mac OS 9
  - Press Cmd+Shift+Q → PocketShaver should quit
  - Press regular keys (no Cmd) → should work as before (soft keyboard, gamepad, etc.)
  - Touch input, gamepad → should work as before

## Constraints

- **SDL2 is a prebuilt xcframework** — cannot modify SDL's internal `pressesBegan`/`pressesEnded` or `keyCommands` implementation. Must work *around* it via responder chain ordering.
- **`UIKeyboardHIDUsage` → `SDLKey` mapping is manual** — there is no existing mapping in the codebase. The HID usage codes are defined by the USB HID spec; the ADB scancodes are in `SDLKey.enValue`. A ~50-key mapping table is needed covering letters, numbers, common symbols, function keys, and modifiers.
- **`canBecomeFirstResponder` change** — Currently gated to simulator only. Changing to include DFiP needs care: it means the OverlayViewController will participate in the responder chain for all key events, not just Cmd. The `pressesBegan`/`pressesEnded` implementation must correctly pass through non-Cmd events.
- **App termination** — The codebase already uses `exit(0)` for termination (in Extensions.swift). Cmd+Shift+Q should use the same mechanism. There is no `NSApplication` or `UIApplication.shared.terminate` pattern in the iOS app.

## Common Pitfalls

- **Swallowing too many events** — If `pressesBegan`/`pressesEnded` doesn't call `super` for non-Cmd presses, the soft keyboard (`HiddenInputFieldDelegate`) and all gamepad/touch input will break. The interception must be narrowly scoped to `press.key?.modifierFlags.contains(.command)`.
- **Cmd key alone** — The user might press and release Cmd without another key. `pressesBegan` will fire for the Cmd key itself (its `keyCode` is `.keyboardLeftGUI` or `.keyboardRightGUI`). The implementation should send `SDLKey.cmd` down/up for the bare Cmd press but NOT trigger app quit or any other combo action.
- **Modifier key tracking** — `pressesBegan` for Cmd+Q fires as two separate presses (Cmd, then Q). The Cmd press arrives first. The Q press arrives with `.modifierFlags` already containing `.command`. The implementation should: (a) on Cmd press alone, send ADB Cmd-down; (b) on Q press with `.command` in modifierFlags, send ADB Q-down (Cmd is already down); (c) on Cmd release, send ADB Cmd-up. This naturally composes the key combo.
- **`canPerformAction` interaction** — The existing `canPerformAction` override that returns `true` for `performClose:` must remain. Without it, the system would close the window on Cmd+W before `pressesBegan` gets a chance to intercept. The two mechanisms are complementary: `canPerformAction` prevents the system action, `pressesBegan`/`pressesEnded` forwards the key to ADB.
- **Cmd+Tab** — This is system-reserved on macOS (app switcher). It will NOT reach `pressesBegan` on a DFiP app. The milestone context already marks this as out of scope. No special handling needed.

## Open Risks

- **SDL2 internal `keyCommands` might register Cmd+key combos** — If SDL2's UIKit integration registers `UIKeyCommand` entries for Cmd+key combos, UIKit dispatches those via the responder chain's `keyCommands` property, which might bypass `pressesBegan`. Mitigation: OverlayViewController should also override `keyCommands` to return an empty array (or return its own UIKeyCommand entries that no-op), preventing SDL's `keyCommands` from winning. This needs testing.
- **`canBecomeFirstResponder` returning true may change focus behavior** — On real iPad with hardware keyboard, this could affect how text field focus works. If the scope is DFiP-only, gate behind `UIDevice.isiOSAppOnMac`. If all-platform, test on iPad hardware keyboard too.
- **Responder chain when HiddenInputField is first responder** — When the soft keyboard is showing, `HiddenInputField` (a `UITextField`) is first responder. `pressesBegan`/`pressesEnded` dispatches to the first responder first. Since `HiddenInputField` doesn't override these methods, they should propagate up. But if `UITextField` internally handles Cmd+key (e.g., Cmd+A for select all), those might be consumed before reaching OverlayViewController. For DFiP mode this is acceptable since the soft keyboard isn't shown (the user has a physical keyboard). But worth noting.
