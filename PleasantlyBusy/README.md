# Pleasantly Busy

A Python sound generator for warm, pleasantly busy listening: steady bass,
soft syncopated percussion, interlocking plucked notes, and a gentle rain texture.
Use the local browser studio to adjust your mix, or export stereo WAV files
from the command line. All audio is synthesized in Python with NumPy; playback
uses your browser. There are no audio samples, accounts, external services, or
runtime network requests.

## Start the listening studio

Requires **Python 3.10+** and NumPy. Run these commands from this directory.

**Windows (PowerShell)**

```powershell
py -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
.venv\Scripts\python main.py serve --open
```

**macOS / Linux**

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python main.py serve --open
```

The studio runs at **http://127.0.0.1:8765**. Generate a mix, then press play.
Nothing plays automatically. Stop the server with `Ctrl+C`; use `serve --port 8766`
if the default port is occupied. Running `main.py` without a subcommand also
starts the server, without opening a browser.

## Controls

| Control | Effect |
| --- | --- |
| Easygoing / In the groove / Playfully busy | Set busyness to 20%, 55%, or 85% while keeping your other settings |
| Busyness | Adds bass accents, shaker hits, and syncopated notes around a steady pulse |
| Tempo | 60–160 beats per minute; independent of note pitch |
| Warm bass | Balance of low notes and a soft kick |
| Soft percussion | Balance of shakers and rounded wooden taps |
| Plucked notes | Balance of the repeating minor-pentatonic melody and quiet echoes |
| Rain texture | Balance of smoothed noise and a slow tonal background |
| Stereo width | Spread of the layers; zero makes both channels identical |
| Length | 15 seconds to 5 minutes in the studio; 2–300 seconds in the CLI |
| Pattern seed | Reproduce a pattern; the shuffle button chooses a new seed |
| Playback volume | Immediate browser volume adjustment; does not change the downloaded WAV |

Click **Generate soundscape** to apply sound changes. The player and download
continue to refer to the previous mix until a new render succeeds. The WAV
download contains the exact audio in the player. Repeat replays the file,
including its gentle opening and closing fades; it is not a seamless loop.

Layer sliders set the relative balance of the mix. Turning every layer off
produces silence. Density variants use one constant gain per render to target
similar RMS levels; perceived loudness can still vary with the chosen sounds.

## Export without the browser

Use the Python executable from your virtual environment in place of `python`:

```sh
python main.py render --output my-groove.wav --duration 60 --complexity 0.65 --tempo 108 --seed 42
python main.py render --output soft.wav --duration 120 --complexity 0.2 --texture 0.4 --melody 0.5
python main.py render --output centered.wav --movement 0 --sample-rate 48000
```

The CLI accepts `--bass`, `--percussion`, `--melody`, `--texture`, `--movement`,
and `--volume` from 0 to 1. `--volume` changes the actual exported level and
defaults to 0.8. Supported sample rates are 22050, 44100 (default), and 48000 Hz.
Output is 16-bit stereo PCM WAV. Use `--force` to replace an existing file;
without it, existing files are preserved. The output directory must exist.

The same settings, seed, and dependency versions reproduce the same file.
The tool prints the duration, sample rate, peak, and RMS after exporting.

## Sound design and limits

The steady pulse is surrounded by moderate rhythmic variation, a recurring
melody, small phrase changes, and soft stereo placement. Voices use rounded
attacks and decaying envelopes. A short fade at each end avoids abrupt starts
and stops. No binaural-beat or special-frequency effect is claimed.

This is a listening experiment. It has not been evaluated as an ADHD treatment
or as a way to "reprogram" the brain. The design was informed by research on
musical pleasure: [Witek et al. (2014)](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0094446)
found the highest pleasure and desire-to-move ratings at intermediate
syncopation levels in their listening study. That result does not establish
attention benefits from this generator. Preferences and responses differ.

Start at a comfortable playback level. The renderer targets -20 dBFS RMS at
`--volume 1` and limits sample peaks to 0.85 full scale before applying volume.
This is digital headroom, not a measurement or guarantee of headphone/speaker
sound pressure. Try comparing how a mix feels with how easily you stay with
your chosen task.

## Development

```sh
python -m unittest -v
```

Tests cover WAV metadata, supported sample rates, fades, clipping, RMS consistency,
seed reproducibility, volume and mono controls, isolated layers, invalid input,
HTTP rendering and request boundaries, and CLI overwrite protection.

- `soundscape.py`: validated settings and two-pass, bounded-memory synthesis.
- `main.py`: CLI and localhost HTTP server, using Python's standard library.
- `static/`: browser controls, player, and download interface.
- `test_soundscape.py`: tests; no extra test dependency.

The server binds only to `127.0.0.1`, serves an explicit asset list, checks the
Host/Origin, limits render input to 4 KB, and allows one render at a time. It is
a local tool and should not be exposed as a public web server. CLI synthesis
uses memory proportional to a few seconds of audio; the browser endpoint also
holds the encoded WAV in memory until it is sent.
