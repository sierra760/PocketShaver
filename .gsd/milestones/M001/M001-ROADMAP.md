# M001: Command-Key Interception & Window Resize Lock

## Vision
Fix the "Designed for iPad" on macOS experience so all Cmd+key shortcuts are forwarded to the emulated Mac OS 9 instead of being consumed by the host, Cmd+Shift+Q is the app quit chord, and iOS 26 window resizing is disabled to prevent accidental resize when using the Mac OS 9 menu bar.

## Slice Overview
| ID | Slice | Risk | Depends | Done | After this |
|----|-------|------|---------|------|------------|
| S01 | Command-key interception | medium | — | ✅ | After this: pressing Cmd+Q in the emulator closes the Mac OS 9 app, not PocketShaver. Cmd+Shift+Q quits PocketShaver. All other Cmd+key combos reach Mac OS 9. |
| S02 | Lock window size on iOS 26 | low | — | ⬜ | After this: cursor near window edges does not trigger resize handles on iOS 26 or DFiP on macOS. |
