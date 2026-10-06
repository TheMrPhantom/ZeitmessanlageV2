import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { setImmediate } from "node:timers/promises";
import test from "node:test";
import vm from "node:vm";

const extension = new URL("../../chrome-starter-serial-extension/", import.meta.url);
const diagnosticsSource = readFileSync(new URL("controller-diagnostics.js", extension), "utf8");
const bridgeSource = readFileSync(new URL("bridge.js", extension), "utf8");
const bytes = (text) => new TextEncoder().encode(text);

function clock() {
  let elapsed = 0;
  let sequence = 0;
  const timers = new Map();
  return {
    setTimer(callback, delay) {
      timers.set(++sequence, { callback, at: elapsed + delay });
      return sequence;
    },
    clearTimer(id) { timers.delete(id); },
    now: () => new Date(Date.UTC(2026, 9, 6) + elapsed),
    advance(ms) {
      const target = elapsed + ms;
      while (true) {
        const next = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        if (!next || next[1].at > target) break;
        elapsed = next[1].at;
        timers.delete(next[0]);
        next[1].callback();
      }
      elapsed = target;
    },
    timers
  };
}

function collector() {
  const time = clock();
  const reports = [];
  const lines = [];
  const context = vm.createContext({ TextDecoder, setTimeout, clearTimeout });
  const Diagnostics = vm.runInContext(`${diagnosticsSource}\nControllerDiagnostics;`, context);
  const diagnostics = new Diagnostics({ ...time, onLine: (line) => lines.push(line),
    onReport: (report) => reports.push(report) });
  return { diagnostics, reports, lines, time };
}

test("split UTF-8, ANSI colors, CRLF and multi-line backtrace are preserved in receive order", () => {
  const { diagnostics, reports, lines, time } = collector();
  const input = bytes("I (1) main: Ready\r\n\x1b[0;33mW (2) Sensor: Gerät offline\x1b[0m\r\n" +
    "Guru Meditation Error: Core 0 panic'ed\r\nCore 0 register dump:\r\n" +
    "PC : 0x40081234\r\nBacktrace: 0x40081234:0x3ffc1234\r\n  0x40085678:0x3ffc1250\r\nRebooting...\r\n");
  for (const byte of input) diagnostics.push(Uint8Array.of(byte));
  time.advance(1999);
  assert.equal(reports.length, 0);
  time.advance(1);
  assert.equal(reports.length, 1);
  assert.equal(reports[0].severity, "error");
  assert.equal(reports[0].warningCount, 1);
  assert.equal(reports[0].errorCount, 2);
  assert.deepEqual([...reports[0].lines], lines);
  assert.equal(lines[1], "W (2) Sensor: Gerät offline");
  assert.equal(lines.at(-2), "  0x40085678:0x3ffc1250");
  assert.equal(time.timers.size, 0);
});

test("ordinary info, finish times and text mentioning errors do not create reports", () => {
  const { diagnostics, reports, time } = collector();
  diagnostics.push(bytes("I (1) main: No error; warning checks passed\n12.345\ncompetitor|Error|Warning|Dog\n"));
  time.advance(20000);
  diagnostics.close();
  assert.equal(reports.length, 0);
});

test("warning bursts share one report and later events create separate reports", () => {
  const { diagnostics, reports, time } = collector();
  diagnostics.push(bytes("W (1) Sensor: Offline\n"));
  time.advance(1500);
  diagnostics.push(bytes("W (2) Sensor: Still offline\n"));
  time.advance(1500);
  assert.equal(reports.length, 0);
  time.advance(500);
  assert.equal(reports[0].warningCount, 2);
  assert.equal(reports[0].severity, "warning");
  diagnostics.push(bytes("E (3) main: Failed\n"));
  time.advance(2000);
  assert.equal(reports.length, 2);
  assert.equal(reports[1].warningCount, 0);
  assert.equal(reports[1].errorCount, 1);
});

test("panic, assertions, abort, backtrace-only and plain severity prefixes trigger capture", () => {
  for (const message of ["Guru Meditation Error: Core 0", "assert failed: function source.c:12",
    "abort() was called at PC 0x40081234", "ESP_ERROR_CHECK failed: esp_err_t 0x101",
    "Backtrace: 0x40081234:0x3ffc1234", "Brownout detector was triggered", "ERROR: failure",
    "[ERROR] failure", "WARNING: offline", "[WARN] offline"]) {
    const { diagnostics, reports, time } = collector();
    diagnostics.push(bytes(`${message}\n`));
    time.advance(2000);
    assert.equal(reports.length, 1, message);
  }
});

test("missing newline is flushed on idle or disconnect, with no duplicate report", () => {
  for (const finish of ["idle", "disconnect"]) {
    const { diagnostics, reports, time } = collector();
    diagnostics.push(bytes("W (1) Sensor: offline"));
    if (finish === "idle") time.advance(2000);
    else diagnostics.close();
    diagnostics.close();
    time.advance(20000);
    assert.equal(reports.length, 1);
    assert.equal(reports[0].lines.at(-1), "W (1) Sensor: offline");
    assert.equal(time.timers.size, 0);
  }
});

test("disconnect and capture deadline retain an unterminated trace tail", () => {
  for (const finish of ["disconnect", "deadline"]) {
    const { diagnostics, reports, time } = collector();
    diagnostics.push(bytes("E (1) main: Failed\n"));
    for (let i = 0; i < 14; i++) {
      time.advance(1000);
      diagnostics.push(bytes(`frame ${i}\n`));
    }
    diagnostics.push(bytes("0x40085678:0x3ffc1250"));
    if (finish === "deadline") time.advance(1000);
    else diagnostics.close();
    assert.equal(reports.length, 1);
    assert.equal(reports[0].lines.at(-1), "0x40085678:0x3ffc1250");
    assert.match(reports[0].reason, finish === "deadline" ? /time limit/ : /closed/);
    assert.equal(time.timers.size, 0);
  }
});

test("context, unfinished serial lines and report size stay bounded under noisy output", () => {
  const { diagnostics, reports } = collector();
  diagnostics.push(bytes(Array.from({ length: 100 }, (_, i) => `I (${i}) main: Ready\n`).join("")));
  assert.equal(diagnostics.context.length, 50);
  diagnostics.push(bytes("E (100) main: Failed\n" + "x" + "\u{1F415}".repeat(200000)));
  assert.ok(reports.length >= 1);
  assert.match(reports[0].reason, /size limit/);
  assert.ok(reports[0].chars < 262144 + 16385);
  assert.doesNotThrow(() => encodeURIComponent(reports[0].lines.join("\n")));
  assert.ok(diagnostics.buffer.length < 16384);
  assert.ok(diagnostics.contextChars <= 16384);
  diagnostics.close();
});

function bridge({ downloadError, downloadState = "complete" } = {}) {
  const time = clock();
  const elements = new Map();
  const downloads = [];
  let downloadListener;
  let streamController;
  const stream = new ReadableStream({ start(controller) { streamController = controller; } });
  const serialPort = {
    readable: stream,
    open: async () => {},
    getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }),
    close: async () => {
      assert.equal(stream.locked, false, "reader must release the lock before closing the port");
      serialPort.readable = null;
    }
  };
  const context = vm.createContext({
    TextDecoder, TextEncoder, AbortController,
    setTimeout: time.setTimer, clearTimeout: time.clearTimer,
    navigator: { serial: { requestPort: async () => serialPort } },
    document: { getElementById(id) {
      if (!elements.has(id)) elements.set(id, {
        textContent: "", checked: true, classList: { toggle() {} }, addEventListener() {}
      });
      return elements.get(id);
    } },
    chrome: {
      runtime: { onMessage: { addListener() {} }, sendMessage(message, callback) { callback({}); } },
      downloads: {
        async download(options) {
          if (downloadError) throw new Error(downloadError);
          downloads.push(options);
          return downloads.length;
        },
        search: async ({ id }) => [{ id, state: downloadState }],
        onChanged: { addListener(listener) { downloadListener = listener; } }
      }
    }
  });
  vm.runInContext(diagnosticsSource, context);
  vm.runInContext(bridgeSource, context);
  const api = vm.runInContext("({ connectSerial, disconnectSerial, applyStarter, buildSerialLine })", context);
  return { api, downloads, elements, serialPort, streamController, time,
    notify: (delta) => downloadListener(delta) };
}

function reportText(download) {
  return decodeURIComponent(download.url.split(",").slice(1).join(","));
}

test("serial warning downloads a UTF-8 txt file with context and stack trace, without a save dialog", async () => {
  const { api, downloads, elements, streamController, time } = bridge();
  await api.connectSerial();
  streamController.enqueue(bytes("I (1) main: Ready\n\x1b[33mW (2) Sensor: Gerät offline\x1b[0m\n"));
  streamController.enqueue(bytes("Backtrace: 0x40081234:0x3ffc1234\n  frame_two\n"));
  await setImmediate();
  time.advance(2000);
  await setImmediate();
  assert.equal(downloads.length, 1, elements.get("log").textContent);
  assert.match(downloads[0].filename, /^DogDog-logs\/controller-error-.*\.txt$/);
  assert.equal(downloads[0].saveAs, false);
  assert.equal(downloads[0].conflictAction, "uniquify");
  const text = reportText(downloads[0]);
  assert.match(text, /USB VID 0x303a; PID 0x1001/);
  assert.ok(text.includes("I (1) main: Ready\r\nW (2) Sensor: Gerät offline\r\nBacktrace:"));
  assert.ok(text.includes("  frame_two\r\n"));
  assert.ok(!text.includes("\x1b"));
  assert.match(elements.get("diagnosticsStatus").textContent, /saved/);
  await api.disconnectSerial();
});

test("manual disconnect, unplug and stream EOF flush pending diagnostics and release reader locks", async () => {
  for (const end of ["disconnect", "unplug", "eof"]) {
    const { api, downloads, elements, serialPort, streamController, time } = bridge();
    await api.connectSerial();
    streamController.enqueue(bytes("Guru Meditation Error: Core 0 panic'ed\nBacktrace: 0x40081234"));
    await setImmediate();
    if (end === "disconnect") await api.disconnectSerial();
    if (end === "eof") streamController.close();
    if (end === "unplug") {
      serialPort.readable = null;
      streamController.error(new Error("Device disconnected"));
    }
    await setImmediate();
    assert.equal(downloads.length, 1, `${end}: ${elements.get("log").textContent}`);
    assert.match(reportText(downloads[0]), /Backtrace: 0x40081234/);
    assert.equal(elements.get("serialStatus").textContent, "Serial disconnected");
    assert.equal(time.timers.size, 0);
  }
});

test("download API rejection and interrupted downloads remain visible in the bridge", async () => {
  for (const failure of ["rejected", "interrupted"]) {
    const { api, elements, streamController, time, notify } = bridge(failure === "rejected" ?
      { downloadError: "Downloads disabled" } : { downloadState: "in_progress" });
    await api.connectSerial();
    streamController.enqueue(bytes("E (1) main: Failed\n"));
    await setImmediate();
    time.advance(2000);
    await setImmediate();
    if (failure === "interrupted") notify({ id: 1, state: { current: "interrupted" },
      error: { current: "FILE_ACCESS_DENIED" } });
    assert.match(elements.get("diagnosticsStatus").textContent,
      failure === "rejected" ? /Downloads disabled/ : /FILE_ACCESS_DENIED/);
    await api.disconnectSerial();
  }
});

test("starter protocol and extension download permission are retained", () => {
  const { api } = bridge();
  assert.equal(api.buildSerialLine({ firstName: "Teste", lastName: "TEST", dogName: "DemoDogE" }),
    "competitor|Teste|TEST|DemoDogE");
  const manifest = JSON.parse(readFileSync(new URL("manifest.json", extension), "utf8"));
  assert.ok(manifest.permissions.includes("downloads"));
});
