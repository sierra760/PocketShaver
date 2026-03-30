# Requirements

This file is the explicit capability and coverage contract for the project.

## Active

### R001 — Cmd+key interception for emulator forwarding
- Class: core-capability
- Status: active
- Description: All keyboard shortcuts involving the Command key (Cmd+Q, Cmd+C, Cmd+V, Cmd+Tab, etc.) must be intercepted by PocketShaver and forwarded to the emulated Mac OS 9 as ADB key events, rather than being consumed by macOS or iPadOS.
- Why it matters: Without this, basic Mac OS 9 operations (closing windows, copy/paste, quitting apps) are impossible when running as "Designed for iPad" on macOS because the host OS steals the shortcuts.
- Source: user
- Primary owning slice: M001/S01
- Supporting slices: none
- Validation: unmapped
- Notes: Must cover arbitrary Cmd+key combos, not just a hardcoded list.

### R002 — Cmd+Shift+Q as PocketShaver quit chord
- Class: core-capability
- Status: active
- Description: Cmd+Shift+Q must be the only keyboard shortcut that actually quits PocketShaver. All other Cmd+key combos are forwarded to the emulator.
- Why it matters: Users need a way to quit the host app, but the standard Cmd+Q must go to the emulated Mac instead.
- Source: user
- Primary owning slice: M001/S01
- Supporting slices: none
- Validation: unmapped
- Notes: This replaces the default Cmd+Q quit behavior.

### R003 — Disable window drag-to-resize on iOS 26
- Class: core-capability
- Status: active
- Description: The iOS 26 windowed mode drag-to-resize behavior must be disabled so that moving the cursor near the emulator window edges does not trigger resize handles. This prevents accidental resizing when the user is trying to use the Mac OS 9 menu bar at the top of the emulator window.
- Why it matters: Accidental resizing disrupts the emulation experience, especially when the emulated Mac menu bar is at the top edge of the window.
- Source: user
- Primary owning slice: M001/S02
- Supporting slices: none
- Validation: unmapped
- Notes: Applies to both iPadOS 26 windowed mode and "Designed for iPad" on macOS.

### R004 — Existing non-Cmd keyboard input unaffected
- Class: quality-attribute
- Status: active
- Description: The soft keyboard, HiddenInputField text entry, and all non-Command-modified key input must continue to work exactly as before.
- Why it matters: The Cmd-key interception must not break the existing touch keyboard or gamepad key input paths.
- Source: inferred
- Primary owning slice: M001/S01
- Supporting slices: none
- Validation: unmapped
- Notes: Regression guard.

### R005 — Existing touch/gamepad input unaffected
- Class: quality-attribute
- Status: active
- Description: Touch gestures, gamepad buttons, hover mode, and all existing input mechanisms must continue to work unchanged.
- Why it matters: The changes to keyboard handling and window behavior must not introduce regressions in the primary iOS input paths.
- Source: inferred
- Primary owning slice: M001/S01
- Supporting slices: M001/S02
- Validation: unmapped
- Notes: Regression guard.

## Validated

(none yet)

## Deferred

(none)

## Out of Scope

(none)

## Traceability

| ID | Class | Status | Primary owner | Supporting | Proof |
|---|---|---|---|---|---|
| R001 | core-capability | active | M001/S01 | none | unmapped |
| R002 | core-capability | active | M001/S01 | none | unmapped |
| R003 | core-capability | active | M001/S02 | none | unmapped |
| R004 | quality-attribute | active | M001/S01 | none | unmapped |
| R005 | quality-attribute | active | M001/S01 | M001/S02 | unmapped |

## Coverage Summary

- Active requirements: 5
- Mapped to slices: 5
- Validated: 0
- Unmapped active requirements: 0
