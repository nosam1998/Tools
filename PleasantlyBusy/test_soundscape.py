"""Audio, command-line, and local HTTP integration checks (stdlib unittest)."""

from dataclasses import replace
import http.client
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
import wave

import numpy as np

from main import StudioServer
from soundscape import Settings, render_wav


def render(settings: Settings) -> tuple[bytes, np.ndarray, dict]:
    output = io.BytesIO()
    measurements = render_wav(output, settings)
    content = output.getvalue()
    with wave.open(io.BytesIO(content), "rb") as wav:
        frames = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").reshape(-1, 2)
    return content, frames.astype(np.float64) / 32767, measurements


class AudioTests(unittest.TestCase):
    def setUp(self) -> None:
        self.settings = Settings(duration=3, sample_rate=22050)

    def test_wave_metadata_and_smooth_fades(self) -> None:
        for rate in (22050, 44100, 48000):
            with self.subTest(rate=rate):
                content, samples, stats = render(replace(self.settings, sample_rate=rate))
                with wave.open(io.BytesIO(content), "rb") as wav:
                    self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()), (2, 2, rate))
                    self.assertEqual(wav.getnframes(), 3 * rate)
                self.assertEqual(stats["frames"], len(samples))
                self.assertTrue(np.isfinite(samples).all())
                self.assertTrue((samples[[0, -1]] == 0).all())
                self.assertLess(np.abs(samples[:100]).max(), 0.004)
                self.assertLess(np.abs(samples[-100:]).max(), 0.004)
                self.assertGreater(np.sqrt(np.mean(samples**2)), 0.02)
                self.assertLess(np.abs(samples.mean(axis=0)).max(), 0.002)
                self.assertFalse(np.array_equal(samples[:, 0], samples[:, 1]))

    def test_seed_reproducibility_and_variation(self) -> None:
        first, _, _ = render(self.settings)
        repeated, _, _ = render(self.settings)
        changed, _, _ = render(replace(self.settings, seed=43))
        self.assertEqual(first, repeated)
        self.assertNotEqual(first, changed)

    def test_density_is_loudness_matched_and_does_not_clip(self) -> None:
        signals, levels = [], []
        for complexity in (0.2, 0.55, 0.85, 1.0):
            _, samples, _ = render(replace(self.settings, duration=8, complexity=complexity))
            self.assertLessEqual(np.abs(samples).max(), 0.85 * self.settings.volume + 1 / 32767)
            levels.append(np.sqrt(np.mean(samples**2)))
            signals.append(samples)
        self.assertLess(max(levels) / min(levels), 1.03)
        self.assertFalse(np.array_equal(signals[0], signals[-1]))

    def test_volume_zero_layers_and_mono(self) -> None:
        _, silent, _ = render(replace(self.settings, volume=0))
        self.assertTrue((silent == 0).all())
        _, muted, _ = render(replace(self.settings, bass=0, percussion=0, melody=0, texture=0))
        self.assertTrue((muted == 0).all())
        _, mono, _ = render(replace(self.settings, movement=0))
        np.testing.assert_array_equal(mono[:, 0], mono[:, 1])
        _, full, _ = render(replace(self.settings, volume=1))
        _, half, _ = render(replace(self.settings, volume=0.5))
        np.testing.assert_allclose(half, full * 0.5, atol=1 / 32767)

    def test_tempo_edges_and_fractional_duration(self) -> None:
        for tempo in (60, 160):
            for layer in ("bass", "percussion", "melody", "texture"):
                with self.subTest(tempo=tempo, layer=layer):
                    values = {name: 0 for name in ("bass", "percussion", "melody", "texture")}
                    values[layer] = 1
                    _, samples, _ = render(replace(self.settings, tempo=tempo, duration=2.125, **values))
                    self.assertEqual(len(samples), round(2.125 * self.settings.sample_rate))
                    self.assertGreater(np.abs(samples).max(), 0)
                    self.assertLessEqual(np.abs(samples).max(), 0.851)

    def test_duration_rounding_at_bar_boundary(self) -> None:
        duration = 240 / self.settings.tempo + 1e-9
        _, samples, _ = render(replace(self.settings, duration=duration))
        self.assertEqual(len(samples), round(duration * self.settings.sample_rate))
        self.assertTrue((samples[-1] == 0).all())

    def test_invalid_settings_fail_before_rendering(self) -> None:
        cases = (
            {"tempo": float("nan")}, {"complexity": float("inf")}, {"duration": 0},
            {"duration": 301}, {"seed": -1}, {"seed": 2**32}, {"seed": 1.5},
            {"sample_rate": 8000}, {"bass": True}, {"melody": "0.5"}, {"tempo": 161},
            {"movement": -0.1}, {"unexpected": 1},
        )
        for values in cases:
            with self.subTest(values=values), self.assertRaises(ValueError):
                Settings.from_dict(values)


class ServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.server = StudioServer(("127.0.0.1", 0))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def request(self, method: str, path: str, body=None, headers=None):
        connection = http.client.HTTPConnection(*self.server.server_address, timeout=20)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, response.getheader("Content-Type"), response.read()
        finally:
            connection.close()

    def test_assets_and_downloadable_wav(self) -> None:
        for path in ("/", "/app.js", "/style.css"):
            with self.subTest(path=path):
                status, _, body = self.request("GET", path)
                self.assertEqual(status, 200)
                self.assertGreater(len(body), 100)
        status, mime, body = self.request("POST", "/api/render", json.dumps({"duration": 2}),
                                        {"Content-Type": "application/json"})
        self.assertEqual((status, mime), (200, "audio/wav"))
        with wave.open(io.BytesIO(body), "rb") as wav:
            self.assertEqual(wav.getnframes(), 88200)

    def test_invalid_requests_and_concurrent_render(self) -> None:
        headers = {"Content-Type": "application/json"}
        for body in ('{"duration":301}', '[]', '{"complexity":NaN}', '{'):
            self.assertEqual(self.request("POST", "/api/render", body, headers)[0], 400)
        self.assertEqual(self.request("POST", "/api/render", "{}", {"Content-Type": "text/plain"})[0], 415)
        self.assertEqual(self.request("POST", "/api/render", " " * 4097, headers)[0], 413)
        self.assertEqual(self.request("GET", "/../soundscape.py")[0], 404)
        self.assertEqual(self.request("GET", "/", headers={"Host": "elsewhere.invalid"})[0], 403)
        self.assertEqual(self.request("POST", "/api/render", "{}", {**headers, "Origin": "https://elsewhere.invalid"})[0], 403)
        with self.server.render_slot:
            self.assertEqual(self.request("POST", "/api/render", "{}", headers)[0], 429)


class CommandLineTests(unittest.TestCase):
    def test_export_and_overwrite_protection(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "test.wav"
            command = [sys.executable, str(Path(__file__).with_name("main.py")), "render",
                       "--duration", "2", "--output", str(output)]
            first = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            original = output.read_bytes()
            again = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(again.returncode, 0)
            self.assertEqual(output.read_bytes(), original)
            forced = subprocess.run(command + ["--force"], capture_output=True, text=True)
            self.assertEqual(forced.returncode, 0, forced.stderr)
            self.assertEqual(output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
