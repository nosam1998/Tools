# Drift EQ — native Windows APO preview

This version places Drift's EQ engine directly in a Windows playback endpoint's audio-effects path. It does not capture/replay system audio or require VB-CABLE. The existing cable app and Mac app remain available separately.

**Current status: a built and tested developer package, not a ready-to-enable consumer release.** The package contains an unsigned APO DLL, a native control app, a DLL test harness, and conservative setup/removal scripts. Setup deliberately refuses unsigned payloads. The tests exercise the DLL inside a separate test process; they do not prove it loads inside Windows Audio Device Graph Isolation (`audiodg.exe`) or works on physical sound hardware.

## Try the package without changing your audio setup

Extract the archive into one folder. In that folder, run:

```powershell
.\drift_apo_tests.exe "$PWD\DriftApo.dll"
.\Setup.ps1 -Mode Inspect | Format-List
.\DriftControl.exe
```

The test harness does not register the DLL, install an effect, capture sound, or play sound. Inspect reads the active playback endpoints and reports existing effects. The control app lists devices; its controls stay disabled until that device has been set up. Close hides it in the tray. Quit closes the controls.

The APO uses Microsoft's dynamic C++ runtime. A machine without the Visual C++ x64 runtime needs the [supported Microsoft redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist). The cable application continues to use the static runtime.

## Signed deployment and removal

For a project without an existing publisher certificate or Hardware Developer account, start with the [release and signing guide](SIGNING.md).

These commands are for a suitably signed build after device compatibility review. They do not make an unsigned preview installable.

1. Run `Setup.ps1 -Mode Inspect | Format-List` and select the full playback endpoint ID for the intended headphones/speakers.
2. In an administrator **64-bit** PowerShell, use the ID from Inspect:

   ```powershell
   $deviceId = 'paste the full playback endpoint ID from Inspect here'
   .\Setup.ps1 -Mode Install -EndpointId $deviceId
   ```

3. Restart Windows. Keep the physical headphones/speakers selected as your normal output. Enable Windows audio enhancements for that device, then open the installed `DriftControl.exe` and enable Drift.
4. Verify that audio plays correctly, the presets make a subtle change, and disabling restores unchanged playback. The UI confirms saved settings, not successful loading inside the audio engine.
5. To remove the attachment, use the same endpoint ID:

   ```powershell
   .\Setup.ps1 -Mode Remove -EndpointId $deviceId
   ```

   Restart Windows to unload the DLL. When every endpoint has been removed and Windows has restarted, the `DriftEQ-APO` folder under Program Files may be deleted.

Setup requires valid signatures on the DLL and control app. An ordinary Authenticode signature is a precondition, **not proof** that the DLL satisfies protected-audio loading requirements. Production deployment needs an appropriate signing/certification path and actual `audiodg.exe` validation. This package does not change protected-audio policy, driver-signing policy, registry ownership, or driver permissions.

This is a conservative compatibility installer for endpoints with an existing writable effects store. It refuses to replace existing endpoint effects, composite endpoint chains, or legacy post-mix effects. Stream/mode effects are left in place. Driver-owned or componentized endpoints may need a device-specific signed extension package instead. Supporting those devices is additional integration work, not an automatic fallback in this preview.

Setup stores binaries and recovery records under `%ProgramFiles%\DriftEQ-APO\0.3.0`. It backs up the exact presence, types, and contents of the two endpoint properties it changes. Failure attempts to restore them; backups remain if restoration fails. Removal refuses to overwrite properties changed by a driver or another installer. It neither restarts the audio service nor changes the default playback device.

## Default playback device

The first device-menu item is **System default — [device name]**. It follows the normal Windows playback default while the controller is open (including while hidden in the tray), checking about once a second. Choosing a named device keeps the controls on that endpoint. The selection is saved for the current Windows user at `HKCU\SOFTWARE\DriftEQ\Controller`.

Following the default loads the new endpoint's existing settings. It does not move the APO installation or copy the previous device's preset or enabled state. If that endpoint is not set up, the controls are disabled and the app explains why. Each endpoint must have the signed effect installed separately. After quitting the controller, Windows still uses each endpoint's installed effect and saved configuration.

## Controls and sound

- **Subtle, Wander, Soft pulse:** the same presets as the desktop engine, with smooth random EQ movement.
- **Movement, timing, output level:** saved for the selected endpoint.
- **Disable effect:** a 50 ms transition to bit-exact passthrough for valid float input. Registry delivery adds up to approximately 100 ms under normal scheduling. This restores the original playback level, which can be louder than the processed signal.
- Closing or quitting the controller does not disable an installed APO. The effect runs with the Windows audio engine.

The effect accepts matching input/output float32 formats, 1–32 channels, and 8–192 kHz. Channel pairs share the same tonal motion. Mono is processed through a duplicated pair and returned as mono. Surround channels retain their positions; there is no upmix or downmix. The sample-peak guard operates per pair, and applies to the processed path; bypass preserves the source. There is no true-peak limiter.

The processed path retains the engine's -12 dB headroom and selected output gain. The adapter adds no audio queue, lookahead, or sample-rate conversion. That does not imply zero total device latency, and the EQ has frequency-dependent phase response.

Settings are machine-wide per playback endpoint at `HKLM\SOFTWARE\DriftEQ\APO\Devices\{endpoint-guid}`. Setup grants local users read/write access only to that dedicated settings key. A single versioned DWORD holds all controls. Unknown or missing settings select unchanged audio. There is no telemetry or audio recording.

## Implementation and validation

`DriftApo.cpp` implements COM activation, APO initialization, format negotiation, buffer validation, processing, and effect discovery. `Processor.h` adapts the shared EQ engine to interleaved mono/stereo/surround audio. `Settings.cpp` reads configuration on a separate worker; `APOProcess` only takes an atomic settings snapshot and processes preallocated state. No registry access, waiting, allocation, or file I/O occurs in the audio callback.

The Windows tests load the actual DLL without global registration, using COM format/property providers in the test process. They cover activation/unloading, initialization/locking errors, float format negotiation, channel counts, frame limits, in-place/out-of-place equality, aliased connection metadata, silence/tails, bypass, enable/disable ramps, and settings validation. Setup tests exercise backup/restore and conflict detection against a unique temporary **current-user** registry key, never machine audio settings.

Build from the repository root with Visual Studio 2019+ C++ tools, Windows SDK and CMake:

```powershell
cmake -S DriftEQ -B DriftEQ/build-windows -A x64
cmake --build DriftEQ/build-windows --config Release --parallel
cd DriftEQ/build-windows
ctest -C Release --output-on-failure
```

New outputs are `Release/DriftApo.dll`, `Release/DriftControl.exe`, and `Release/drift_apo_tests.exe`. GitHub Actions packages them with this guide and the setup scripts.

Before a consumer release, complete audio-compatible signing, installer validation in a VM, physical-device testing, coexistence with vendor enhancements, hotplug/sleep/restart, long sessions, and latency/CPU measurements. Exclusive/ASIO, hardware-offloaded, spatial, protected, and driver-specific paths require explicit compatibility tests. Do not promise filtering of every possible Windows audio path.

Architecture references: [Microsoft APO architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture), [implementation and componentized deployment](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects).
