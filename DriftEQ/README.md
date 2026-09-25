# Drift EQ

**Calm chaos for familiar sound.** Drift is a small listening experiment that applies smooth, randomized EQ changes to music or other audio. The goal is to explore a balance between soothing sound and gentle stimulation.

The project now includes native desktop development previews plus the original browser listening prototype:

- **[Windows and macOS desktop apps](native/README.md):** shared C++ engine, presets, tray/menu-bar controls, and native audio routing. Windows requires a separately installed virtual cable; macOS requires audio-capture permission.
- **[Browser prototype](index.html):** open a local audio file or play the built-in demo. This page processes only its own audio.

The native adapters require real-device acceptance testing before a consumer release. All-app mobile filtering through ordinary app stores remains outside the implemented scope.

The idea was motivated by an interest in attention and ADHD. Whether this effect improves focus or relaxation is an open question; no therapeutic benefit has been established by this project.

## Browser quick start

1. Download this folder, or clone the repository.
2. Open [index.html](index.html) in a current desktop browser. GitHub's file viewer shows source, so open your downloaded copy locally.
3. Click **Play gentle demo**, or choose an audio file and press play in the audio player.
4. Start with **Subtle**, then compare **Drift on** with **Original**.

There is no build step, account, package installation, or server requirement. The page works offline and makes no network requests. Audio files stay on your device.

If your browser restricts opening local HTML, optionally serve the folder with Python 3:

```sh
cd DriftEQ
python -m http.server 8000 --bind 127.0.0.1
```

Then open [localhost:8000](http://localhost:8000). Use `python3` instead of `python` if that is your Python command.

WAV and MP3 are useful starting formats. Other formats depend on the browser's codecs. Mobile layout support does not imply verified mobile audio support; browser background playback and file opening vary by platform.

## Presets and controls

| Preset | Maximum movement per band | Timing | Character |
| --- | --- | --- | --- |
| Subtle | ±0.8 dB | About 6 seconds between targets | Small, slow tonal shifts |
| Wander | ±1.4 dB | About 10 seconds between targets | More noticeable, slower movement |
| Soft pulse | ±0.5 dB | 1.5 seconds per pulse | Gentle tonal pulses with random depth and direction |

Soft pulse is not synchronized to the music's beat. It modulates EQ, not a separate volume tremolo.

- **Maximum movement per band:** 0–2 dB. Zero removes EQ movement. Overlapping bands can produce a combined response larger than one band's setting.
- **Timing:** 0.5–16 seconds. In drift modes this is the typical time between new random targets; in pulse mode it is the pulse period.
- **Drift on / Original:** Compare processed and dry audio using a short crossfade.
- **Output level:** Adjust the shared output gain independently of EQ depth.
- **Loop this file:** Repeat the selected file.
- **Stop all audio:** Stop the demo and pause/reset file playback.
- **Live curve:** See the EQ response; it excludes the shared headroom and output level.

Editing a slider customizes the current motion mode. Presets restore their depth and timing. Settings are not saved between page loads.

## How it works

The browser implementation lives in `index.html`: interface styles, controls, audio graph, motion generation, and curve display. It uses native Web Audio nodes with no external libraries.

```text
Local file or generated demo
          |
  Shared -12 dB headroom
          |
    +-----+--------------------+
    |                          |
  Dry path              Five moving EQ bands
    |                          |
    +---- Comparison crossfade-+
                  |
             Output level
                  |
          Speakers / headphones
```

The five broad peaking filters are centered at **120 Hz, 400 Hz, 1.2 kHz, 3.5 kHz, and 9 kHz**, with **Q = 0.75**. At low sample rates, centers are clamped below Nyquist. The left and right channels share filter parameters, preserving stereo placement.

Drift modes use independently randomized target values and durations, joined with smooth quintic transitions. Soft pulse uses a squared-sine envelope with new random signed amplitudes at pulse boundaries. Changing a setting starts from the current shape, so reducing depth may briefly retain values from the previous range while transitioning.

Filter automation is scheduled 20 minutes ahead and refreshed periodically. This keeps short UI stalls and ordinary timer throttling from interrupting the movement. If a browser freezes the page for longer than the scheduled window, the EQ holds its final shape until scheduling resumes.

The dry and processed paths share the same -12 dB headroom and output gain. They are **not perceptually loudness-matched**, and this headroom is **not a true-peak limiter**. The 120 ms bypass crossfade can briefly differ from the settled response shown in the chart.

The demo combines filtered noise and soft musical tones. File mode adds no extra sound.

## Scope and mobile feasibility

The intended longer-term product is an all-app mobile filter installed through ordinary app stores. The desktop implementations do not solve that mobile goal, and current platform access is a material constraint:

| Platform | Supported direction | Limitation |
| --- | --- | --- |
| Windows / macOS | Native adapters are included; see the [desktop guide](native/README.md) | Virtual-cable setup on Windows, capture permission on macOS, and real-device compatibility testing |
| Android | EQ for compatible audio sessions | Universal all-app coverage cannot be guaranteed; global output-mix EQ through session 0 is deprecated |
| iPhone / iPad | In-app audio processing or an AUv3 effect in a compatible host | Ordinary apps have no supported universal EQ insertion point for every other app's output |

A responsive browser page or a native wrapper does not remove these OS restrictions. System integration, rooted devices, and external DSP hardware are outside the ordinary-app-store-only requirement.

Primary platform references:

- [Android Equalizer and audio sessions](https://developer.android.com/reference/android/media/audiofx/Equalizer)
- [Android playback capture restrictions](https://developer.android.com/media/platform/av-capture)
- [Android system/device audio effects](https://source.android.com/docs/core/audio/audio-effects)
- [Apple Audio Unit app extensions](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html)
- [Apple Core Audio process taps for macOS](https://developer.apple.com/documentation/coreaudio/capturing-system-audio-with-core-audio-taps)
- [Microsoft Audio Processing Objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture)

## Validation and contributing

The initial prototype passed automated checks in headless Microsoft Edge for:

- Demo start/stop, preset selection, bypass switching, and local WAV playback.
- Layout overflow at 320, 390, 736, and 1,100 pixels wide.
- Bounded, deterministic motion for both modes at multiple speeds.
- Offline audio rendering at 16, 44.1, and 48 kHz: finite samples, matching left/right output for identical input, and transparent zero-depth processing apart from the documented headroom.
- No clipping for the tested stress signal and no JavaScript page errors.

These checks do not establish compatibility across all browsers, devices, source material, or background conditions. The one-off validation harness is not included in this folder.

To contribute, edit `index.html` directly and include the browser/device used to check your change. For audio changes, verify playback, rapid control changes, zero depth, bypass transitions, and both motion modes. For interface changes, check keyboard access and narrow layouts. Describe subjective listening results separately from technical correctness or clinical claims.

## License

This tool is covered by the repository's [Apache 2.0 license](../LICENSE).
