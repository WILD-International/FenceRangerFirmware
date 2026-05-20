#!/usr/bin/env python3
"""Dev server for ota.html. Serves the page and stubs the device API endpoints."""

import json
import os
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs

DELAY = 0.2

HTML_PATH = os.path.join(os.path.dirname(__file__), "../src/ota.html")

# Fake device state
state = {
    "eui": "AABBCCDDEEFF0011",
    "name": "test-fence",
    "appkey": "00112233445566778899aabbccddeeff",
}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/":
            with open(HTML_PATH, "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(body)

        elif self.path == "/api/config":
            time.sleep(DELAY)
            body = json.dumps(state).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(body)

        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)

        if self.path == "/config":
            time.sleep(DELAY)
            data = parse_qs(body.decode())
            state["name"] = data.get("name", [state["name"]])[0]
            state["appkey"] = data.get("appkey", [state["appkey"]])[0]
            print(f"Config updated: {state}")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Saved")

        elif self.path == "/update":
            print(f"Firmware upload received ({length} bytes)")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"OK")

        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, fmt, *args):
        print(f"{self.address_string()} {fmt % args}")


if __name__ == "__main__":
    server = HTTPServer(("localhost", 8080), Handler)
    print("Serving at http://localhost:8080")
    server.serve_forever()
