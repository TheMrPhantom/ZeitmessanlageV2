import hashlib
import hmac
import json
import os
from datetime import datetime, timezone
from pathlib import Path

import requests
from flask import Flask, abort, jsonify, request, send_file


DEVICE_ASSETS = {
    "measure-stations-lora": "measure-stations-lora.bin",
    "dogdog-controller": "dogdog-controller.bin",
    "timepanel-hub75": "timepanel-hub75.bin",
}

app = Flask(__name__)


def data_dir() -> Path:
    path = Path(os.environ.get("DATA_DIR", "/data")).resolve()
    path.mkdir(parents=True, exist_ok=True)
    (path / "firmware").mkdir(parents=True, exist_ok=True)
    return path


def github_headers() -> dict[str, str]:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "dogdog-ota-relay",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def verify_hmac_signature(secret: str) -> bool:
    signature = request.headers.get("X-Hub-Signature-256", "")
    if not signature.startswith("sha256="):
        return False

    expected = hmac.new(
        secret.encode("utf-8"),
        request.get_data(),
        hashlib.sha256,
    ).hexdigest()
    return hmac.compare_digest(signature, f"sha256={expected}")


def require_webhook_auth() -> None:
    token = os.environ.get("WEBHOOK_TOKEN")
    if token:
        provided = request.headers.get("X-DogDog-Token") or request.args.get("token")
        if hmac.compare_digest(provided or "", token):
            return

    github_secret = os.environ.get("GITHUB_WEBHOOK_SECRET")
    if github_secret and verify_hmac_signature(github_secret):
        return

    abort(401)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as firmware:
        for chunk in iter(lambda: firmware.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_asset(device: str, asset: dict, firmware_dir: Path) -> dict:
    target = firmware_dir / f"{device}.bin"
    tmp = firmware_dir / f"{device}.bin.tmp"

    with requests.get(
        asset["browser_download_url"],
        headers=github_headers(),
        timeout=120,
        stream=True,
    ) as response:
        response.raise_for_status()
        with tmp.open("wb") as firmware:
            for chunk in response.iter_content(chunk_size=1024 * 256):
                if chunk:
                    firmware.write(chunk)

    tmp.replace(target)

    return {
        "device": device,
        "asset": asset["name"],
        "size": target.stat().st_size,
        "sha256": sha256_file(target),
        "url": f"/firmware/{device}.bin",
        "source_url": asset["browser_download_url"],
        "updated_at": utc_now(),
    }


def sync_latest_release() -> dict:
    owner = os.environ.get("GITHUB_OWNER", "TheMrPhantom")
    repo = os.environ.get("GITHUB_REPO", "ZeitmessanlageV2")
    tag = os.environ.get("GITHUB_RELEASE_TAG", "latest")

    release_url = f"https://api.github.com/repos/{owner}/{repo}/releases/tags/{tag}"
    response = requests.get(release_url, headers=github_headers(), timeout=30)
    response.raise_for_status()
    release = response.json()

    assets_by_name = {asset["name"]: asset for asset in release.get("assets", [])}
    firmware_dir = data_dir() / "firmware"
    downloaded = []
    missing = []

    for device, asset_name in DEVICE_ASSETS.items():
        asset = assets_by_name.get(asset_name)
        if asset is None:
            missing.append(asset_name)
            continue
        downloaded.append(download_asset(device, asset, firmware_dir))

    manifest = {
        "synced_at": utc_now(),
        "repository": f"{owner}/{repo}",
        "tag": tag,
        "release_id": release.get("id"),
        "release_name": release.get("name"),
        "firmware": downloaded,
        "missing_assets": missing,
    }

    manifest_path = data_dir() / "manifest.json"
    tmp_manifest_path = data_dir() / "manifest.json.tmp"
    tmp_manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    tmp_manifest_path.replace(manifest_path)
    return manifest


@app.get("/healthz")
def healthz():
    return jsonify({"ok": True})


@app.get("/manifest.json")
def manifest():
    manifest_path = data_dir() / "manifest.json"
    if not manifest_path.exists():
        abort(404)
    return send_file(manifest_path, mimetype="application/json", conditional=True)


@app.route("/firmware/<device>.bin", methods=["GET", "HEAD"])
def firmware(device: str):
    if device not in DEVICE_ASSETS:
        abort(404)

    firmware_path = data_dir() / "firmware" / f"{device}.bin"
    if not firmware_path.exists():
        abort(404)

    response = send_file(
        firmware_path,
        mimetype="application/octet-stream",
        as_attachment=False,
        download_name=f"{device}.bin",
        conditional=True,
    )
    response.headers["Content-Length"] = str(firmware_path.stat().st_size)
    return response


@app.post("/sync")
@app.post("/webhook/github")
def webhook():
    require_webhook_auth()
    manifest = sync_latest_release()
    status = 207 if manifest["missing_assets"] else 200
    return jsonify(manifest), status


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=int(os.environ.get("PORT", "8000")))
