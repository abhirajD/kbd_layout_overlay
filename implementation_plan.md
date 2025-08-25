# Implementation Plan (Updated)

[Overview]
Implement a compact menubar and Preferences UX that replaces the legacy "Persistent mode" with an Auto-hide setting, exposes Scale/Opacity/Position/Custom sizing and an improved Hotkey section, and persists these settings across launches.

This document has been updated to reflect work completed to date, tests that ran successfully, remaining polish tasks, and recommended next steps.

[Status Summary]
- Core goals implemented:
  - Replace Persistent mode with Auto-hide (Off / 0.8s / 2s)
  - Add position_mode, click_through, always_on_top, start_at_login, auto_hide to persisted config
  - Migrate legacy `persistent` flag to `auto_hide`
  - NSTimer-based auto-hide semantics and unified hotkey behavior (Carbon + CGEvent)
  - Hotkey capture + quick-picks and re-registration on Apply
  - Immediate application of click-through, always-on-top, opacity, and position on Apply
- Tests:
  - Unit tests for config save/load and migration added (tests/test_config.c) and ran successfully.
- Known issues / partial work:
  - Preferences window layout was reflowed into a grid and improved, but visual polish is still required — controls can look uneven on some scales/resolutions. Auto Layout migration recommended.
  - Potential duplication risk: repeated calls to Carbon RegisterEventHotKey / InstallEventHandler may re-install handlers. This needs audit and a small guard to ensure one-time handler install / deregistration on changes.

[Types — Changes Made]
Modified `shared/config.h` (Config struct):
- Added fields:
  - float auto_hide;       /* seconds; 0.0 == Off (persistent) */
  - int position_mode;     /* 0 = Center, 1 = Top-Center, 2 = Bottom-Center, 3 = Custom */
  - int start_at_login;    /* 0/1 */
  - int click_through;     /* 0/1 */
  - int always_on_top;     /* 0/1 */
- get_default_config() updated:
  - auto_hide default = 0.8f
  - position_mode default = 2 (Bottom-Center)
  - click_through = 0
  - always_on_top = 0

[Files Modified]
- shared/config.h
- shared/config.c
  - Parsing and writing of: auto_hide, position_mode, start_at_login, click_through, always_on_top
  - Migration: if persistent == 1 then set auto_hide = 0.0f
  - Value clamping & basic validation
- macos/KbdLayoutOverlay/AppDelegate.m
  - buildMenu(): replaced "Persistent mode" with Auto-hide submenu, added quick choices
  - showOverlay / hideOverlay: added _autoHideTimer scheduling and cancellation
  - Carbon/CGEvent hotkey handlers: unified semantics for auto_hide == 0 vs timed auto-hide
  - createOverlayWindow: applies click-through, always-on-top, opacity, and position_mode when creating the window
  - openPreferences(): compact P1 layout implemented (grid-based reflow)
  - prefApply(): reads & persists new fields, immediately applies settings and re-registers hotkey
  - windowWillClose: nil out preference UI ivars to avoid dangling pointers
- tests/test_config.c (new)
  - Tests: save/load round-trip and persistent->auto_hide migration

[Functions — High-level]
- New/Updated helpers (AppDelegate.m)
  - setAutoHideSeconds:, setPositionMode:, updateAutoHideTimerIfNeeded:, applyQuickHotkey:
  - showOverlay / hideOverlay updated to honor new config fields and timer
  - prefApply persists and applies changes immediately

[Testing — Completed]
- Added tests/test_config.c to verify:
  - Round-trip persistence of new fields
  - Migration from legacy `"persistent": 1` to `auto_hide == 0.0`
- Local run: tests compiled and passed on developer machine:
  - "All config tests passed"

[Implementation Order — Progress Checklist]
1. Update shared/config.h: add fields and defaults
   - [x] Completed
2. Update shared/config.c: parse/write new JSON keys, migrate persistent->auto_hide, clamp/validate
   - [x] Completed
3. Add AppDelegate helpers: setAutoHideSeconds:, setPositionMode:, updateAutoHideTimerIfNeeded:, applyQuickHotkey:
   - [x] Completed
4. Add unit tests for config parsing & migration
   - [x] Completed (tests/test_config.c)
5. Modify buildMenu(): remove Persistent, add Show Keymap (momentary), Auto-hide submenu, Scale/Opacity/Position quick choices
   - [x] Completed
6. Add NSTimer *_autoHideTimer and implement scheduling in showOverlay/hideOverlay
   - [x] Completed
7. Update hotkey handlers to use auto_hide semantics (press-and-hold vs timed auto-hide)
   - [x] Completed
8. Refactor openPreferences() into compact P1 layout with new controls; wire quick-picks and validations
   - [x] Implemented (grid-based reflow)
   - [~] Visual polish remaining (alignment/spacing on some scales)
9. Wire prefApply() to persist new fields, reload overlay on size changes, re-register hotkey, and update menu
   - [x] Completed
10. Run unit tests and perform manual macOS verification steps; fix issues and polish UI labeling and localization
   - [x] Unit tests passed
   - [~] Manual verification: core behaviors verified (hotkey semantics, auto-hide timings, click-through, always-on-top, persistence). Preferences layout still needs visual polish on some displays.

[Remaining Work / Next Steps]
- Preferences UI polish (high priority)
  - Replace manual frame math with Auto Layout constraints to ensure consistent layout across screen scales and window sizes.
  - Ensure controls align when accessibility (large text) or different backingScaleFactor is used.
- Carbon event handler audit (medium priority)
  - Ensure Carbon InstallEventHandler / RegisterEventHotKey is called once and handlers are unregistered or updated safely when re-registering hotkeys.
- Automated UI/Integration tests (low/medium)
  - Add headless tests where feasible or add scripted manual verification steps in CI docs.
- Multi-monitor per-screen position persistence (future enhancement)
  - Persist screen identifier for custom positions; compute fallback if screen missing.
- Start-at-login implementation (deferred)
  - Field persisted; actual launch-helpers (SMLoginItemSetEnabled) can be implemented later.
- Small code TODOs added:
  - Add a guard flag to prevent multiple Carbon handler installs.
  - Add comments/documentation around migration behavior (persistent -> auto_hide).

[Manual Verification Checklist Performed]
- Show Keymap obeys Auto-hide Off/0.8/2s semantics.
- Hotkey press-and-hold preserved when auto_hide == 0 (legacy persistent behavior).
- Click-through toggles window ignoresMouseEvents behavior.
- Always-on-top toggles window level to NSScreenSaverWindowLevel when enabled.
- Opacity slider updates window alpha immediately on Apply.
- Position quick-picks reposition overlay; custom pixel offsets applied when position_mode == Custom.
- Tests for config save/load & migration run and pass.

[Commit & Release Recommendations]
- Commit changes in a single feature branch with descriptive message: "feat(macos): add auto-hide, position and prefs layout; migrate persistent -> auto_hide"
- Include tests/test_config.c in CI and run unit tests on PR.
- After preferences UI polish, consider patch release with migration notes in MVP_CHANGELOG.md.

[Notes]
- The preferences window layout was reflowed into a simple grid and improved, but visual alignment and spacing vary by backing scale. Converting to Auto Layout will resolve remaining issues and is the recommended next step.
- `persistent` field remains in the Config for migration compatibility; it can be removed in a future major cleanup once migration is well established.

--- End of Update
