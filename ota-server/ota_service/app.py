"""Authenticated GitHub release mirror for ESP-IDF firmware images."""

from __future__ import annotations

import errno
import hashlib
import json
import os
import re
import secrets
import shutil
import tempfile
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import quote

import requests
from flask import Flask, Response, jsonify, request, send_file
from requests.adapters import HTTPAdapter


GITHUB_RELEASE_BASE = (
    "https://github.com/TheMrPhantom/ZeitmessanlageV2/releases/download/latest"
)
DEFAULT_MAX_FIRMWARE_SIZE = 4 * 1024 * 1024
DEFAULT_DOWNLOAD_ATTEMPTS = 3
DEFAULT_DOWNLOAD_RETRY_BACKOFF = 0.5
DOWNLOAD_CHUNK_SIZE = 64 * 1024
MAX_RETRY_DELAY = 10.0
RETRYABLE_HTTP_STATUSES = frozenset({429, 500, 502, 503, 504})
SAFE_FILENAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$", re.ASCII)
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$", re.ASCII)
COMMIT_SHA_RE = re.compile(r"^[0-9a-fA-F]{40}$", re.ASCII)
UPDATE_FIELDS = frozenset({"sha256", "size", "commit_sha"})


class RequestValidationError(ValueError):
    """The webhook request is invalid."""


class DownloadIntegrityError(RuntimeError):
    """The downloaded asset does not match the webhook metadata."""


class DownloadTooLargeError(RuntimeError):
    """The upstream response exceeds the configured firmware limit."""


def is_safe_filename(value: object) -> bool:
    """Return whether *value* is one safe, non-reserved path component."""

    if not isinstance(value, str) or value in {".", "..", "healthz"}:
        return False
    try:
        value.encode("ascii")
    except UnicodeEncodeError:
        return False
    return SAFE_FILENAME_RE.fullmatch(value) is not None


def _create_http_session() -> requests.Session:
    # Complete attempts are retried by _download_asset(). Keeping adapter retries
    # disabled avoids multiplying attempts for failures that happen before headers.
    adapter = HTTPAdapter(max_retries=0)
    session = requests.Session()
    session.mount("https://", adapter)
    session.mount("http://", adapter)
    session.max_redirects = 10
    session.headers.update(
        {
            "Accept": "application/octet-stream",
            "Accept-Encoding": "identity",
            "User-Agent": "dogdog-ota-mirror/1.0",
        }
    )
    return session


def _error(message: str, status: int) -> tuple[Response, int]:
    return jsonify({"error": message}), status


def _is_authorized(expected_token: str) -> bool:
    header = request.headers.get("Authorization", "")
    scheme, separator, supplied_token = header.partition(" ")
    correct_scheme = separator == " " and scheme.lower() == "bearer"
    supplied = supplied_token if correct_scheme else ""
    matches = secrets.compare_digest(
        supplied.encode("utf-8"), expected_token.encode("utf-8")
    )
    return correct_scheme and bool(supplied) and matches


def _parse_update_payload(max_size: int) -> dict[str, Any]:
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict):
        raise RequestValidationError("request body must be a JSON object")

    unknown_fields = set(payload) - UPDATE_FIELDS
    missing_fields = UPDATE_FIELDS - set(payload)
    if missing_fields:
        raise RequestValidationError(
            f"missing fields: {', '.join(sorted(missing_fields))}"
        )
    if unknown_fields:
        raise RequestValidationError(
            f"unexpected fields: {', '.join(sorted(unknown_fields))}"
        )

    expected_hash = payload["sha256"]
    expected_size = payload["size"]
    commit_sha = payload["commit_sha"]

    if not isinstance(expected_hash, str) or not SHA256_RE.fullmatch(expected_hash):
        raise RequestValidationError("sha256 must contain exactly 64 hexadecimal characters")
    if (
        isinstance(expected_size, bool)
        or not isinstance(expected_size, int)
        or expected_size <= 0
        or expected_size > max_size
    ):
        raise RequestValidationError(f"size must be an integer from 1 to {max_size}")
    if not isinstance(commit_sha, str) or not COMMIT_SHA_RE.fullmatch(commit_sha):
        raise RequestValidationError(
            "commit_sha must contain exactly 40 hexadecimal characters"
        )

    return {
        "sha256": expected_hash.lower(),
        "size": expected_size,
        "commit_sha": commit_sha.lower(),
    }


def _remove_if_present(path: Path | None) -> None:
    if path is None:
        return
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def _fsync_directory(path: Path) -> None:
    flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        if exc.errno in {errno.EACCES, errno.EINVAL, errno.ENOTSUP}:
            return
        raise
    try:
        try:
            os.fsync(descriptor)
        except OSError as exc:
            if exc.errno not in {errno.EBADF, errno.EINVAL, errno.ENOTSUP}:
                raise
    finally:
        os.close(descriptor)


def _download_asset_once(
    session: requests.Session,
    source_url: str,
    data_dir: Path,
    filename: str,
    expected_size: int,
    expected_hash: str,
    max_size: int,
    connect_timeout: float,
    read_timeout: float,
) -> Path:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{filename}.", suffix=".download", dir=data_dir
    )
    temporary_path = Path(temporary_name)
    response: requests.Response | None = None

    try:
        response = session.get(
            source_url,
            stream=True,
            allow_redirects=True,
            timeout=(connect_timeout, read_timeout),
        )
        response.raise_for_status()

        content_length = response.headers.get("Content-Length")
        if content_length is not None:
            try:
                advertised_size = int(content_length)
            except ValueError as exc:
                raise DownloadIntegrityError(
                    "upstream returned an invalid Content-Length"
                ) from exc
            if advertised_size > max_size:
                raise DownloadTooLargeError("upstream firmware exceeds the size limit")
            if advertised_size != expected_size:
                raise DownloadIntegrityError(
                    "upstream Content-Length does not match the webhook size"
                )

        digest = hashlib.sha256()
        downloaded_size = 0
        with os.fdopen(descriptor, "wb") as firmware_file:
            descriptor = -1
            for chunk in response.iter_content(chunk_size=DOWNLOAD_CHUNK_SIZE):
                if not chunk:
                    continue
                downloaded_size += len(chunk)
                if downloaded_size > max_size:
                    raise DownloadTooLargeError("upstream firmware exceeds the size limit")
                if downloaded_size > expected_size:
                    raise DownloadIntegrityError(
                        "downloaded firmware is larger than the webhook size"
                    )
                firmware_file.write(chunk)
                digest.update(chunk)
            firmware_file.flush()
            os.fsync(firmware_file.fileno())

        if downloaded_size != expected_size:
            raise DownloadIntegrityError(
                "downloaded firmware size does not match the webhook size"
            )
        if not secrets.compare_digest(digest.hexdigest(), expected_hash):
            raise DownloadIntegrityError(
                "downloaded firmware SHA-256 does not match the webhook checksum"
            )
        return temporary_path
    except Exception:
        if descriptor >= 0:
            os.close(descriptor)
        _remove_if_present(temporary_path)
        raise
    finally:
        if response is not None:
            response.close()


def _is_retryable_download_error(error: requests.RequestException) -> bool:
    if not isinstance(error, requests.HTTPError):
        return True
    response = error.response
    return response is not None and response.status_code in RETRYABLE_HTTP_STATUSES


def _retry_delay(
    failed_attempt: int,
    error: requests.RequestException,
    base_backoff: float,
) -> float:
    delay = base_backoff * (2 ** (failed_attempt - 1))
    response = getattr(error, "response", None)
    if response is not None:
        retry_after = response.headers.get("Retry-After")
        if retry_after is not None:
            try:
                delay = max(delay, float(retry_after))
            except ValueError:
                pass
    return min(max(delay, 0.0), MAX_RETRY_DELAY)


def _download_asset(
    session: requests.Session,
    source_url: str,
    data_dir: Path,
    filename: str,
    expected_size: int,
    expected_hash: str,
    max_size: int,
    connect_timeout: float,
    read_timeout: float,
    attempts: int,
    retry_backoff: float,
) -> Path:
    """Download and verify an asset, retrying complete interrupted streams."""

    for attempt in range(1, attempts + 1):
        try:
            return _download_asset_once(
                session,
                source_url,
                data_dir,
                filename,
                expected_size,
                expected_hash,
                max_size,
                connect_timeout,
                read_timeout,
            )
        except requests.RequestException as error:
            if attempt == attempts or not _is_retryable_download_error(error):
                raise
            delay = _retry_delay(attempt, error, retry_backoff)
            if delay:
                time.sleep(delay)

    raise RuntimeError("download retry loop ended unexpectedly")


def _write_metadata_temp(metadata_dir: Path, filename: str, metadata: dict[str, Any]) -> Path:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{filename}.", suffix=".json.tmp", dir=metadata_dir
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as metadata_file:
            descriptor = -1
            json.dump(metadata, metadata_file, sort_keys=True, separators=(",", ":"))
            metadata_file.write("\n")
            metadata_file.flush()
            os.fsync(metadata_file.fileno())
        return temporary_path
    except Exception:
        if descriptor >= 0:
            os.close(descriptor)
        _remove_if_present(temporary_path)
        raise


def _backup_existing(path: Path) -> Path | None:
    if not path.is_file():
        return None
    descriptor, backup_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".backup", dir=path.parent
    )
    os.close(descriptor)
    backup_path = Path(backup_name)
    backup_path.unlink()
    try:
        os.link(path, backup_path)
    except OSError:
        shutil.copyfile(path, backup_path)
        with backup_path.open("rb") as backup_file:
            os.fsync(backup_file.fileno())
    return backup_path


def _restore_previous(target: Path, backup: Path | None) -> None:
    if backup is None:
        _remove_if_present(target)
    else:
        os.replace(backup, target)


def _publish_update(
    binary_temp: Path,
    metadata_temp: Path,
    binary_target: Path,
    metadata_target: Path,
    data_dir: Path,
    metadata_dir: Path,
) -> None:
    binary_backup: Path | None = None
    metadata_backup: Path | None = None
    binary_replaced = False
    metadata_replaced = False

    try:
        binary_backup = _backup_existing(binary_target)
        metadata_backup = _backup_existing(metadata_target)

        os.replace(binary_temp, binary_target)
        binary_replaced = True
        os.replace(metadata_temp, metadata_target)
        metadata_replaced = True
        _fsync_directory(data_dir)
        _fsync_directory(metadata_dir)
    except Exception:
        # Publication has two files. Restore both if the second replace or fsync fails.
        if metadata_replaced:
            _restore_previous(metadata_target, metadata_backup)
            metadata_backup = None
        if binary_replaced:
            _restore_previous(binary_target, binary_backup)
            binary_backup = None
        _fsync_directory(data_dir)
        _fsync_directory(metadata_dir)
        raise
    finally:
        for path in (binary_temp, metadata_temp, binary_backup, metadata_backup):
            _remove_if_present(path)


def _safe_stored_file(data_dir: Path, filename: str) -> Path | None:
    if not is_safe_filename(filename):
        return None
    candidate = data_dir / filename
    try:
        resolved = candidate.resolve(strict=True)
        data_root = data_dir.resolve(strict=True)
    except (FileNotFoundError, OSError):
        return None
    if resolved.parent != data_root or not resolved.is_file():
        return None
    return resolved


def create_app(config: dict[str, Any] | None = None) -> Flask:
    app = Flask(__name__)
    app.config.from_mapping(
        DATA_DIR=os.environ.get("OTA_DATA_DIR", "/data"),
        OTA_WEBHOOK_TOKEN=os.environ.get("OTA_WEBHOOK_TOKEN", ""),
        MAX_FIRMWARE_SIZE=DEFAULT_MAX_FIRMWARE_SIZE,
        DOWNLOAD_CONNECT_TIMEOUT=float(os.environ.get("OTA_CONNECT_TIMEOUT", "5")),
        DOWNLOAD_READ_TIMEOUT=float(os.environ.get("OTA_READ_TIMEOUT", "30")),
        DOWNLOAD_ATTEMPTS=DEFAULT_DOWNLOAD_ATTEMPTS,
        DOWNLOAD_RETRY_BACKOFF=DEFAULT_DOWNLOAD_RETRY_BACKOFF,
        MAX_CONTENT_LENGTH=16 * 1024,
        HTTP_SESSION=None,
    )
    if config:
        app.config.update(config)

    token = app.config["OTA_WEBHOOK_TOKEN"]
    if not isinstance(token, str) or not token:
        raise RuntimeError("OTA_WEBHOOK_TOKEN must be set to a non-empty value")
    attempts = app.config["DOWNLOAD_ATTEMPTS"]
    if isinstance(attempts, bool) or not isinstance(attempts, int) or not 1 <= attempts <= 5:
        raise RuntimeError("DOWNLOAD_ATTEMPTS must be an integer from 1 to 5")
    retry_backoff = app.config["DOWNLOAD_RETRY_BACKOFF"]
    if (
        isinstance(retry_backoff, bool)
        or not isinstance(retry_backoff, (int, float))
        or not 0 <= retry_backoff <= MAX_RETRY_DELAY
    ):
        raise RuntimeError(
            f"DOWNLOAD_RETRY_BACKOFF must be from 0 to {MAX_RETRY_DELAY:g} seconds"
        )

    data_dir = Path(app.config["DATA_DIR"]).resolve()
    metadata_dir = data_dir / ".metadata"
    data_dir.mkdir(parents=True, exist_ok=True)
    metadata_dir.mkdir(mode=0o700, exist_ok=True)
    app.extensions["ota_data_dir"] = data_dir
    app.extensions["ota_metadata_dir"] = metadata_dir
    app.extensions["ota_http_session"] = (
        app.config["HTTP_SESSION"] or _create_http_session()
    )
    # gunicorn.conf.py deliberately uses one process so this lock coordinates all
    # publications while its threads may continue serving existing firmware.
    app.extensions["ota_publish_lock"] = threading.Lock()

    @app.get("/healthz")
    def health() -> Response:
        response = jsonify({"status": "ok"})
        response.headers["Cache-Control"] = "no-store"
        return response

    @app.post("/update/<binary_name>")
    def update(binary_name: str) -> tuple[Response, int] | Response:
        if not _is_authorized(app.config["OTA_WEBHOOK_TOKEN"]):
            response, status = _error("unauthorized", 401)
            response.headers["WWW-Authenticate"] = "Bearer"
            return response, status
        if not is_safe_filename(binary_name):
            return _error("binary name is not a safe filename", 400)

        try:
            update_data = _parse_update_payload(app.config["MAX_FIRMWARE_SIZE"])
        except RequestValidationError as exc:
            return _error(str(exc), 400)

        source_url = f"{GITHUB_RELEASE_BASE}/{quote(binary_name, safe='')}"
        binary_temp: Path | None = None
        metadata_temp: Path | None = None
        try:
            binary_temp = _download_asset(
                app.extensions["ota_http_session"],
                source_url,
                data_dir,
                binary_name,
                update_data["size"],
                update_data["sha256"],
                app.config["MAX_FIRMWARE_SIZE"],
                app.config["DOWNLOAD_CONNECT_TIMEOUT"],
                app.config["DOWNLOAD_READ_TIMEOUT"],
                app.config["DOWNLOAD_ATTEMPTS"],
                app.config["DOWNLOAD_RETRY_BACKOFF"],
            )
            metadata = {
                "commit_sha": update_data["commit_sha"],
                "name": binary_name,
                "sha256": update_data["sha256"],
                "size": update_data["size"],
                "source_url": source_url,
                "updated_at": datetime.now(timezone.utc).isoformat(),
            }
            metadata_temp = _write_metadata_temp(metadata_dir, binary_name, metadata)
            with app.extensions["ota_publish_lock"]:
                _publish_update(
                    binary_temp,
                    metadata_temp,
                    data_dir / binary_name,
                    metadata_dir / f"{binary_name}.json",
                    data_dir,
                    metadata_dir,
                )
            binary_temp = None
            metadata_temp = None
        except DownloadTooLargeError as exc:
            return _error(str(exc), 413)
        except DownloadIntegrityError as exc:
            return _error(str(exc), 422)
        except requests.RequestException:
            app.logger.warning("GitHub asset download failed for %s", binary_name)
            return _error("GitHub asset download failed", 502)
        except OSError:
            app.logger.exception("Failed to store firmware %s", binary_name)
            return _error("firmware storage failed", 500)
        finally:
            _remove_if_present(binary_temp)
            _remove_if_present(metadata_temp)

        return jsonify(metadata)

    @app.route("/<binary_name>", methods=["GET", "HEAD"])
    def firmware(binary_name: str) -> Response | tuple[Response, int]:
        stored_file = _safe_stored_file(data_dir, binary_name)
        if stored_file is None:
            return _error("firmware not found", 404)

        response = send_file(
            stored_file,
            mimetype="application/octet-stream",
            as_attachment=False,
            download_name=binary_name,
            conditional=True,
            etag=True,
            max_age=0,
        )
        response.headers["Accept-Ranges"] = "bytes"
        response.headers["Cache-Control"] = (
            "no-store, no-cache, must-revalidate, max-age=0"
        )
        response.headers["Pragma"] = "no-cache"
        response.headers["Expires"] = "0"
        return response

    return app
