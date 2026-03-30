# Knowledge Base

<!-- Append-only. Add entries that would save future agents from repeating investigation. -->

## K001: Intercepting Cmd+key on DFiP requires empty keyCommands
**Context:** M001/S01
SDL2 registers its own `UIKeyCommand` objects on the responder chain, which intercept Cmd+key combos before `pressesBegan` fires. Returning an empty `keyCommands` array from `OverlayViewController` prevents this, ensuring all Cmd+key combos arrive at `pressesBegan` for custom handling.

## K002: iPad simulator destination varies by Xcode/SDK version
**Context:** M001/S01/T02
The task plan specified `iPad (10th generation)` but that device isn't available in Xcode 17 / iOS 26.2 SDK. Use `xcrun simctl list devices available` to find valid destinations. iPad Air 11-inch (M3) worked for this SDK version.

## K003: Non-Cmd presses must call super with single-element sets
**Context:** M001/S01/T01
When iterating over `presses` in `pressesBegan`/`pressesEnded`, non-Cmd presses must be forwarded individually via `super.pressesBegan(Set([press]), with: event)` rather than passing the entire original set. This avoids re-processing already-handled Cmd presses.

## K004: canBecomeFirstResponder and becomeFirstResponder must both gate on isiOSAppOnMac
**Context:** M001/S01/T01
Returning `true` from `canBecomeFirstResponder` isn't enough — `becomeFirstResponder()` must also be called in `setupViews()` for the same condition. Without both, keyboard presses don't route to `OverlayViewController` on DFiP.
