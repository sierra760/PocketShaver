# S01: Command-key interception — UAT

**Milestone:** M001
**Written:** 2026-03-30T04:53:24.479Z

# S01: Command-key interception — UAT

**Milestone:** M001
**Written:** 2026-03-29

## UAT Type

- UAT mode: human-experience
- Why this mode is sufficient: Keyboard interception requires a physical keyboard attached to a Mac running PocketShaver in "Designed for iPad" mode. Automated simulator testing cannot reproduce DFiP keyboard routing.

## Preconditions

- PocketShaver built and running on macOS in "Designed for iPad" mode
- Physical keyboard connected
- Mac OS 9 booted to desktop inside the emulator
- A Mac OS 9 application with menu bar shortcuts available (e.g., SimpleText, Finder)

## Smoke Test

Press Cmd+Q while a Mac OS 9 application is in the foreground. The Mac OS 9 app should quit (not PocketShaver).

## Test Cases

### 1. Cmd+Q quits Mac OS 9 app, not PocketShaver

1. Open SimpleText in Mac OS 9
2. Press Cmd+Q on the physical keyboard
3. **Expected:** SimpleText closes. PocketShaver remains running. Mac OS 9 desktop is visible.

### 2. Cmd+Shift+Q quits PocketShaver

1. With PocketShaver running and Mac OS 9 visible
2. Press Cmd+Shift+Q on the physical keyboard
3. **Expected:** PocketShaver terminates immediately.

### 3. Cmd+C / Cmd+V reach Mac OS 9

1. Open SimpleText in Mac OS 9, type some text, select it
2. Press Cmd+C
3. Click to a new position, press Cmd+V
4. **Expected:** Text is copied and pasted within SimpleText. macOS clipboard is NOT affected.

### 4. Cmd+Tab reaches Mac OS 9

1. Open two applications in Mac OS 9 (e.g., SimpleText and Finder)
2. Press Cmd+Tab
3. **Expected:** Mac OS 9 application switcher activates (not the macOS Cmd+Tab switcher).

### 5. Cmd+W closes Mac OS 9 window

1. Open a Finder window in Mac OS 9
2. Press Cmd+W
3. **Expected:** The Finder window closes inside Mac OS 9. PocketShaver window remains open.

### 6. Non-Cmd keyboard input is unaffected

1. Open SimpleText in Mac OS 9
2. Type regular text (letters, digits, symbols) without holding Cmd
3. **Expected:** All characters appear correctly in SimpleText. No interception or dropped keys.

### 7. Soft keyboard / touch input is unaffected

1. Tap the soft keyboard toggle (if available)
2. Type using the on-screen keyboard
3. **Expected:** Characters appear in the emulated Mac OS 9 as before. No regressions.

### 8. Function keys with Cmd reach Mac OS 9

1. In Mac OS 9, use a shortcut involving Cmd+F1 or similar (if an app binds it)
2. **Expected:** The key combination reaches Mac OS 9 rather than triggering a macOS system function.

## Edge Cases

### Bare Cmd key press/release

1. Press and release the Cmd key alone (no other key)
2. **Expected:** A `.cmd` ADB key-down followed by key-up is sent to the emulator. No crash, no hang. Mac OS 9 may briefly highlight the menu bar (normal behavior).

### Rapid Cmd+key sequences

1. Quickly press Cmd+Z, Cmd+Z, Cmd+Z in succession
2. **Expected:** All three undo events reach Mac OS 9. No dropped or duplicated events.

### Modifier combinations with Cmd

1. Press Cmd+Option+Escape in Mac OS 9
2. **Expected:** The three-modifier combo reaches Mac OS 9 as an ADB event (Mac OS 9 Force Quit behavior, if any).

## Failure Signals

- Pressing Cmd+Q causes PocketShaver to quit instead of the Mac OS 9 app
- Cmd+Shift+Q does NOT quit PocketShaver
- Regular typing (no Cmd) drops characters or behaves differently than before
- Touch/gamepad input stops working after the change
- Any crash when pressing Cmd+key combinations

## Not Proven By This UAT

- Exhaustive coverage of all 96+ mapped HID key codes — only common shortcuts are tested
- Behavior on actual iPad hardware with external keyboard (only DFiP on macOS is tested)
- Performance impact of the press interception (expected to be negligible)
- Interaction with specific Mac OS 9 applications beyond SimpleText and Finder

## Notes for Tester

- The Cmd+Shift+Q test will terminate PocketShaver — save any work in Mac OS 9 first.
- If Cmd+Tab is captured by macOS instead of reaching Mac OS 9, this may be a macOS system-level override that cannot be intercepted at the UIKit level — document this as a known limitation.
- The empty `keyCommands` array is critical — if SDL2 re-registers UIKeyCommands (e.g., after a code update), some Cmd+key combos may stop reaching pressesBegan.
