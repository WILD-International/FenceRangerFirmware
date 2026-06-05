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
    "euiOverride": False,
    "name": "test-fence",
    "appkey": "00112233445566778899aabbccddeeff",
    "alertV": 3000,
    "recoverV": 3500,
    "burstSends": 3,
    "idleHbCycles": 60,
    "alertHbCycles": 15,
    "recoverCycles": 30,
    "sleepInterval": 60,
    "battFullMv": 4000,
    "battLowMv": 3300,
    "battSleepMult": 30,
    "battRecoveryN": 50,
    "measureDurationMs": 3000,
    "fenceCalPercent": 100,
    "rxLnaEnabled": 1,
    "txPower": 10,
    "dataRate": 3,
    "adrEnabled": 1,
    "syncwordPublic": 1,
    "scanGuardMs": 10,
    "version": "v1.2.3-dev",
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

        if self.path == "/device":
            time.sleep(DELAY)
            data = parse_qs(body.decode())
            state["name"] = data.get("name", [state["name"]])[0]
            eui = data.get("eui", [""])[0]
            if eui == "":
                state["eui"] = "AABBCCDDEEFF0011"
                state["euiOverride"] = False
            elif len(eui) == 16:
                state["eui"] = eui.upper()
                state["euiOverride"] = True
            state["appkey"] = data.get("appkey", [state["appkey"]])[0]
            print(f"Device updated: {state}")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Saved")

        elif self.path == "/alert":
            time.sleep(DELAY)
            data = parse_qs(body.decode())
            state["alertV"] = int(data.get("alertV", [state["alertV"]])[0])
            state["recoverV"] = int(data.get("recoverV", [state["recoverV"]])[0])
            state["burstSends"] = int(data.get("burstSends", [state["burstSends"]])[0])
            state["idleHbCycles"] = int(data.get("idleHbCycles", [state["idleHbCycles"]])[0])
            state["alertHbCycles"] = int(data.get("alertHbCycles", [state["alertHbCycles"]])[0])
            state["recoverCycles"] = int(data.get("recoverCycles", [state["recoverCycles"]])[0])
            print(f"Alert config updated: {state}")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Saved")

        elif self.path == "/sleep":
            time.sleep(DELAY)
            data = parse_qs(body.decode())
            state["sleepInterval"] = int(data.get("sleepInterval", [state["sleepInterval"]])[0])
            state["battFullMv"] = int(data.get("battFullMv", [state["battFullMv"]])[0])
            state["battLowMv"] = int(data.get("battLowMv", [state["battLowMv"]])[0])
            state["battSleepMult"] = int(data.get("battSleepMult", [state["battSleepMult"]])[0])
            state["battRecoveryN"] = int(data.get("battRecoveryN", [state["battRecoveryN"]])[0])
            print(f"Sleep config updated: {state}")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Saved")

        elif self.path == "/measure":
            time.sleep(DELAY)
            data = parse_qs(body.decode())
            state["measureDurationMs"] = int(data.get("measureDurationMs", [state["measureDurationMs"]])[0])
            state["fenceCalPercent"] = int(data.get("fenceCalPercent", [state["fenceCalPercent"]])[0])
            print(f"Measure config updated: {state}")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Saved")

        elif self.path == "/radio":
            time.sleep(DELAY)
            data = parse_qs(body.decode())
            state["rxLnaEnabled"] = int(data.get("rxLnaEnabled", [state["rxLnaEnabled"]])[0])
            state["txPower"] = int(data.get("txPower", [state["txPower"]])[0])
            state["dataRate"] = int(data.get("dataRate", [state["dataRate"]])[0])
            state["adrEnabled"] = int(data.get("adrEnabled", [state["adrEnabled"]])[0])
            state["syncwordPublic"] = int(data.get("syncwordPublic", [state["syncwordPublic"]])[0])
            state["scanGuardMs"] = int(data.get("scanGuardMs", [state["scanGuardMs"]])[0])
            print(f"Radio config updated: {state}")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Saved")

        elif self.path == "/resume":
            time.sleep(DELAY)
            print("Resume monitoring requested (AP would shut down on device)")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Resuming normal operation")

        elif self.path == "/update":
            chunk_size = 4096
            received = 0
            while received < length:
                chunk = min(chunk_size, length - received)
                self.rfile.read(chunk)
                received += chunk
                time.sleep(0.05)  # simulate ~80 KB/s
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
