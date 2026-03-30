# M001: Command-Key Interception & Window Resize Lock

**Gathered:** 2026-03-29
**Status:** Ready for planning

## Project Description

PocketShaver is a Mac OS 9 emulator (SheepShaver fork) running as an iOS app, also usable on macOS via "Designed for iPad" mode. Two issues degrade the DFiP experience: the host OS steals Cmd+key shortcuts that should go to the emulated Mac, and iOS 26's new windowed mode causes accidental window resizing.

## Why This Milestone

The emulator is functional but the "Designed for iPad" on macOS experience is broken for keyboard-driven Mac OS 9 usage. Cmd+Q quits PocketShaver instead of the Mac OS 9 app. Cmd+C/V don't reach the emulated Mac for copy/paste. And on iOS 26, accidentally brushing the window edge resizes the emulator window when you're trying to click the Mac OS 9 menu bar.

## User-Visible Outcome

### When this milestone is complete, the user can:

- Press Cmd+Q in the emulator and it closes the active Mac OS 9 application window, not PocketShaver
- Press Cmd+C, Cmd+V, Cmd+Tab, and all other Cmd+key combos and have them reach Mac OS 9
- Press Cmd+Shift+Q to actually quit PocketShaver
- Move the cursor to the top of the emulator window to use the Mac OS 9 menu bar without triggering window resize handles

### Entry point / environment

- Entry point: PocketShaver.app launched on macOS via "Designed for iPad"
- Environment: macOS (Apple Silicon) running an iOS app in DFiP compatibility mode, also iPadOS 26 on iPad
- Live dependencies involved: none (all changes are local to the app)

## Completion Class

- Contract complete means: keyboard interception can be verified by checking that `pressesBegan`/`pressesEnded` overrides exist, ADB key events fire for Cmd+key combos, and Cmd+Shift+Q triggers app termination
- Integration complete means: the interception works in the actual running emulator with a Mac OS 9 session
- Operational complete means: none (no lifecycle/service concerns)

## Final Integrated Acceptance

To call this milestone complete, we must prove:

- Cmd+Q in a running Mac OS 9 session closes the Mac OS 9 window (not PocketShaver)
- Cmd+Shift+Q quits PocketShaver
- Window edges do not trigger resize handles on iOS 26 / DFiP on macOS

## Risks and Unknowns

- SDL2's internal UIKit keyboard handling may fight with OverlayViewController's `pressesBegan`/`pressesEnded` for Cmd-modified key events — the responder chain ordering matters
- `UIRequiresFullScreen` is already set to `true` but may not prevent iOS 26 windowed mode resize behavior when built with Xcode 26 SDK
- Some Cmd+key combos may be system-reserved at a level below UIKit (e.g., Cmd+Tab for app switching on macOS) and may not be interceptable

## Existing Codebase / Prior Art

- `OverlayViewController.swift` — already intercepts Cmd-W via `canPerformAction` / `performClose` override
- `InputInteractionModel.swift` — central input handling, routes keys to ADB via `handle(_:isDown:hapticAllowed:)`
- `SDLKey.swift` — enum with `.cmd` case (ADB scancode 0x37) and all letter/number/symbol keys
- `ADBObjC.mm` — C bridge: `objc_ADBKeyDown()`/`objc_ADBKeyUp()` → C++ `ADBKeyDown()`/`ADBKeyUp()`
- `SpecialButton.swift` — has `.cmdW` case that sends Cmd+W as paired ADB down/up events
- `HiddenInputFieldDelegate.swift` — soft keyboard input handling (must remain unaffected)
- `Info.plist` — has `UIRequiresFullScreen: true`

> See `.gsd/DECISIONS.md` for all architectural and pattern decisions — it is an append-only register; read it during planning, append to it during execution.

## Relevant Requirements

- R001 — Cmd+key interception for emulator forwarding
- R002 — Cmd+Shift+Q as PocketShaver quit chord
- R003 — Disable window drag-to-resize on iOS 26
- R004 — Existing non-Cmd keyboard input unaffected
- R005 — Existing touch/gamepad input unaffected

## Scope

### In Scope

- Intercepting all Cmd+key combos in the UIKit responder chain and forwarding them as ADB events
- Making Cmd+Shift+Q the quit chord for PocketShaver
- Disabling iOS 26 window resizing (via Info.plist keys, scene delegate configuration, or windowScene API)
- Removing or updating the existing narrow Cmd-W interception to be subsumed by the general approach

### Out of Scope / Non-Goals

- Changing how touch input, gamepad, or soft keyboard works
- Supporting Mac Catalyst as a separate target
- Handling Cmd+Tab (this is a system-level app switcher shortcut that macOS reserves; it cannot be intercepted by DFiP apps)

## Technical Constraints

- The app uses SDL2 bundled as xcframework — SDL's UIKit view controller participates in the responder chain
- The overlay is injected via `injectOverlayViewController()` as a child of SDL's root VC
- Must compile with `#if compiler(>=6.2)` / `@available(iOS 26.0, *)` guards for iOS 26 APIs
- `UIRequiresFullScreen` is already true; the resize issue is specific to iOS 26's new windowing system overriding this on iPads

## Integration Points

- SDL2's UIKit view controller — may handle `pressesBegan`/`pressesEnded` internally; the overlay must intercept Cmd events before SDL sees them
- macOS app lifecycle — Cmd+Q triggers `applicationWillTerminate` by default; need to prevent this

## Open Questions

- Does `pressesBegan`/`pressesEnded` on OverlayViewController fire for Cmd+key combos before SDL's view controller sees them? — Testing will confirm; the overlay is a child VC so it should be in the responder chain
- Is `UIDesignRequiresCompatibility` in Info.plist sufficient to prevent iOS 26 resize behavior? — Research suggests yes, but needs verification
