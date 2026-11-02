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
Version 0.2.1 fixes serial send ordering, stalled writes, connection cleanup,
and retries of failed starter messages. The UART receiver fix below also
requires rebuilding and updating the controller firmware.

## Current starter detection

On the new webmelden live entry page, the extension reads the dedicated
`wm-live-results-current` form. The last starter and pending starter tables do
not override it. If the current form is empty or missing on a live entry page,
no starter update is sent. Older pages using `tr.akt` remain supported.

Starter, dog, and start number columns are located by their table headers.
Changes to text, rows, or CSS classes trigger a scan; a one-second polling
fallback also checks the page. Unchanged starters do not trigger repeat sends.
Failed messages to the extension are retried on the next scan and produce a
warning in the webmelden page's developer console. After an extension reload,
refresh the webmelden page to restore its content script.

## Serial reliability and troubleshooting

Serial sends run in order. If a starter changes during a pending write, the
bridge sends the latest starter next and only remembers the starter actually
written. Previously, concurrent writes could fail with a locked stream, and
completion of an older write could mark the newer, unsent starter as sent.
Reconnect now sends the current starter again, even if it has not changed.
Enabling Auto-send also checks for an unsent current starter.

A write that stalls for five seconds now logs a timeout and disconnects the
session. Disconnect cancels pending writes and reads before closing the port.
Previously, a stalled write held the writer lock indefinitely without an error;
Disconnect could then fail to close the port and discard the only handle to it.
USB disconnect events also end the session, even if no stream error arrives.
After an unexpected disconnect or read/write failure, the bridge automatically
tries the same selected port up to five times, waiting two seconds before each
attempt. It does not open a port chooser or switch to another device. The status
and log show reconnect progress; use `Cancel Reconnect` to stop the attempts.
A successful reconnect sends the latest starter again if Auto-send is enabled.
The new session must stay connected for one second before the retry count resets.
Manual Disconnect does not trigger automatic reconnection. After all attempts
fail, use `Connect Serial` to try again.

If cleanup or closing stalls, the bridge logs it after three seconds per phase
and retains the port handle. Automatic reconnect retries cleanup before opening
the port; after cancelling or exhausting retries, use `Retry Disconnect` if needed.

The firmware now installs the UART receive driver with a 2 KiB interrupt-backed
buffer and switches console input to that driver. Previously, `getchar()` read
the hardware FIFO directly and slept for 20 ms when empty. The ESP32-S3 FIFO is
only 128 bytes, which fills in about 11 ms at 115200 baud (8N1). A burst or a
delayed receive task could therefore lose command bytes or the terminating
newline while Chrome still reported a successful write. The driver's receive
buffer decouples byte reception from the task's scheduling. See Espressif's
[UART driver guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/peripherals/uart.html).

The firmware also clears stdio EOF/error state on a failed read and discards
an entire oversized line until its newline, rather than interpreting the tail
as another command. Oversized commands now emit an error even with error-only
logging. Host regressions model the old FIFO limit and reproduce the dropped
input; this is a confirmed receive-path flaw, not proof that it caused every
observed interruption. The firmware changes must be installed on the controller
to take effect; reloading the extension alone does not update firmware.

If the serial chooser is empty, check Windows Device Manager and
`chrome://device-log` for USB removal or driver errors. Reconnect the controller's
USB cable if Windows no longer lists its COM port. The extension cannot restore
a device that is absent from the operating system. Chrome documents serial
events and the device log in its [Web Serial guide](https://developer.chrome.com/docs/capabilities/serial).

The controller configuration in this checkout uses UART0 for console input;
the secondary USB Serial/JTAG console only mirrors output. Use the USB-to-UART
port for starter commands with that configuration. Seeing controller logs on
the native USB Serial/JTAG port does not prove commands can be read there.
See Espressif's [USB Serial/JTAG console guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/usb-serial-jtag-console.html).

`Sent` / `Auto-sent` means the browser completed the write, not that the
controller acknowledged or displayed it. The current protocol has no
acknowledgement. The controller's `Received competitor from PC` message uses
info logging, but `sdkconfig.defaults` currently selects error-only logging;
warnings and receipt messages will therefore be absent with those settings.
The simulated tests reproduce extension and firmware input failures; the cause of an actual
USB device disappearing still requires logs from the affected PC/controller.

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
node --test tests/chrome-extension/content.test.mjs tests/chrome-extension/bridge.test.mjs tests/chrome-extension/diagnostics.test.mjs tests/chrome-extension/messaging.test.mjs
```

The tests use simulated serial streams and Chrome downloads. They cover byte
chunking, UTF-8, ANSI colors, multi-line traces, grouping, bounded capture,
disconnects, missing newlines, and download failures. Verify downloads with the
physical controller after reloading the extension.

Serial lifecycle tests also cover concurrent starter changes, reconnecting
without a starter change, stuck/rejected writes, USB events, pending-write
cancellation, failed/timed-out closes, recoverable read errors, rapid clicks,
Auto-send changes, automatic reconnect to the same port, bounded retries, and
cancelling pending retries or an in-flight reopen. Messaging tests cover failed
delivery and storage retries.

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
