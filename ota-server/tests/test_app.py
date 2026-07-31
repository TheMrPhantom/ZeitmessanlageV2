from __future__ import annotations

import hashlib
import json
import threading
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

import pytest
import requests

import ota_service.app as app_module
from ota_service import create_app


TOKEN = "test-token-with-enough-entropy"
COMMIT_SHA = "1" * 40


@contextmanager
def local_http_server(handler_type):
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler_type)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        host, port = server.server_address
        yield f"http://{host}:{port}"
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


class FakeResponse:
    def __init__(self, body: bytes = b"", status: int = 200, headers=None):
        self.body = body
        self.status_code = status
        self.headers = headers or {"Content-Length": str(len(body))}
        self.closed = False

    def raise_for_status(self):
        if self.status_code >= 400:
            response = requests.Response()
            response.status_code = self.status_code
            raise requests.HTTPError(f"HTTP {self.status_code}", response=response)

    def iter_content(self, chunk_size: int):
        for offset in range(0, len(self.body), max(1, chunk_size)):
            yield self.body[offset : offset + chunk_size]

    def close(self):
        self.closed = True


class InterruptedResponse(FakeResponse):
    def iter_content(self, chunk_size: int):
        split_at = max(1, len(self.body) // 2)
        yield self.body[:split_at]
        raise requests.exceptions.ChunkedEncodingError(
            "simulated interrupted response body"
        )


class FakeSession:
    def __init__(self, responses=None, exception: Exception | None = None):
        self.responses = list(responses or [])
        self.exception = exception
        self.calls = []

    def get(self, url, **kwargs):
        self.calls.append((url, kwargs))
        if self.exception:
            raise self.exception
        if not self.responses:
            raise AssertionError("unexpected download")
        return self.responses.pop(0)


def update_payload(body: bytes, **overrides):
    payload = {
        "sha256": hashlib.sha256(body).hexdigest(),
        "size": len(body),
        "commit_sha": COMMIT_SHA,
    }
    payload.update(overrides)
    return payload


def make_app(tmp_path: Path, session: FakeSession):
    return create_app(
        {
            "TESTING": True,
            "DATA_DIR": tmp_path,
            "OTA_WEBHOOK_TOKEN": TOKEN,
            "HTTP_SESSION": session,
            "DOWNLOAD_RETRY_BACKOFF": 0,
        }
    )


def authorized_headers():
    return {"Authorization": f"Bearer {TOKEN}"}


def publish(client, name: str, body: bytes, **payload_overrides):
    return client.post(
        f"/update/{name}",
        json=update_payload(body, **payload_overrides),
        headers=authorized_headers(),
    )


def test_health_is_public_and_root_does_not_list_files(tmp_path):
    app = make_app(tmp_path, FakeSession())
    client = app.test_client()

    health = client.get("/healthz")
    assert health.status_code == 200
    assert health.get_json() == {"status": "ok"}
    assert health.headers["Cache-Control"] == "no-store"
    assert client.get("/").status_code == 404


@pytest.mark.parametrize(
    "authorization",
    [None, "", "Basic dGVzdDp0ZXN0", "Bearer wrong", f"bearer {TOKEN}x"],
)
def test_update_requires_exact_bearer_token(tmp_path, authorization):
    session = FakeSession()
    client = make_app(tmp_path, session).test_client()
    headers = {} if authorization is None else {"Authorization": authorization}

    response = client.post("/update/test.bin", json=update_payload(b"firmware"), headers=headers)

    assert response.status_code == 401
    assert response.headers["WWW-Authenticate"] == "Bearer"
    assert session.calls == []


def test_authenticated_update_accepts_arbitrary_safe_name(tmp_path):
    firmware = b"ESP32 firmware\x00\x01"
    response = FakeResponse(firmware)
    session = FakeSession([response])
    client = make_app(tmp_path, session).test_client()

    result = publish(client, "custom_board-v2.1.bin", firmware)

    assert result.status_code == 200
    assert result.get_json()["sha256"] == hashlib.sha256(firmware).hexdigest()
    assert (tmp_path / "custom_board-v2.1.bin").read_bytes() == firmware
    metadata = json.loads(
        (tmp_path / ".metadata" / "custom_board-v2.1.bin.json").read_text("utf-8")
    )
    assert metadata["name"] == "custom_board-v2.1.bin"
    assert metadata["size"] == len(firmware)
    assert metadata["commit_sha"] == COMMIT_SHA
    assert metadata["updated_at"].endswith("+00:00")
    assert session.calls[0][0] == (
        "https://github.com/TheMrPhantom/ZeitmessanlageV2/"
        "releases/download/latest/custom_board-v2.1.bin"
    )
    assert session.calls[0][1]["allow_redirects"] is True
    assert session.calls[0][1]["stream"] is True
    assert response.closed


def test_real_requests_session_follows_local_redirect_chain(tmp_path, monkeypatch):
    firmware = b"firmware delivered after a real redirect chain"
    requested_paths = []
    redirects = {
        "/release/redirected.bin": (302, "/redirect/one"),
        "/redirect/one": (307, "/redirect/two"),
        "/redirect/two": (308, "/assets/redirected.bin"),
    }

    class RedirectChainHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            path = urlsplit(self.path).path
            requested_paths.append(path)
            if path in redirects:
                status, location = redirects[path]
                self.send_response(status)
                self.send_header("Location", location)
                self.send_header("Content-Length", "0")
                self.send_header("Connection", "close")
                self.end_headers()
                return
            if path == "/assets/redirected.bin":
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(firmware)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(firmware)
                return
            self.send_error(404)

        def log_message(self, format, *args):
            pass

    with local_http_server(RedirectChainHandler) as origin:
        monkeypatch.setattr(app_module, "GITHUB_RELEASE_BASE", f"{origin}/release")
        session = app_module._create_http_session()
        session.trust_env = False
        try:
            client = make_app(tmp_path, session).test_client()
            response = publish(client, "redirected.bin", firmware)
        finally:
            session.close()

    assert response.status_code == 200
    assert (tmp_path / "redirected.bin").read_bytes() == firmware
    assert requested_paths == [
        "/release/redirected.bin",
        "/redirect/one",
        "/redirect/two",
        "/assets/redirected.bin",
    ]


def test_real_redirect_limit_is_bounded_and_keeps_previous_firmware(
    tmp_path, monkeypatch
):
    previous = b"previous known-good firmware"
    candidate = b"candidate hidden behind an endless redirect loop"
    state = {"redirect_loop": False}
    requested_paths = []

    class RedirectLoopHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            path = urlsplit(self.path).path
            requested_paths.append(path)
            if not state["redirect_loop"] and path == "/release/loop.bin":
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(previous)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(previous)
                return

            if path == "/release/loop.bin":
                next_path = "/loop/0"
            elif path.startswith("/loop/"):
                next_path = f"/loop/{int(path.rsplit('/', 1)[1]) + 1}"
            else:
                self.send_error(404)
                return

            self.send_response(302)
            self.send_header("Location", next_path)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()

        def log_message(self, format, *args):
            pass

    with local_http_server(RedirectLoopHandler) as origin:
        monkeypatch.setattr(app_module, "GITHUB_RELEASE_BASE", f"{origin}/release")
        session = app_module._create_http_session()
        session.trust_env = False
        try:
            app = make_app(tmp_path, session)
            client = app.test_client()
            assert publish(client, "loop.bin", previous).status_code == 200
            previous_metadata = (tmp_path / ".metadata" / "loop.bin.json").read_bytes()

            state["redirect_loop"] = True
            requested_paths.clear()
            response = publish(client, "loop.bin", candidate)
        finally:
            session.close()

    assert response.status_code == 502
    assert len(requested_paths) == (
        app.config["DOWNLOAD_ATTEMPTS"] * (session.max_redirects + 1)
    )
    assert requested_paths.count("/release/loop.bin") == app.config["DOWNLOAD_ATTEMPTS"]
    assert (tmp_path / "loop.bin").read_bytes() == previous
    assert (tmp_path / ".metadata" / "loop.bin.json").read_bytes() == previous_metadata
    assert not list(tmp_path.glob(".*.download"))


@pytest.mark.parametrize(
    "name",
    ["-leading.bin", ".hidden.bin", "healthz", "with space.bin", "ümlaut.bin"],
)
def test_unsafe_or_reserved_names_are_rejected(tmp_path, name):
    session = FakeSession()
    client = make_app(tmp_path, session).test_client()

    response = publish(client, name, b"firmware")

    assert response.status_code in {400, 404}
    assert session.calls == []


def test_encoded_backslash_and_path_separator_are_rejected(tmp_path):
    session = FakeSession()
    client = make_app(tmp_path, session).test_client()
    payload = update_payload(b"firmware")

    backslash = client.post(
        "/update/dir%5Cfile.bin", json=payload, headers=authorized_headers()
    )
    slash = client.post(
        "/update/dir%2Ffile.bin", json=payload, headers=authorized_headers()
    )

    assert backslash.status_code == 400
    assert slash.status_code == 404
    assert session.calls == []


@pytest.mark.parametrize(
    "payload",
    [
        {},
        {"sha256": "0" * 64, "size": True, "commit_sha": COMMIT_SHA},
        {"sha256": "bad", "size": 8, "commit_sha": COMMIT_SHA},
        {"sha256": "0" * 64, "size": 8, "commit_sha": "bad"},
        {
            "sha256": "0" * 64,
            "size": 8,
            "commit_sha": COMMIT_SHA,
            "source_url": "https://attacker.invalid/firmware.bin",
        },
    ],
)
def test_invalid_payload_is_rejected_before_download(tmp_path, payload):
    session = FakeSession()
    client = make_app(tmp_path, session).test_client()

    response = client.post(
        "/update/test.bin", json=payload, headers=authorized_headers()
    )

    assert response.status_code == 400
    assert session.calls == []


@pytest.mark.parametrize(
    ("response", "expected_status"),
    [
        (FakeResponse(b"not found", status=404), 502),
        (FakeResponse(b"short", headers={"Content-Length": "999"}), 422),
        (FakeResponse(b"data", headers={"Content-Length": "not-a-number"}), 422),
    ],
)
def test_upstream_errors_are_bounded_and_reported(tmp_path, response, expected_status):
    session = FakeSession([response])
    client = make_app(tmp_path, session).test_client()

    result = publish(client, "test.bin", b"expected")

    assert result.status_code == expected_status
    assert not (tmp_path / "test.bin").exists()


def test_network_failure_returns_bad_gateway(tmp_path):
    session = FakeSession(exception=requests.Timeout("timed out"))
    client = make_app(tmp_path, session).test_client()

    result = publish(client, "test.bin", b"expected")

    assert result.status_code == 502
    assert not (tmp_path / "test.bin").exists()
    assert len(session.calls) == 3


def test_interrupted_stream_is_discarded_and_retried_from_start(tmp_path):
    firmware = b"complete candidate firmware image"
    interrupted = InterruptedResponse(firmware)
    successful = FakeResponse(firmware)
    session = FakeSession([interrupted, successful])
    client = make_app(tmp_path, session).test_client()

    response = publish(client, "stream-retry.bin", firmware)

    assert response.status_code == 200
    assert (tmp_path / "stream-retry.bin").read_bytes() == firmware
    assert len(session.calls) == 2
    assert interrupted.closed
    assert successful.closed
    assert not list(tmp_path.glob(".*.download"))


def test_all_interrupted_stream_attempts_keep_previous_firmware(tmp_path):
    previous = b"last known good image"
    candidate = b"candidate that always loses its connection"
    interrupted = [InterruptedResponse(candidate) for _ in range(3)]
    session = FakeSession([FakeResponse(previous), *interrupted])
    client = make_app(tmp_path, session).test_client()
    assert publish(client, "stream-fail.bin", previous).status_code == 200
    previous_metadata = (tmp_path / ".metadata" / "stream-fail.bin.json").read_bytes()

    response = publish(client, "stream-fail.bin", candidate)

    assert response.status_code == 502
    assert len(session.calls) == 4
    assert all(item.closed for item in interrupted)
    assert (tmp_path / "stream-fail.bin").read_bytes() == previous
    assert (tmp_path / ".metadata" / "stream-fail.bin.json").read_bytes() == previous_metadata
    assert not list(tmp_path.glob(".*.download"))


def test_checksum_and_size_failures_keep_previous_firmware(tmp_path):
    old = b"old firmware"
    new = b"new firmware"
    session = FakeSession([FakeResponse(old), FakeResponse(new), FakeResponse(new)])
    client = make_app(tmp_path, session).test_client()
    assert publish(client, "test.bin", old).status_code == 200
    old_metadata = (tmp_path / ".metadata" / "test.bin.json").read_bytes()

    bad_hash = publish(client, "test.bin", new, sha256="0" * 64)
    bad_size = publish(client, "test.bin", new, size=len(new) + 1)

    assert bad_hash.status_code == 422
    assert bad_size.status_code == 422
    assert (tmp_path / "test.bin").read_bytes() == old
    assert (tmp_path / ".metadata" / "test.bin.json").read_bytes() == old_metadata


def test_upstream_failure_keeps_previous_firmware(tmp_path):
    firmware = b"known good firmware"
    failures = [FakeResponse(status=503) for _ in range(3)]
    session = FakeSession([FakeResponse(firmware), *failures])
    client = make_app(tmp_path, session).test_client()
    assert publish(client, "stable.bin", firmware).status_code == 200
    previous_metadata = (tmp_path / ".metadata" / "stable.bin.json").read_bytes()

    failed = publish(client, "stable.bin", b"unavailable candidate")

    assert failed.status_code == 502
    assert all(item.closed for item in failures)
    assert (tmp_path / "stable.bin").read_bytes() == firmware
    assert (tmp_path / ".metadata" / "stable.bin.json").read_bytes() == previous_metadata


def test_repeating_the_same_authenticated_update_is_idempotent(tmp_path):
    firmware = b"same release asset"
    session = FakeSession([FakeResponse(firmware), FakeResponse(firmware)])
    client = make_app(tmp_path, session).test_client()

    first = publish(client, "repeatable.bin", firmware)
    second = publish(client, "repeatable.bin", firmware)

    assert first.status_code == 200
    assert second.status_code == 200
    assert (tmp_path / "repeatable.bin").read_bytes() == firmware
    assert len(session.calls) == 2


def test_four_mebibyte_limit_is_enforced_before_download(tmp_path):
    session = FakeSession()
    app = make_app(tmp_path, session)
    client = app.test_client()
    too_large = app.config["MAX_FIRMWARE_SIZE"] + 1

    response = client.post(
        "/update/test.bin",
        json={"sha256": "0" * 64, "size": too_large, "commit_sha": COMMIT_SHA},
        headers=authorized_headers(),
    )

    assert response.status_code == 400
    assert session.calls == []


def test_stream_larger_than_limit_is_aborted(tmp_path):
    limit = 1024
    advertised_body = b"x" * (limit + 1)
    session = FakeSession([FakeResponse(advertised_body, headers={"X-Test": "1"})])
    app = make_app(tmp_path, session)
    app.config["MAX_FIRMWARE_SIZE"] = limit
    client = app.test_client()

    response = client.post(
        "/update/oversized.bin",
        json={
            "sha256": hashlib.sha256(advertised_body[:limit]).hexdigest(),
            "size": limit,
            "commit_sha": COMMIT_SHA,
        },
        headers=authorized_headers(),
    )

    assert response.status_code == 413
    assert not (tmp_path / "oversized.bin").exists()


def test_binary_get_head_and_range_are_direct_and_not_cached(tmp_path):
    firmware = b"0123456789abcdef"
    client = make_app(tmp_path, FakeSession([FakeResponse(firmware)])).test_client()
    assert publish(client, "range.bin", firmware).status_code == 200

    whole = client.get("/range.bin")
    head = client.head("/range.bin")
    partial = client.get("/range.bin", headers={"Range": "bytes=2-5"})

    assert whole.status_code == 200
    assert whole.data == firmware
    assert whole.mimetype == "application/octet-stream"
    assert "Location" not in whole.headers
    assert whole.headers["Accept-Ranges"] == "bytes"
    assert "no-store" in whole.headers["Cache-Control"]
    assert head.status_code == 200
    assert head.data == b""
    assert int(head.headers["Content-Length"]) == len(firmware)
    assert partial.status_code == 206
    assert partial.data == b"2345"
    assert partial.headers["Content-Range"] == f"bytes 2-5/{len(firmware)}"


def test_firmware_persists_across_application_restart(tmp_path):
    firmware = b"persistent firmware"
    first = make_app(tmp_path, FakeSession([FakeResponse(firmware)])).test_client()
    assert publish(first, "persistent.bin", firmware).status_code == 200

    second = make_app(tmp_path, FakeSession()).test_client()
    response = second.get("/persistent.bin")

    assert response.status_code == 200
    assert response.data == firmware
    assert second.get("/.metadata/persistent.bin.json").status_code == 404


def test_metadata_publish_failure_rolls_back_binary(tmp_path, monkeypatch):
    old = b"known good"
    new = b"candidate"
    session = FakeSession([FakeResponse(old), FakeResponse(new)])
    client = make_app(tmp_path, session).test_client()
    assert publish(client, "atomic.bin", old).status_code == 200
    old_metadata = (tmp_path / ".metadata" / "atomic.bin.json").read_bytes()
    real_replace = app_module.os.replace

    def fail_metadata_replace(source, destination):
        if Path(destination).name == "atomic.bin.json":
            raise OSError("simulated metadata storage failure")
        return real_replace(source, destination)

    monkeypatch.setattr(app_module.os, "replace", fail_metadata_replace)
    response = publish(client, "atomic.bin", new)

    assert response.status_code == 500
    assert (tmp_path / "atomic.bin").read_bytes() == old
    assert (tmp_path / ".metadata" / "atomic.bin.json").read_bytes() == old_metadata
    assert not list(tmp_path.glob(".*.download"))


def test_missing_binary_returns_404_without_redirect(tmp_path):
    client = make_app(tmp_path, FakeSession()).test_client()

    response = client.get("/missing.bin")

    assert response.status_code == 404
    assert "Location" not in response.headers
