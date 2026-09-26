# Drift EQ desktop — development preview

Drift applies small, smoothly randomized changes to the tone of existing audio. These native Windows and macOS apps share a C++17 processing engine. They include Subtle, Wander, and Soft pulse presets, depth/timing/output controls, bypass, output-device selection, and saved preferences.

This is a development preview. Windows uses a separately installed virtual audio cable. macOS uses the operating system's audio-capture permission. The applications do not record or upload audio, change the system's default output, or start processing automatically at login.

## Native Windows effect without a cable

A separate [APO developer preview](windows/apo/README-APO.md) now builds the EQ directly as a Windows endpoint effect, with its own controller and setup/removal tools. Its unsigned package is for isolated DLL testing; signed system deployment and hardware validation remain pending. The instructions below still describe the existing VB-CABLE app.

## Windows 11: first run

1. Install [VB-CABLE from VB-Audio](https://vb-audio.com/Cable/) using its instructions. Driver installation requires administrator privileges and may require a restart. VB-CABLE is a separate product and is not bundled here.
2. Run `DriftEQ.exe`. Select **CABLE Output** as input and your actual headphones/speakers as output. Cable playback endpoints are excluded from the output picker to prevent an obvious feedback loop.
3. Click **Start**. Check that the status says Drift is processing.
4. Open **Windows Sound settings** from the app and set the Windows playback output to **CABLE Input**. Apps using the default device now feed the processor. If an app has its own device selection, select CABLE Input there too.
5. Play music and start with Subtle. **Bypass EQ** keeps the audio route running with the same shared headroom/output level.

The endpoint names are intentionally confusing: **CABLE Input** is where other apps play audio; **CABLE Output** is where Drift captures it.

### Restore normal Windows audio

**Before stopping or quitting Drift, select your real headphones/speakers as the playback output in Windows Sound settings.** Drift deliberately does not use undocumented APIs to change this setting automatically.

Closing the window hides it in the tray and keeps processing. Double-click the tray icon to reopen it. Quit warns when the default output is still a cable. If Drift crashes or the output disconnects, open Windows Sound settings and select your physical device to restore playback. Some applications may need their individual output reset as well.

The app uses shared-mode WASAPI at 48 kHz stereo with Windows format conversion. It accepts active cable capture endpoints and physical playback choices; it does not record the microphone by default. The first version is intended for VB-CABLE. Other virtual audio drivers are not verified. Per-app exclusive/ASIO/direct-device routes can bypass the cable, and protected content may behave differently.

## macOS 14.2+: first run

1. Build `DriftEQ.app` with the commands below, or use a development archive produced by the workflow.
2. Move the app to a stable location, then open it.
3. Select your headphones/speakers and click **Start**.
4. Allow the system-audio capture permission requested by macOS. If it was denied, use System Settings → Privacy & Security and the audio/screen-recording permission section; the exact label varies by macOS version. Restart the app after changing permission if necessary.
5. Play audio from another app. The system tap excludes Drift's own process, processes the mixed stereo audio, and suppresses the original while the tap is active.

**Stop routing** destroys the tap and restores normal source playback. Closing the window leaves Drift available in the menu bar. Quit also releases the route. Device disconnects, format changes, or stalled callbacks stop processing; select a working device and Start again. Before sleep, the app stops routing; it does not restart automatically on wake.

macOS builds are for development: workflow bundles have only an ad-hoc signature, not a Developer ID signature or Apple notarization. Consumer distribution needs the maintainer's signing credentials and a notarization step. Building locally is the supported development path. Do not disable Gatekeeper globally.

The tap path must be exercised on a real Mac with permission granted. Compilation and DSP tests alone do not prove system-audio capture, Bluetooth behavior, or protected-content compatibility.

## Build from source

There are no third-party runtime libraries. The Windows cable executable uses the static MSVC runtime. The separate APO DLL uses the dynamic Microsoft runtime.

### Windows

Install Visual Studio 2019 or newer with **Desktop development with C++**, a Windows SDK, and CMake. In a developer terminal, from the repository root:

```powershell
cmake -S DriftEQ -B DriftEQ/build-windows -A x64
cmake --build DriftEQ/build-windows --config Release --parallel
cd DriftEQ/build-windows
ctest -C Release --output-on-failure
```

Run `DriftEQ/build-windows/Release/DriftEQ.exe` relative to the repository root. For device enumeration without capture or playback:

```powershell
.\DriftEQ\build-windows\Release\DriftEQ.exe --diagnostics "$PWD\devices.txt"
```

### macOS

Use macOS 14.2+, Xcode 15.3+ with a suitable macOS SDK, and CMake 3.16+:

```sh
cmake -S DriftEQ -B DriftEQ/build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build DriftEQ/build-macos --parallel
(cd DriftEQ/build-macos && ctest --output-on-failure)
open DriftEQ/build-macos/DriftEQ.app
```

For a universal Apple Silicon/Intel build, add `'-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'` to configuration. GitHub Actions builds both architectures, runs the native tests on the runner's architecture, and packages the app.

### Core tests on Linux

Linux builds the shared engine and tests; a Linux desktop adapter is not included.

```sh
cmake -S DriftEQ -B DriftEQ/build-core -DCMAKE_BUILD_TYPE=Release
cmake --build DriftEQ/build-core --parallel
(cd DriftEQ/build-core && ctest --output-on-failure)
```

## Processing and implementation

- `core/Engine.*`: five broad peaking filters (120/400/1,200/3,500/9,000 Hz, Q 0.75), randomized drift, tonal pulse, smoothed parameters, and linked stereo processing.
- `core/AudioQueue.h`: bounded single-producer/single-consumer stereo buffer, interpolation to compensate small clock differences, startup buffering, and underrun/overrun counters.
- `windows/`: Win32 tray interface and event-driven WASAPI capture/render worker.
- `macos/`: Cocoa menu-bar interface, private Core Audio tap/aggregate, and HAL output Audio Unit.
- `tests/`: deterministic native processing and queue checks; no audio device is required.

The real-time paths use preallocated storage. UI controls are read through lock-free atomics; no UI callbacks, file I/O, or logging occur in audio callbacks. macOS capture and output callbacks communicate through the bounded queue. Windows handles capture and output on one audio worker.

Both dry and processed audio retain **-12 dB headroom** and the chosen output gain. Bypass is not bit-perfect passthrough or perceptual loudness matching. The native engine adds a linked **sample-peak guard at 0.98**, with immediate attenuation and gradual release; this is not an oversampled true-peak limiter. The browser prototype does not have this guard. Motion has additional native smoothing and is similar to, not sample-identical with, the browser prototype.

The adaptive buffer starts around 40 ms on macOS and at least 20 ms (usually more) on Windows, depending on endpoint buffers. Total latency also includes OS/device buffers and Bluetooth. There is no measured latency guarantee in this preview. Resampling handles small clock mismatch, not arbitrary unsupported formats.

## Validation and remaining release work

Automated tests cover multiple sample rates, both modes, deterministic seeds, zero-depth and bypass behavior, channel consistency, rapid control changes, block-size independence, non-finite input, sample-peak containment, and concurrent queue pressure. The workflow builds Windows x64 and universal macOS archives and runs those tests.

Before calling this a consumer release, complete real-device acceptance testing:

- Windows: routed browser/music playback through VB-CABLE; bypass; restore output; device unplug; long sessions; sleep/resume; Bluetooth.
- macOS: first-run permission; capture of several apps; own-output exclusion; stopping and quitting; device/format changes; sleep/resume; Bluetooth.
- Measure end-to-end latency and CPU use on representative hardware.
- Add signed installers/notarization, icons, accessibility/DPI polish, automatic recovery where reliable, and optional start at login.

Settings are local: `%LOCALAPPDATA%\DriftEQ\settings.ini` on Windows and the app's user defaults on macOS. There is no telemetry. The intended experience is gentle stimulation and relaxation; these builds do not establish an ADHD treatment benefit.
