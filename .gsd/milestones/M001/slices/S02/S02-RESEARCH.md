# M001/S02 — Research

**Date:** 2026-03-29
**Status:** Complete

## Summary

Locking the window size on iOS 26 is straightforward. iPadOS 26 introduces a new windowed multitasking system where apps can be freely resized via drag handles. The existing `UIRequiresFullScreen = true` in Info.plist is deprecated and will be ignored in a future release. The correct approach is to use the `UIWindowScene.sizeRestrictions` API (available since iOS 13 but newly relevant for iPadOS 26's windowed mode) to set `minimumSize == maximumSize`, which makes the window non-resizable.

The app has no UIScene lifecycle — SDL2 manages the app delegate and creates the UIWindow directly. The existing `OverlayViewController.injectOverlayViewController()` method already retrieves the window via `UIApplication.shared.delegate?.window`, providing a natural access point to `window.windowScene?.sizeRestrictions`. No scene delegate or scene manifest changes are needed — `sizeRestrictions` is a property on `UIWindowScene` accessible from any UIWindow.

On macOS DFiP, iOS apps already have fixed window sizes, so this change primarily affects iPadOS 26 on iPad. Setting size restrictions on DFiP is harmless.

## Recommendation

**Add a `lockWindowSize()` static method to `OverlayViewController`** that reads the current window's bounds and pins `sizeRestrictions.minimumSize` and `maximumSize` to that size. Call it from `injectOverlayViewController()` after the overlay is injected.

The method should:
1. Get the UIWindow from `UIApplication.shared.delegate?.window`
2. Access `window.windowScene?.sizeRestrictions`
3. Set `minimumSize = maximumSize = currentWindowSize` (using the window's current bounds size)
4. Guard with `@available(iOS 13.0, *)` (sizeRestrictions exists from iOS 13)

This is a ~15-line change in a single file. No new files, no architectural changes.

**Keep `UIRequiresFullScreen = true` in Info.plist** — it still provides backward compatibility for iPadOS versions before 26, and removing it would be a separate migration concern.

## Implementation Landscape

### Key Files

- `SheepShaver/src/MacOSX/PocketShaver/Swift/Overlay/OverlayViewController.swift` — Contains `injectOverlayViewController()` which is the injection point. The new `lockWindowSize()` method goes here. The method already has access to the window via `UIApplication.shared.delegate?.window`.
- `SheepShaver/src/MacOSX/PocketShaver/Info.plist` — Already has `UIRequiresFullScreen: true`. No changes needed here.

### Build Order

1. **Add `lockWindowSize()` to OverlayViewController** — a static method that sets `sizeRestrictions.minimumSize = sizeRestrictions.maximumSize = windowSize`. Call it from `injectOverlayViewController()` after the overlay is embedded.
2. **Build and verify** — compile against iPad simulator, confirm no build errors.

This is a single-task slice. There's no dependency ordering or risk exploration needed.

### Verification Approach

1. **Build verification**: `xcodebuild build` succeeds with no new warnings/errors.
2. **Runtime verification on iPadOS 26 simulator**: Launch the app on an iPad simulator running iPadOS 26. The window should not show resize handles when the cursor approaches edges. Attempt to resize — it should not resize.
3. **Runtime verification on DFiP (macOS)**: Launch via "Designed for iPad" mode. Window behavior should be unchanged (already non-resizable on DFiP).
4. **Behavioral verification**: Cursor near the top edge of the window (Mac OS 9 menu bar area) should not trigger any resize affordance.

## Constraints

- `UIWindowScene.sizeRestrictions` is available from iOS 13 but is `nil` on iPhone. Must guard the optional access (`sizeRestrictions?.minimumSize = ...`).
- The `injectOverlayViewController()` method is called from ObjC (`objc_initOverlayViewController` in `OverlayViewControllerObjC.mm`). The window must exist by this point — it does, since SDL creates it before calling this function.
- SDL2 is bundled as a prebuilt xcframework — no ability to modify SDL's window creation flags. Size restrictions must be applied after SDL creates the window.

## Common Pitfalls

- **Setting size restrictions before the window has its final size** — `injectOverlayViewController()` is called during SDL's initialization. The window should already be at its final size by this point (SDL sets it to the screen bounds), but verify `window.bounds.size` is non-zero before using it.
- **Using UIScreen.main.bounds instead of window bounds** — On iPadOS 26 windowed mode, `UIScreen.main.bounds` may differ from the actual window size. Use `window.bounds.size` or `windowScene.coordinateSpace.bounds.size`.

## Skills Discovered

| Technology | Skill | Status |
|------------|-------|--------|
| UIKit/iOS | thebushidocollective/han@ios-uikit-architecture | available (62 installs) |
| iOS Development | travisjneuman/.claude@ios-development | available (57 installs) |

## Sources

- `UIRequiresFullScreen` deprecation and `UISceneSizeRestrictions` API mentioned in WWDC 2025 session "Make your UIKit app more flexible" (source: [Apple Developer Forums discussion](https://developer.apple.com/forums/thread/793406))
- `windowScene.sizeRestrictions?.minimumSize = CGSize(480,720)` confirmed working on iPadOS 26 by developer forum posts (source: [Apple Developer Forums](https://developer.apple.com/forums/tags/ipados/))
- `UIDesignRequiresCompatibility` is for Liquid Glass opt-out, not resize prevention (source: [Donny Wals blog](https://www.donnywals.com/opting-your-app-out-of-the-liquid-glass-redesign-with-xcode-26/))
