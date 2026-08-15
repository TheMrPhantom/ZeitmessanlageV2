# DogDog OTA Relay

Small Flask service for `ota.dogdog-zeitmessung.de`.

It receives a GitHub workflow/webhook notification, downloads the firmware
assets from the public `latest` release, caches them in `/data`, and serves
short ESP-friendly URLs:

- `/firmware/measure-stations-lora.bin`
- `/firmware/dogdog-controller.bin`
- `/firmware/timepanel-hub75.bin`
- `/manifest.json`
- `/healthz`

## Run

```bash
WEBHOOK_TOKEN=change-me docker compose up -d --build
```

Optional environment variables:

- `GITHUB_TOKEN`: avoids public GitHub API rate limits.
- `GITHUB_WEBHOOK_SECRET`: accepts GitHub `X-Hub-Signature-256` HMAC auth.

The GitHub workflow posts to `/webhook/github` with `X-DogDog-Token`.
You can also trigger a manual sync with:

```bash
curl -X POST https://ota.dogdog-zeitmessung.de/sync \
  -H "X-DogDog-Token: $WEBHOOK_TOKEN"
```
