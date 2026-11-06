#!/usr/bin/env python3
"""Short-lived relay for the ABAMC experimental jar transfer.

A sender creates an offer, then uploads jar bytes. A receiver reads the file
list and downloads only after they accept in the launcher. Nothing is executed.
Offers expire after 15 minutes.
"""

import hashlib
import json
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_FILES = 20
MAX_BYTES = 150 * 1024 * 1024
MAX_OFFERS = 40
TTL_SECONDS = 15 * 60
CODE_ALPHABET = "23456789abcdefghijkmnpqrstuvwxyz"


def new_code():
    return "".join(secrets.choice(CODE_ALPHABET) for _ in range(8))


class Offer:
    def __init__(self, files):
        self.created = time.time()
        self.token = secrets.token_urlsafe(18)
        self.files = files
        self.blobs = {}

    def expired(self):
        return time.time() - self.created > TTL_SECONDS


class Store:
    def __init__(self):
        self.lock = threading.Lock()
        self.offers = {}

    def prune(self):
        dead = [code for code, offer in self.offers.items() if offer.expired()]
        for code in dead:
            del self.offers[code]

    def create(self, files):
        with self.lock:
            self.prune()
            if len(self.offers) >= MAX_OFFERS:
                return None
            code = new_code()
            while code in self.offers:
                code = new_code()
            offer = Offer(files)
            self.offers[code] = offer
            return code, offer.token

    def offer(self, code):
        with self.lock:
            self.prune()
            return self.offers.get(code)

    def put(self, code, index, token, blob):
        with self.lock:
            self.prune()
            offer = self.offers.get(code)
            if offer is None or not secrets.compare_digest(offer.token, token):
                return False
            if index < 0 or index >= len(offer.files):
                return False
            expected = offer.files[index]
            if len(blob) != expected["size"]:
                return False
            if hashlib.sha256(blob).hexdigest() != expected["sha256"]:
                return False
            offer.blobs[index] = blob
            return True


STORE = Store()


def valid_files(payload):
    files = payload.get("files")
    if not isinstance(files, list) or not files or len(files) > MAX_FILES:
        return None
    total = 0
    cleaned = []
    for item in files:
        if not isinstance(item, dict):
            return None
        name = item.get("name")
        size = item.get("size")
        digest = item.get("sha256")
        if not isinstance(name, str) or "/" in name or "\\" in name or name in {".", ".."}:
            return None
        if not name.lower().endswith(".jar"):
            return None
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            return None
        if not isinstance(digest, str) or len(digest) != 64:
            return None
        total += size
        if total > MAX_BYTES:
            return None
        cleaned.append({"name": name, "size": size, "sha256": digest.lower()})
    return cleaned


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        return

    def send_json(self, status, payload):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def read_body(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length < 0 or length > MAX_BYTES:
            return None
        return self.rfile.read(length)

    def do_GET(self):
        parts = [part for part in self.path.split("?")[0].split("/") if part]
        if len(parts) == 3 and parts[0] == "v1" and parts[1] == "offers":
            offer = STORE.offer(parts[2])
            if offer is None:
                self.send_json(404, {"error": "없는 코드입니다."})
                return
            self.send_json(200, {"files": offer.files})
            return
        if len(parts) == 4 and parts[0] == "v1" and parts[1] == "offers":
            offer = STORE.offer(parts[2])
            try:
                index = int(parts[3])
            except ValueError:
                index = -1
            if offer is None or index not in offer.blobs:
                self.send_error(404)
                return
            blob = offer.blobs[index]
            self.send_response(200)
            self.send_header("Content-Type", "application/java-archive")
            self.send_header("Content-Length", str(len(blob)))
            self.end_headers()
            self.wfile.write(blob)
            return
        self.send_error(404)

    def do_POST(self):
        parts = [part for part in self.path.split("?")[0].split("/") if part]
        body = self.read_body()
        if body is None:
            self.send_error(413)
            return
        if parts == ["v1", "offers"]:
            try:
                payload = json.loads(body.decode())
            except (UnicodeDecodeError, json.JSONDecodeError):
                self.send_json(400, {"error": "요청을 읽지 못했습니다."})
                return
            files = valid_files(payload)
            if files is None:
                self.send_json(400, {"error": "jar 파일만, 합계 150MB까지 보낼 수 있습니다."})
                return
            created = STORE.create(files)
            if created is None:
                self.send_json(429, {"error": "중계가 가득 찼습니다. 잠시 뒤에 다시 하세요."})
                return
            code, token = created
            self.send_json(200, {"code": code, "token": token})
            return
        if len(parts) == 4 and parts[0] == "v1" and parts[1] == "offers":
            token = self.headers.get("X-Token", "")
            try:
                index = int(parts[3])
            except ValueError:
                self.send_error(404)
                return
            if not STORE.put(parts[2], index, token, body):
                self.send_json(403, {"error": "파일을 올리지 못했습니다."})
                return
            self.send_json(200, {"ok": True})
            return
        self.send_error(404)


def main():
    server = ThreadingHTTPServer(("0.0.0.0", 8791), Handler)
    server.serve_forever()


if __name__ == "__main__":
    main()
