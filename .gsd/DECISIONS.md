# Decisions Register

<!-- Append-only. Never edit or remove existing rows.
     To reverse a decision, add a new row that supersedes it.
     Read this file at the start of any planning or research phase. -->

| # | When | Scope | Decision | Choice | Rationale | Revisable? | Made By |
|---|------|-------|----------|--------|-----------|------------|---------|
| D001 | M001/S02 | arch | Window resize behavior on iOS 26 | Disable iOS 26 window resizing | Accidental resize when trying to use the Mac OS 9 menu bar near the top edge is a usability problem. The emulator window should be fixed-size. | Yes — if future work adds proper resize handling | human |
| D002 | M001/S01/T01 | architecture | How to intercept Cmd+key shortcuts on DFiP | Override pressesBegan/pressesEnded on OverlayViewController with Set-based iteration, return empty keyCommands, and expand canBecomeFirstResponder to include isiOSAppOnMac | Returning empty keyCommands prevents SDL2's internal UIKeyCommand registrations from intercepting Cmd+key before pressesBegan fires. Set-based iteration handles each press individually, calling super with single-element sets for non-Cmd presses to preserve existing SDL input paths. | Yes | agent |
