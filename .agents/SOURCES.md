# Sources for Kobo Tweaks agent work

Use this catalogue together with the repository-root `AGENTS.md`.

## Primary Kobo/Nickel references

### NickelHook
- Repository: https://github.com/pgaskin/NickelHook
- Local copy/submodule: `NickelHook/`
- Use for: lifecycle, failsafe, dlsym guidance, toolchain expectations, long-lived plugin safety, ABI cautions, `/mnt/onboard` safety, Qt/C preference.

### NickelMenu
- Repository: https://github.com/pgaskin/NickelMenu
- Use for: native menu implementation, `NickelTouchMenu`, `MenuTextItem`, tap gesture registration, private Nickel constructors/destructors, symbol checks, compatibility patterns.
- Treat this as the main reference implementation for the native settings menu.

### NickelDBus
- Repository: https://github.com/shermp/NickelDBus
- Use for: NickelHook lifecycle patterns, observing/controlling Nickel, signal integration, private API handling.

### NickelClock
- Repository: https://github.com/shermp/NickelClock
- Use for: compact reading-view modification patterns and long-lived reading UI behavior.

### NickelSeries / pgaskin kobo-mods
- Repository: https://github.com/pgaskin/kobo-mods
- Use for: smaller focused NickelHook patterns and conservative private-API use.

### Other useful comparison projects
- https://github.com/redphx/nickel-screensaver
- https://github.com/nicoverbruggen/NickelTypeFix
- https://github.com/nicoverbruggen/NickelHome
- https://github.com/nicoverbruggen/NickelDissolve

Use these as comparative evidence, not automatic authority. Check their firmware assumptions before borrowing implementation details.

## Firmware/binary truth

For private Nickel ABI, the real target firmware wins over assumptions in any repository.

Current validated target:
- Kobo Sage
- Firmware 4.38.23552

Useful inspection tools:
- `nm`
- `readelf`
- `objdump`
- `c++filt`
- Ghidra

Verify:
- mangled symbol names
- constructor/destructor variants
- function signatures/calling conventions
- vtable/class assumptions
- symbol presence on the exact firmware

Never infer a private ABI contract only from a newer/older firmware or another device.

## Qt references

Use official Qt documentation for the actual APIs involved, especially:
- QObject ownership and object trees
- `QPointer`
- signals/slots
- event-loop and timer behavior
- `QSettings`
- `QFileSystemWatcher`
- layouts/widgets
- GUI-thread rules

Do not assume Qt 6-only APIs or semantics are available on the Kobo runtime.

## C++ ABI references

KDE binary compatibility guidance:
- https://community.kde.org/Policies/Binary_Compatibility_Issues_With_C%2B%2B
- https://community.kde.org/Policies/Binary_Compatibility_Examples

Use especially when interacting with private C++ objects from `libnickel`.

## General C++ references

C++ Core Guidelines:
- https://github.com/isocpp/CppCoreGuidelines

cppreference:
- https://en.cppreference.com/

Use for ownership, UB avoidance, RAII, pointer safety, const-correctness, and language semantics. If generic modern-C++ advice conflicts with NickelHook compatibility requirements, prefer the NickelHook-compatible solution.

## Analysis/tooling references

Clang Static Analyzer / scan-build:
- https://clang-analyzer.llvm.org/

AddressSanitizer:
- https://clang.llvm.org/docs/AddressSanitizer.html

UndefinedBehaviorSanitizer:
- https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html

Use sanitizers only on host-buildable/testable portions. The final artifact still needs the Kobo/Nickel-compatible toolchain.

## Community sources

Use MobileRead, GitHub issues/discussions, Reddit, and blogs to find:
- device-specific bugs
- firmware regressions
- practical reproduction steps
- hardware quirks
- historical context

Do not use community posts alone to decide ABI signatures, memory ownership, object sizes, or Qt lifetime rules.

## Evidence rule

For a risky change, an agent should be able to state which evidence supports it.

Examples:
- Native menu change -> compare with NickelMenu and verify local symbol abstraction.
- Private symbol change -> verify target firmware binary or a known-good same-firmware reference.
- QObject lifetime change -> confirm Qt ownership semantics and inspect actual parent relationships in code.
- Cross-firmware claim -> provide explicit evidence for each claimed supported version/device, otherwise state only the version physically validated.
