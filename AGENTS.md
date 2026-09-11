# Agent Development Guide

This file defines the engineering rules for AI/code agents working in this repository. Treat it as mandatory project guidance, not optional advice.

## Project scope and validated target

This repository is a NickelHook-based mod for Kobo's Nickel reader UI.

Validated hardware/firmware target for the current fork:

- Kobo Sage
- Firmware 4.38.23552
- Nickel as the primary reader
- Native/offline Kobo Tweaks settings menu
- Runtime application of reading-header/footer changes without closing the book

The mod must remain compatible with the user's existing Kobo/Grimmory setup. Do not modify or overwrite `/.kobo/Kobo/Kobo eReader.conf`, `[OneStoreServices]`, the custom Kobo API endpoint, reading-state synchronization, or unrelated Nickel account/sync behavior unless the task explicitly requires it.

Do not add a runtime dependency on Wi-Fi, HTTP, localhost browser pages, or an external web UI for settings. The validated UI path is native Nickel UI.

## Source-of-truth hierarchy

When deciding how to implement something, use sources in this order.

1. **This repository and the vendored `NickelHook` submodule.**
   - Read the current implementation before changing it.
   - Read `NickelHook/README.md` before adding hooks, dlsym symbols, opaque Nickel objects, or startup logic.

2. **NickelMenu as the main reference implementation for private Nickel UI.**
   - Repository: `pgaskin/NickelMenu`.
   - Use it as the first reference for `NickelTouchMenu`, `MenuTextItem`, tap registration, private constructors/destructors, dlsym patterns, failsafe handling, and firmware compatibility.

3. **Other mature NickelHook mods.**
   - `shermp/NickelDBus`
   - `shermp/NickelClock`
   - `pgaskin/NickelSeries` / `pgaskin/kobo-mods`
   - `redphx/nickel-screensaver`
   - `nicoverbruggen/NickelTypeFix`
   - `nicoverbruggen/NickelHome`
   Use these to compare lifecycle, QObject ownership, symbol handling, UI updates, logging, and compatibility patterns.

4. **The actual target firmware binary is authoritative for private Nickel ABI.**
   For private `libnickel` symbols, signatures, mangled names, constructors, object sizes, vtables, or layout assumptions, verify against the real Kobo firmware whenever possible using tools such as `nm`, `readelf`, `objdump`, `c++filt`, or Ghidra. Never guess a private symbol signature or object layout from memory.

5. **Official Qt documentation matching the Kobo runtime/toolchain.**
   Use it for QObject ownership, parent-child lifetime, `QPointer`, signals/slots, event loop behavior, `QSettings`, layouts, file watching, timers, and GUI-thread rules. Do not assume a Qt 6 behavior/API is available on Kobo without verification.

6. **KDE C++ binary compatibility guidance.**
   Use it whenever a change touches private C++ ABI, class layout, vtables, constructors, inheritance, or binary interfaces.

7. **C++ Core Guidelines and cppreference.**
   Use them for ownership, RAII, const-correctness, UB avoidance, pointer safety, interfaces, and general code quality. If a generic modern-C++ recommendation conflicts with NickelHook compatibility guidance, NickelHook compatibility wins.

8. **Issues, MobileRead, Reddit, blogs, and forum posts are secondary evidence.**
   They are useful for reproductions and firmware/device quirks, but do not use them as the sole authority for ABI, memory ownership, or Qt behavior.

See `.agents/SOURCES.md` for the reference catalogue.

## Non-negotiable engineering rules

### Keep NickelHook mods small and conservative

This code runs inside Nickel for long periods. Prefer the smallest change that solves the task. Avoid turning the plugin into a general application framework.

Priorities, in order:

1. Device stability
2. Backward/forward compatibility where practical
3. Graceful failure
4. Correctness
5. Battery/CPU/IO efficiency
6. Maintainability
7. Features

A feature that risks random Nickel crashes, silent memory corruption, boot loops, excessive wakeups, or long-term memory growth must be redesigned.

### Prefer Qt/C over the C++ standard library in plugin runtime code

Follow NickelHook guidance: avoid C++ standard-library runtime dependencies unless there is a strong reason. Prefer Qt and C APIs. C++ language features are fine when they do not introduce risky runtime/ABI dependencies.

### Treat all private Nickel API as unstable ABI

- Centralize private symbol resolution in the existing dlsym abstraction (`TweaksDlsym` / the repository's current symbol layer).
- Do not scatter raw `dlsym()` calls through feature code.
- Do not invent mangled names or signatures.
- Check every required symbol before use.
- If a symbol required only for an optional feature is unavailable, disable that feature gracefully and log why rather than crashing Nickel.
- If opaque storage must be allocated for a private Nickel class, document the reference implementation and reason for the chosen size, reserve safety margin where appropriate, and prefer a design that avoids depending on the exact private class size.
- Be especially careful with vtables, constructors, destructors, inheritance, and calling conventions.

### QObject and GUI ownership

- Use Qt parent-child ownership wherever possible.
- Use `QPointer<T>` when holding a reference to a QObject whose lifetime is controlled elsewhere.
- Do not keep raw pointers to Nickel objects longer than necessary unless lifetime is proven.
- Modify GUI objects only on the Qt GUI thread, preferably through normal QObject signal/slot/event-loop mechanisms.
- Do not delete layouts/widgets you do not own. Give Kobo Tweaks-owned layouts/widgets stable `objectName` values when ownership must be recognized later.
- When rebuilding the reading UI, destroy/recreate only Kobo Tweaks-owned widgets/layouts. Do not recreate or replace Nickel's entire `ReadingView` unless a task explicitly proves it is necessary and safe.

### Settings are a single-source-of-truth subsystem

Current settings code lives under `src/settings/`.

- Reuse `TweaksSettings` and `src/settings/settings_keys.h`.
- Do not duplicate literal settings keys in unrelated files.
- Read booleans with boolean semantics (`toBool()`), integers with integer semantics, and preserve the existing validated ranges/defaults.
- Keep migration/backward compatibility in mind when changing keys or values.
- When validating widget placement, validate all six zones: header left/center/right and footer left/center/right.
- Never allow the native menu and settings loader to disagree about the persisted value.
- Call `QSettings::sync()` where immediate persistence/read-after-write correctness is required.

The user's current configuration uses the existing `Reading`, `Reading.Widget`, `Reading.Widget.Battery`, and `Reading.Widget.Clock` schema. Preserve it unless an explicit migration is part of the task.

### Native settings UI architecture

The validated settings path is:

`NickelMenu reader entry -> local trigger -> Kobo Tweaks watcher -> NickelTouchMenu/MenuTextItem -> QSettings -> runtime widget reload`

Maintain these constraints:

- No Wi-Fi requirement.
- No embedded browser/local HTTP server.
- No plain QWidget/QPushButton overlay for touch interaction; this was tested on Kobo Sage and did not receive Nickel touch gestures correctly.
- Use Nickel's native touch menu/gesture registration pattern as established by NickelMenu.
- Keep menu transitions event-loop friendly; avoid nested modal event loops unless a specific Nickel API requires them.
- Magic delays must be named constants with a comment explaining why they exist and what device/firmware validated them.

### Runtime reload

Changes from the menu should apply without closing the book when safe.

- Re-read settings from disk before rebuilding.
- Reuse existing adapters/lifetimes where possible.
- Remove only Kobo Tweaks-owned header/footer containers/layouts.
- Rebuild only the affected Kobo Tweaks UI.
- Trigger an explicit typed refresh/update path rather than string-based `QMetaObject::invokeMethod` when the target code is under our control.
- Do not append duplicate QSS rules on every reload. Rebuild the effective stylesheet from a stable base or replace owned rules deterministically.
- Verify repeated changes do not produce unbounded stylesheet, QObject, signal-connection, timer, or memory growth.

### `/mnt/onboard` and USB safety

The Kobo user-storage filesystem can disappear or be exported during USB mass-storage mode.

- Do not keep file handles on `/mnt/onboard` open longer than necessary.
- Avoid polling files aggressively.
- Perform IO in short, bounded operations from controlled Qt callbacks.
- A file watcher/trigger must recover gracefully if the path disappears and returns.
- Never risk corruption by assuming `/mnt/onboard` is permanently mounted.

### Performance and battery

Nickel can stay running for weeks.

- No busy loops.
- No high-frequency polling when an event/signal/watch can be used.
- Avoid unnecessary repaint/full-screen refreshes.
- Avoid repeated allocations that accumulate across menu openings/page turns.
- Timers must have a documented purpose and reasonable cadence.
- Any workaround that prevents sleep/standby or keeps hardware awake must be opt-in, narrowly scoped, and accompanied by a battery-impact discussion/test plan.

## Repository-specific structure

Before editing, locate the relevant subsystem rather than adding a new parallel implementation.

- `src/settings/settings.cc` / `.h`: typed settings model and validation
- `src/settings/settings_keys.h`: canonical settings-key constants
- `src/settings/settings_native_menu.cc`: native offline settings menu
- `src/hooks/`: Nickel lifecycle/hooks and private integration points
- `src/adapters/`: adapters that expose reading state/data to widgets
- `src/widgets/` (if present in the current branch): reading widgets/containers
- `src/common.*`, `src/utils.*`: shared helpers
- `src/patches.*`: patch/hook registration
- `NickelHook/`: upstream framework guidance; do not casually fork its behavior locally

If a new feature logically belongs in an existing subsystem, extend that subsystem instead of creating a second implementation.

## Required workflow before changing code

For every non-trivial change:

1. Read the current implementation and nearby ownership/lifecycle code.
2. Identify whether the change touches private Nickel ABI, GUI lifetime, settings persistence, `/mnt/onboard`, startup, or power behavior.
3. For private Nickel UI/API, compare against NickelMenu or another mature NickelHook mod before coding.
4. If a private symbol/signature/layout is involved, verify it against the target firmware or a known-good reference. Do not guess.
5. Implement the smallest safe change.
6. Add logging for failure paths that would otherwise fail silently.
7. Build with CI/NickelTC-compatible settings.
8. Run host-side static analysis where feasible.
9. Perform real-device smoke testing before merging any change that affects runtime Nickel behavior.

If evidence is insufficient, stop and state what must be inspected rather than inventing a private API contract.

## Build and static-analysis expectations

Final Kobo artifacts must be built with the project/NickelHook toolchain, which is intended to be compatible with NickelTC.

Before considering a change ready:

- CI build must pass.
- Keep compiler warnings at zero where reasonably possible.
- Use `clangd`/`compile_commands.json` for code navigation where useful.
- Use `scan-build`/Clang Static Analyzer for host-side analysis when the code can be compiled that way.
- Use ASan/UBSan only for host-executable/testable portions; do not assume sanitizer builds represent the actual Kobo runtime ABI.
- Do not introduce code that only compiles with modern C++ features unsupported by the NickelTC compiler.

## Real-device smoke-test gate

Changes affecting Nickel UI, dlsym/private symbols, settings, runtime reload, reading state, hooks, or power behavior are **not merge-ready until tested on the physical Kobo Sage**.

At minimum for settings/UI changes test:

1. Open a book in Nickel.
2. Open `Ajustes Kobo Tweaks` with Wi-Fi disabled.
3. Change 12/24-hour clock and confirm persistence after reopening the menu.
4. Add/remove/move chapter progress and remaining-time widgets across zones.
5. Change margins/spacers/widget spacing.
6. Confirm changes apply live without closing/reopening the book.
7. Make many consecutive changes to catch growth/leak/lifetime problems.
8. Turn several pages and confirm widgets continue updating.
9. Sleep/wake the device and repeat a menu action.
10. Confirm normal Kobo reading/progress behavior and Grimmory-related sync behavior are unaffected.

For lifecycle/storage changes, also test connect/disconnect USB where relevant.

For power/button fixes, explicitly test long idle, first physical page-button press after idle, sleep/wake, repeated page turns, and battery impact.

## Git and PR policy

- Do substantial work on a feature/fix branch, not directly on `main`.
- Keep changes scoped; do not mix unrelated refactors with a risky runtime behavior change unless necessary.
- CI must be green before device testing/merge.
- For runtime Nickel changes, wait for real-device confirmation before merge.
- Prefer squash merge after experimental iterations so `main` keeps a readable history.
- Do not delete recovery/uninstall/failsafe paths to simplify code.
- When changing a private-symbol dependency, document the supported/validated firmware in the PR.

## Failure behavior

Prefer graceful degradation over runtime crashes.

Examples:

- Optional menu symbols missing -> disable native settings menu, log the missing capability, and use an existing safe dialog/log path when available.
- Invalid persisted setting -> clamp/fallback to a known default and log the correction.
- Missing reading view during a reload -> abort the reload rather than dereferencing a stale pointer.
- Storage temporarily unavailable -> retry only when a normal event indicates it is available; do not busy-poll.

A startup failure that intentionally triggers NickelHook failsafe is appropriate only when continuing would be more dangerous than disabling the mod.

## Things agents must not do without explicit approval

- Do not merge runtime changes before physical-device validation.
- Do not modify Kobo account/API/Grimmory endpoint configuration.
- Do not replace Nickel as the reading application.
- Do not add mandatory network access to settings or reading features.
- Do not globally disable Kobo power management as a shortcut to a button/power bug.
- Do not introduce a second settings schema when the existing schema can be extended.
- Do not guess private Nickel symbols, signatures, object sizes, offsets, or ownership.
- Do not silently broaden firmware support claims beyond devices/versions actually validated.

## Definition of done

A change is done only when all applicable items are true:

- Current code and reference implementations were inspected.
- Ownership/lifetime and ABI risks were considered.
- Settings remain backward-compatible or include an explicit migration.
- Failure paths are safe and logged.
- CI passes with the Kobo toolchain.
- Static analysis was run when practical.
- Runtime/resource growth was considered for long-lived Nickel execution.
- Physical Sage test passed for runtime changes.
- README/user-facing docs were updated when behavior/install/configuration changed.
- The PR states what firmware/device was actually validated.
