#!/usr/bin/env python3
"""Run the local listening studio or render a soundscape from the command line."""

import argparse
import io
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
import traceback
from urllib.parse import urlsplit
import webbrowser

from soundscape import Settings, render_wav


STATIC = Path(__file__).resolve().parent / "static"
ASSETS = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
    "/style.css": ("style.css", "text/css; charset=utf-8"),
}


class StudioServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int]):
        super().__init__(address, StudioHandler)
        self.render_slot = threading.BoundedSemaphore(1)


class StudioHandler(BaseHTTPRequestHandler):
    server: StudioServer

    def _reply(self, status: int, body: bytes, content_type: str, **headers: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", (
            "default-src 'self'; script-src 'self'; style-src 'self'; "
            "media-src 'self' blob:; img-src 'self'; frame-ancestors 'none'; base-uri 'none'"
        ))
        for name, value in headers.items():
            self.send_header(name, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass  # The listener closed the tab while a render was finishing.

    def _error(self, status: int, message: str) -> None:
        self._reply(status, json.dumps({"error": message}).encode(), "application/json")

    def _local_request(self) -> bool:
        port = self.server.server_address[1]
        hosts = {f"127.0.0.1:{port}", f"localhost:{port}"}
        host = self.headers.get("Host", "")
        origin = self.headers.get("Origin")
        if host not in hosts or (origin is not None and origin != f"http://{host}"):
            self._error(403, "Open this studio directly on localhost.")
            return False
        return True

    def do_GET(self) -> None:
        if not self._local_request():
            return
        asset = ASSETS.get(urlsplit(self.path).path)
        if asset is None:
            self._error(404, "Not found")
            return
        filename, content_type = asset
        self._reply(200, (STATIC / filename).read_bytes(), content_type)

    def do_POST(self) -> None:
        if not self._local_request():
            return
        if self.path != "/api/render":
            self._error(404, "Not found")
            return
        if self.headers.get_content_type() != "application/json":
            self._error(415, "Send settings as application/json.")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 4096:
                self._error(413, "Settings must be between 1 and 4096 bytes.")
                return
            values = json.loads(self.rfile.read(length))
            settings = Settings.from_dict(values)
        except (ValueError, TypeError, UnicodeDecodeError) as error:
            self._error(400, str(error))
            return
        if not self.server.render_slot.acquire(blocking=False):
            self._error(429, "A soundscape is already rendering. Try again when it finishes.")
            return
        try:
            output = io.BytesIO()
            measurements = render_wav(output, settings)
            self._reply(200, output.getvalue(), "audio/wav", **{
                "Content-Disposition": f'attachment; filename="pleasantly-busy-{settings.seed}.wav"',
                "X-Audio-Duration": str(measurements["duration"]),
            })
        except Exception:
            self.log_error("Audio rendering failed")
            traceback.print_exc()
            self._error(500, "Audio rendering failed. Check the terminal and try again.")
        finally:
            self.server.render_slot.release()


def serve(port: int, open_browser: bool) -> None:
    with StudioServer(("127.0.0.1", port)) as server:
        url = f"http://127.0.0.1:{server.server_address[1]}"
        print(f"Pleasantly Busy is ready at {url}", flush=True)
        print("Press Ctrl+C to stop. Audio is generated locally.", flush=True)
        if open_browser:
            webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\nStudio stopped.")


def cli() -> None:
    parser = argparse.ArgumentParser(description="Generate a pleasantly busy soundscape.")
    commands = parser.add_subparsers(dest="command")
    studio = commands.add_parser("serve", help="Open the local listening studio (default)")
    studio.add_argument("--port", type=int, default=8765)
    studio.add_argument("--open", action="store_true", help="Open your default browser")
    render = commands.add_parser("render", help="Write a stereo WAV file")
    render.add_argument("--output", type=Path, default=Path("pleasantly-busy.wav"))
    for name in ("duration", "tempo", "complexity", "bass", "percussion", "melody", "texture",
                 "movement", "volume"):
        render.add_argument(f"--{name}", type=float, default=getattr(Settings(), name))
    render.add_argument("--seed", type=int, default=42)
    render.add_argument("--sample-rate", type=int, default=44100, choices=(22050, 44100, 48000))
    render.add_argument("--force", action="store_true", help="Replace an existing output file")
    args = parser.parse_args()
    if args.command != "render":
        port = getattr(args, "port", 8765)
        if not 0 <= port <= 65535:
            parser.error("port must be from 0 to 65535")
        try:
            serve(port, getattr(args, "open", False))
        except OSError as error:
            parser.exit(1, f"Could not start the studio: {error}\n")
        return
    values = vars(args).copy()
    output = values.pop("output")
    force = values.pop("force")
    values.pop("command")
    try:
        settings = Settings.from_dict(values)
        # Exclusive creation protects a previous export unless --force is used.
        with output.open("wb" if force else "xb") as stream:
            measurements = render_wav(stream, settings)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Could not render: {error}\n")
    print(f"Saved {output.resolve()}")
    print(json.dumps(measurements, indent=2))


if __name__ == "__main__":
    cli()
