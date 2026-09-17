"""Deterministic, gently syncopated stereo synthesis using only NumPy.

Rendering uses two bounded-memory passes: measure the mix, then write PCM with
one constant gain. Density changes are approximately loudness matched without
the pumping caused by normalizing every beat or bar independently.
"""

from dataclasses import dataclass, fields
from pathlib import Path
from typing import BinaryIO, Iterator
import math
import wave

import numpy as np


@dataclass(frozen=True)
class Settings:
    duration: float = 30.0
    tempo: float = 108.0
    complexity: float = 0.55
    bass: float = 0.75
    percussion: float = 0.55
    melody: float = 0.65
    texture: float = 0.30
    movement: float = 0.45
    volume: float = 0.8
    seed: int = 42
    sample_rate: int = 44100

    def __post_init__(self) -> None:
        bounds = {
            "duration": (2, 300),
            "tempo": (60, 160),
            **{name: (0, 1) for name in (
                "complexity", "bass", "percussion", "melody", "texture",
                "movement", "volume",
            )},
        }
        for name, (low, high) in bounds.items():
            value = getattr(self, name)
            if (isinstance(value, bool) or not isinstance(value, (int, float))
                    or not math.isfinite(value) or not low <= value <= high):
                raise ValueError(f"{name} must be a finite number from {low} to {high}")
        if type(self.seed) is not int or not 0 <= self.seed <= 2**32 - 1:
            raise ValueError("seed must be an integer from 0 to 4294967295")
        if type(self.sample_rate) is not int or self.sample_rate not in (22050, 44100, 48000):
            raise ValueError("sample_rate must be 22050, 44100, or 48000")

    @classmethod
    def from_dict(cls, values: dict) -> "Settings":
        if not isinstance(values, dict):
            raise ValueError("settings must be a JSON object")
        unknown = set(values) - {field.name for field in fields(cls)}
        if unknown:
            raise ValueError("Unknown settings: " + ", ".join(sorted(unknown)))
        return cls(**values)


def _frequency(midi: int) -> float:
    return 440.0 * 2 ** ((midi - 69) / 12)


def _envelope(t: np.ndarray, attack: float, decay: float) -> np.ndarray:
    """Zero endpoints and a rounded attack keep note boundaries quiet."""
    onset = np.sin(np.minimum(t / attack, 1.0) * np.pi / 2) ** 2
    release = np.minimum((t[-1] - t) / 0.04, 1.0).clip(0, 1)
    return onset * np.exp(-t / decay) * release**2


def _tone(midi: int, sr: int, kind: str) -> np.ndarray:
    is_bass = kind == "bass"
    t = np.arange(round(sr * (1.25 if is_bass else 1.6))) / sr
    phase = 2 * np.pi * _frequency(midi) * t
    signal = np.sin(phase) + 0.22 * np.sin(2 * phase) + 0.06 * np.sin(3 * phase)
    return (signal * _envelope(t, 0.018 if is_bass else 0.009,
                               0.29 if is_bass else 0.36)).astype(np.float32)


def _percussion(sr: int, kind: str, rng: np.random.Generator) -> np.ndarray:
    duration = {"kick": 0.30, "wood": 0.11, "shaker": 0.105}[kind]
    t = np.arange(round(sr * duration)) / sr
    if kind == "kick":
        # A smooth falling sine, with no sharp attack or added click.
        phase = 2 * np.pi * (46 * t + 19 * 0.035 * (1 - np.exp(-t / 0.035)))
        signal = np.sin(phase)
        attack, decay = 0.009, 0.075
    elif kind == "wood":
        signal = 0.7 * np.sin(2 * np.pi * 460 * t) + 0.3 * np.sin(2 * np.pi * 710 * t)
        attack, decay = 0.004, 0.020
    else:
        noise = rng.standard_normal(len(t))
        # Smoothed noise keeps the shaker softer than broadband white noise.
        signal = np.convolve(noise, np.ones(9) / 9, mode="same")
        attack, decay = 0.008, 0.026
    return (signal * _envelope(t, attack, decay)).astype(np.float32)


def _add(mix: np.ndarray, sound: np.ndarray, at: int, level: float, pan: float) -> None:
    length = min(len(sound), len(mix) - at)
    if length <= 0 or level == 0:
        return
    # Equal-power panning; pan=0 always produces the same signal in both ears.
    angle = (pan + 1) * np.pi / 4
    mix[at:at + length, 0] += sound[:length] * (level * np.cos(angle))
    mix[at:at + length, 1] += sound[:length] * (level * np.sin(angle))


def _raw_audio(settings: Settings) -> Iterator[np.ndarray]:
    sr = settings.sample_rate
    total = round(settings.duration * sr)
    beat = 60.0 / settings.tempo
    bar_seconds = 4 * beat
    tail_size = round(2.2 * sr)
    carry = np.zeros((tail_size, 2), dtype=np.float32)
    roots = (38, 41, 36, 43)  # D, F, C, G: a repeating, warm minor groove.
    motif = (62, 69, 65, 67, 72, 69, 67, 65)
    tone_cache: dict[tuple[int, str], np.ndarray] = {}

    def tone(note: int, kind: str) -> np.ndarray:
        key = (note, kind)
        if key not in tone_cache:
            tone_cache[key] = _tone(note, sr, kind)
        return tone_cache[key]

    for bar in range(math.ceil(settings.duration / bar_seconds)):
        start = round(bar * bar_seconds * sr)
        if start >= total:
            break
        end = round((bar + 1) * bar_seconds * sr)
        count = end - start
        mix = np.zeros((count + tail_size, 2), dtype=np.float32)
        mix[:tail_size] += carry
        rhythm_rng = np.random.default_rng(np.random.SeedSequence([settings.seed, bar, 1]))
        melody_rng = np.random.default_rng(np.random.SeedSequence([settings.seed, bar, 2]))
        texture_rng = np.random.default_rng(np.random.SeedSequence([settings.seed, bar, 3]))

        def at(step: float) -> int:
            return round((bar * bar_seconds + step * beat / 4) * sr) - start

        if settings.bass:
            for step in (0, 4, 8, 12):
                _add(mix, _percussion(sr, "kick", rhythm_rng), at(step),
                     0.14 * settings.bass, 0)
            for step, strength in ((0, 1.0), (6, 0.67), (11, 0.56), (14, 0.42)):
                if step == 0 or settings.complexity >= {6: 0.12, 11: 0.36, 14: 0.75}[step]:
                    _add(mix, tone(roots[(bar // 2) % 4], "bass"), at(step),
                         0.21 * strength * settings.bass, 0)

        # Draw every candidate before selecting it: more density adds events
        # without scrambling the existing beat, pitch, or stereo arrangement.
        candidates = rhythm_rng.random((16, 3))
        if settings.percussion:
            for step, (chance, accent, position) in enumerate(candidates):
                anchor = step in (2, 6, 10, 14)
                if anchor or chance < settings.complexity * 0.80:
                    pan = (position - 0.5) * 1.25 * settings.movement
                    _add(mix, _percussion(sr, "shaker", rhythm_rng), at(step),
                         (0.07 + 0.045 * accent) * settings.percussion, pan)
            for step in (4, 12):
                _add(mix, _percussion(sr, "wood", rhythm_rng), at(step),
                     0.085 * settings.percussion, -0.24 * settings.movement)

        notes = melody_rng.random((8, 3))
        if settings.melody:
            for index, step in enumerate((0, 3, 6, 7, 10, 12, 13, 15)):
                chance, accent, position = notes[index]
                if step not in (0, 6, 10) and chance >= settings.complexity * 0.85:
                    continue
                note = motif[(index + bar // 2) % len(motif)]
                if bar % 4 == 3 and index == 7:
                    note += 12  # A small phrase-ending variation.
                pan = np.sin(bar * 0.39 + position * 2 * np.pi) * 0.70 * settings.movement
                sound = tone(note, "pluck")
                level = (0.075 + 0.035 * accent) * settings.melody
                _add(mix, sound, at(step), level, pan)
                _add(mix, sound, at(step) + round(beat * 0.75 * sr), level * 0.17, -pan)

        if settings.texture:
            absolute_t = np.arange(start, end) / sr
            noise = texture_rng.standard_normal((count + 64, 2))
            window = np.hanning(65)
            window /= window.sum()
            rain = np.column_stack([
                np.convolve(noise[:, channel], window, mode="valid")
                for channel in (0, 1)
            ])
            # Blend towards mono when stereo movement is reduced.
            rain = rain.mean(axis=1, keepdims=True) + settings.movement * (
                rain - rain.mean(axis=1, keepdims=True)
            )
            breath = 0.8 + 0.2 * np.sin(2 * np.pi * absolute_t / 19)
            pad = (np.sin(2 * np.pi * _frequency(50) * absolute_t)
                   + 0.4 * np.sin(2 * np.pi * _frequency(57) * absolute_t))
            mix[:count] += ((0.06 * rain + 0.012 * pad[:, None])
                            * breath[:, None] * settings.texture).astype(np.float32)

        carry = mix[count:].copy()
        chunk = mix[:min(count, total - start)]
        indices = np.arange(start, start + len(chunk))
        fade_in = np.sin(np.minimum(indices / (0.4 * sr), 1) * np.pi / 2) ** 2
        fade_out = np.sin(np.minimum((total - 1 - indices) / (0.7 * sr), 1) * np.pi / 2) ** 2
        chunk *= (fade_in * fade_out)[:, None]
        yield chunk


def render_wav(destination: str | Path | BinaryIO, settings: Settings) -> dict:
    """Write 16-bit stereo WAV; return measurements in linear full-scale units.

    Output gain targets -20 dBFS RMS at volume=1, with a 0.85 sample-peak ceiling.
    Digital levels do not determine headphone/speaker sound pressure.
    """
    peak = sum_squares = 0.0
    samples = 0
    for chunk in _raw_audio(settings):
        peak = max(peak, float(np.max(np.abs(chunk))))
        sum_squares += float(np.sum(np.square(chunk, dtype=np.float64)))
        samples += chunk.size
    rms = math.sqrt(sum_squares / samples)
    gain = min(0.1 / rms, 0.85 / peak) * settings.volume if peak > 0 else 0.0

    target = str(destination) if isinstance(destination, Path) else destination
    with wave.open(target, "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(settings.sample_rate)
        wav.setnframes(round(settings.duration * settings.sample_rate))
        for chunk in _raw_audio(settings):
            pcm = np.rint(chunk * gain * 32767).astype("<i2")
            wav.writeframesraw(pcm.tobytes())
    return {
        "frames": samples // 2,
        "sample_rate": settings.sample_rate,
        "duration": (samples // 2) / settings.sample_rate,
        "peak": peak * gain,
        "rms": rms * gain,
        "seed": settings.seed,
    }
