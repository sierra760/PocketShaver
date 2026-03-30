# Project

## What This Is

PocketShaver is a fork of SheepShaver that brings Mac OS 9 PowerPC emulation to iOS/iPad with Metal GPU acceleration, native Swift UI overlay, Bonjour LAN networking, and touchscreen gamepad support. It also runs on macOS via "Designed for iPad" mode. The codebase is a mix of C++ (emulation core, graphics engines), Objective-C++ (ADB bridge, SDL integration), and Swift (overlay UI, preferences, input handling).

## Core Value

Faithful Mac OS 9 emulation with a native, non-intrusive host experience — the emulator should feel like the primary app, not fight with the host OS for control.

## Current State

The emulator is functional on iOS and "Designed for iPad" on macOS. Metal GPU acceleration (NQD, RAVE, OpenGL), touch input, gamepad overlay, Bonjour networking, and preferences are all working. Two pain points exist in DFiP mode: (1) Cmd+key shortcuts are consumed by macOS instead of forwarded to the emulated Mac, and (2) iOS 26's new windowed mode introduces accidental window resizing near screen edges.

## Architecture / Key Patterns

- **Emulation core:** SheepShaver C++ in `SheepShaver/src/`, driven by `main_unix.cpp`
- **SDL2:** Provides the framebuffer window; bundled as xcframework. Keyboard events on iOS flow through SDL's internal UIKit handling, but PocketShaver doesn't poll SDL key events — it uses its own UIKit overlay.
- **ADB bridge:** `ADBObjC.mm` wraps C++ `ADBKeyDown`/`ADBKeyUp` for Swift consumption. All keyboard input routes through `InputInteractionModel` → `objc_ADBKeyDown`/`objc_ADBKeyUp`.
- **Overlay system:** `OverlayViewController` (Swift) sits on top of SDL's view. Handles touch gestures, gamepad, soft keyboard, performance counter. Already intercepts Cmd-W via `canPerformAction`.
- **DFiP detection:** `UIDevice.isiOSAppOnMac` static property, used to hide gamepad and suppress touch-specific hints.
- **Key mapping:** `SDLKey` enum maps to Mac ADB scancodes via `enValue`/`svValue`.

## Capability Contract

See `.gsd/REQUIREMENTS.md` for the explicit capability contract, requirement status, and coverage mapping.

## Milestone Sequence

- [ ] M001: Command-key interception & window resize lock — Fix DFiP experience so Cmd+key combos forward to emulator and iOS 26 window resizing is disabled
