# Dogdog OTA mirror

This service mirrors assets from the repository's public GitHub `latest` release.
GitHub Actions authenticates an update request, the service follows GitHub's
redirects, verifies the binary, and publishes it at a stable URL with no redirect:

```text
https://ota.dogdog-zeitmessung.de/<binary-name>
```

The webhook never accepts a source URL. It always downloads from
`TheMrPhantom/ZeitmessanlageV2`, so possession of the token permits publishing a
safe filename but cannot turn the service into an arbitrary URL fetcher.

Each update gets at most three complete download attempts. A connection failure,
retryable GitHub status, or interrupted response body discards that attempt's
temporary file and starts again from byte zero; partial data is never published.

## Deploy

1. Copy `.env.example` to `.env` and replace the token with a long random value.
2. Add the same value to the GitHub repository secret `OTA_WEBHOOK_TOKEN`.
3. Start the service:

   ```sh
   docker compose up -d --build
   ```

The compose service is reachable only at `127.0.0.1:8080`. Configure the existing
TLS reverse proxy for `ota.dogdog-zeitmessung.de` to forward to that address. For
example, an nginx location can use:

```nginx
location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto https;
    proxy_request_buffering off;
    proxy_read_timeout 190s;
    proxy_send_timeout 190s;
}
```

Firmware and metadata live in the `ota-firmware` Docker volume and survive
container replacement. Back up this volume if retaining the last known-good
images is operationally important.

## HTTP API

Trigger an update after the named asset has been uploaded to the GitHub `latest`
release:

```sh
curl --fail-with-body \
  -H "Authorization: Bearer $OTA_WEBHOOK_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"sha256":"<64-hex-sha256>","size":123456,"commit_sha":"<40-hex-git-sha>"}' \
  https://ota.dogdog-zeitmessung.de/update/example.bin
```

Names may contain ASCII letters, digits, dots, underscores, and hyphens, must
start with a letter or digit, and are limited to 128 characters. `healthz` is
reserved for health monitoring. Updates are limited to 4 MiB. The service retries
transient GitHub failures, validates size and SHA-256, flushes data to disk, and
atomically replaces the prior image. Failed updates retain the prior image.

Public endpoints are:

- `GET` or `HEAD /<binary-name>`: direct binary download with byte-range support.
- `GET /healthz`: liveness response.

There is deliberately no endpoint that lists stored firmware or its metadata.

Verify the reverse proxy is not redirecting a firmware URL:

```sh
for name in measure-stations-lora.bin dogdog-controller.bin remote.bin; do
    curl --fail -sS -D - -o /dev/null \
        "https://ota.dogdog-zeitmessung.de/$name"
done
```

The result must be `200 OK` (or `206 Partial Content` for a range request), with no
`Location` header.

## Rollout and partition migration

Deploy and verify this server before publishing firmware that uses the shared OTA
component. Dogdog controllers and remotes previously used single-application
partition tables, so each one needs a one-time serial migration. Flash the new
bootloader, partition table, initial OTA data, and application together, but do
not erase the whole chip: the NVS partition at `0x9000` must be preserved.

Use each project's generated `flash_args` file after a clean ESP-IDF 5.5.1 build;
it contains the correct chip-specific bootloader offset. In particular, the
application and OTA-data offsets are `0x20000` and `0x10000` for both projects.
Do not attempt to install the new two-slot partition table through application
OTA.

The measurement station already uses an OTA partition table. Updating its
bootloader by serial once is still recommended so application rollback is
enforced by the bootloader. Its NVS hardware keys must also remain intact.

Before a fleet rollout, exercise these hardware cases on each board type:

- The normal gesture and its release protection: remote 10-second hold,
  measurement-station BOOT press in the first ten seconds, and dogdog five-button
  chord in the first ten seconds.
- The remote sends STOP immediately even when the same press becomes an OTA
  gesture.
- Successful OTA, bad checksum/missing asset, lost network, and power loss while
  downloading all leave a bootable image.
- A deliberately broken candidate rolls back, while a candidate that completes
  critical initialization is marked valid.
- The unified measurement image operates with both the standard and XLR NVS
  provisioning profiles.

## Tests

Create a virtual environment, install `requirements-dev.txt`, and run:

```sh
pytest -q
```

The test suite mocks GitHub; it never downloads a real release asset.
