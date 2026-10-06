# DogDog Starter Serial Bridge

Chrome extension that reads the current starter from the webmelden result entry page and sends it to the DogDog controller over Web Serial.

## Install

1. Open `chrome://extensions`.
2. Enable `Developer mode`.
3. Click `Load unpacked`.
4. Select this `chrome-starter-serial-extension` directory.
5. Click the DogDog extension icon. A bridge tab opens.
6. Click `Connect Serial` and select the DogDog controller serial port at `115200` baud.

The scraper runs on `webmelden.de`, `www.webmelden.de`, and `beta.webmelden.de`
over HTTP or HTTPS, on any page path.

For testing with the saved local HTML file, open the extension details page and enable `Allow access to file URLs`.

After updating an existing installation, click `Reload` on the extension in
`chrome://extensions`, refresh the webmelden page, reopen the bridge tab, and
reconnect the serial port.
This version adds Chrome's `downloads` permission for diagnostic text files.

## Current starter detection

On the new webmelden live entry page, the extension reads the dedicated
`wm-live-results-current` form. The last starter and pending starter tables do
not override it. If the current form is empty or missing on a live entry page,
no starter update is sent. Older pages using `tr.akt` remain supported.

Starter, dog, and start number columns are located by their table headers.
Changes to text, rows, or CSS classes trigger a scan; a one-second polling
fallback also checks the page. Unchanged starters do not trigger repeat sends.

## Automatic controller diagnostics

While the bridge tab is open and serial is connected, controller warnings and
errors automatically create UTF-8 `.txt` files on the host in
`DogDog-logs` inside Chrome's configured Downloads directory. No Save As dialog
is requested. The bridge shows whether a file was saved or the download failed.

Files have names such as
`controller-error-2026-10-06T12-30-45-123Z-1.txt`. Existing files are never
overwritten. Reports include UTC timestamps, USB vendor/product IDs, the
warning/error messages, preceding serial context, and subsequent controller
output in receive order, including register dumps and stack traces.

The extension recognizes ESP-IDF `W (...) tag:` and `E (...) tag:` messages
(including ANSI-colored logs), plain `WARN`, `WARNING`, and `ERROR` prefixes,
and crash markers such as Guru Meditation, failed assertions, aborts,
`ESP_ERROR_CHECK failed`, brownouts, and `Backtrace:`. Normal info messages and
finish-time output do not trigger downloads.

Related messages are grouped into one report until serial output has been quiet
for two seconds. Disconnecting or losing the port flushes pending diagnostics,
including a final line without a newline. To bound memory use and ensure a busy
controller still produces a file, a capture also ends after 15 seconds or about
256 Ki characters; the report states why capture ended. Preceding context is
limited to 50 lines / 16 Ki characters. These limits can shorten unusually long
crash output.

The controller must emit the diagnostic over the connected port. Ordinary
warnings and errors often have no stack trace; the extension saves the message
and context in that case. It cannot reconstruct a missing trace or recover
output sent before connection. Firmware log-level settings must allow warnings
if warning capture is wanted. Raw backtrace addresses are preserved; decoding
them to source lines requires the matching firmware build.

## Tests

From the repository root, with Node.js 18 or newer:

```powershell
node --test tests/chrome-extension/content.test.mjs tests/chrome-extension/diagnostics.test.mjs
```

The tests use simulated serial streams and Chrome downloads. They cover byte
chunking, UTF-8, ANSI colors, multi-line traces, grouping, bounded capture,
disconnects, missing newlines, and download failures. Verify downloads with the
physical controller after reloading the extension.

Scraper tests run in headless Chrome or Edge against an anonymized fixture of
the supplied live entry markup. They cover the current form, duplicate rows,
empty sections, column order, Unicode names, legacy pages, and class changes.
Set `CHROME_PATH` if the browser is installed outside the usual locations;
scraper tests are skipped when no browser is found. Environments that cannot
run Chrome's process sandbox can set `CHROME_TEST_NO_SANDBOX=1` for this local
fixture test.

## Protocol

The extension sends one CRLF-terminated line whenever the current starter changes:

```text
competitor|First name|Last name|Dog name
```

Example using synthetic names:

```text
competitor|Teste|TEST|DemoDogE
```

The controller parses that line and forwards it to the timepanel as a competitor update.
