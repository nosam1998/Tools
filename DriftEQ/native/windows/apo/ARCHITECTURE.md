# Windows native EQ integration

Status: implemented developer preview; system deployment has not been validated.

## Decision

Use an endpoint Audio Processing Object (EFX) containing the existing C++ EQ engine. Keep the WASAPI cable adapter as a separate executable. The user-facing APO controller selects a physical playback endpoint and writes that endpoint's effect settings; it never captures audio or changes the default device.

```text
Apps -> Windows audio mix -> existing stream/mode effects -> Drift EFX -> device
                                                             ^
Controller -> per-endpoint DWORD -> non-audio settings worker -> atomic snapshot
```

The adapter exposes the Windows APO COM interfaces directly. It accepts identical float32 input/output formats, validates buffer sizes, and processes in-place or out-of-place. All channel pairs use the same random seed and motion, preserving spatial channel positions. Full disable fades to unchanged samples. No bridge queue or resampler is involved.

## Boundaries

Audio callbacks own filter state and fixed scratch arrays. Settings updates use a single atomic word with version and range validation. Registry reads happen on a stoppable non-audio worker, which is joined before the DLL can be unloaded. Bad or missing settings disable processing. A controller crash does not interrupt playback or remove the user's last selected settings.

Per-endpoint settings are shared between local users. This matches the device-wide effect; per-user behavior would need an explicit session policy and different control architecture. The versioned DWORD reserves bits for future schema changes. A new schema must continue to fail to bypass on old DLLs.

## Deployment tradeoff

An APO is part of the system audio path, not an ordinary portable audio application. This version implements a narrowly scoped compatibility attachment to a writable endpoint effects store, with signature preflight, exact backup, and conflict-aware restoration. It does not wrap an existing EFX, take registry ownership, or weaken protected-audio loading policy. It therefore rejects some otherwise common endpoints instead of disabling their vendor effects.

A production product needs audio-compatible signing and device-specific deployment validation. Modern componentized drivers can require signed driver extension packages; this repository does not claim that the compatibility script is a universal substitute for those packages. Revisit composite effect chaining or OEM integration only after hardware testing establishes a supported device matrix.

## Recovery and validation

Installation starts disabled. A saved record precedes endpoint mutation, and record updates use atomic file replacement. Rollback only touches the two saved endpoint values and refuses conflicting driver changes. Removal disables the settings, restores the original values, and unregisters the COM class after the last managed endpoint is removed. Files remain until a restart releases DLL references.

Tests load the actual DLL into a dedicated process and exercise COM lifecycle and buffer contracts. Separate setup tests use an isolated user registry key. These tests are necessary but cannot establish real `audiodg.exe` compatibility. Hardware acceptance and properly signed deployment remain release gates.
