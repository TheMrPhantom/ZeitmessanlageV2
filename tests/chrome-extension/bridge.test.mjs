import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { setImmediate } from "node:timers/promises";
import test from "node:test";
import vm from "node:vm";

const extension = new URL("../../chrome-starter-serial-extension/", import.meta.url);
const sources = ["controller-diagnostics.js", "bridge.js"].map((file) =>
  readFileSync(new URL(file, extension), "utf8"));
const starter = (number) => ({ startNumber: String(number), firstName: "Test",
  lastName: `Starter${number}`, dogName: `Dog${number}` });
const line = (number) => `competitor|Test|Starter${number}|Dog${number}\r\n`;

function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

function bridge() {
  const elements = new Map();
  const serialListeners = new Map();
  const timers = new Map();
  const ports = [];
  let nextPort;
  let timerSequence = 0;
  let now = 0;
  let requestCount = 0;
  function makePort({ open = async () => {}, write = async () => {}, close = async () => {} } = {}) {
    let input;
    const writes = [];
    const serialPort = {
      openCount: 0, closeCount: 0,
      async open(options) {
        this.openCount++;
        await open(options);
        this.readable = new ReadableStream({ start(controller) { input = controller; } });
        this.writable = new WritableStream({ write(bytes) {
          writes.push(new TextDecoder().decode(bytes));
          return write(bytes);
        } });
      },
      async close() {
        this.closeCount++;
        assert.equal(Boolean(this.readable?.locked), false, "reader lock leaked");
        assert.equal(Boolean(this.writable?.locked), false, "writer lock leaked");
        await close();
        this.readable = this.writable = null;
      },
      getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }),
      get input() { return input; },
      writes
    };
    ports.push(serialPort);
    nextPort = serialPort;
    return serialPort;
  }
  const context = vm.createContext({
    TextDecoder, TextEncoder, AbortController,
    setTimeout(callback, delay) {
      timers.set(++timerSequence, { callback, at: now + delay });
      return timerSequence;
    },
    clearTimeout(id) { timers.delete(id); },
    navigator: { serial: {
      requestPort: async () => { requestCount++; return nextPort; },
      addEventListener(name, listener) { serialListeners.set(name, listener); }
    } },
    document: { getElementById(id) {
      if (!elements.has(id)) elements.set(id, {
        textContent: "", checked: true, disabled: false, listeners: new Map(),
        classList: { toggle() {} },
        addEventListener(name, listener) { this.listeners.set(name, listener); }
      });
      return elements.get(id);
    } },
    chrome: {
      runtime: { onMessage: { addListener() {} }, sendMessage(message, callback) { callback({}); } },
      downloads: {
        download: async () => 1, search: async () => [], onChanged: { addListener() {} }
      }
    }
  });
  for (const source of sources) vm.runInContext(source, context);
  const api = vm.runInContext("({ connectSerial, disconnectSerial, toggleSerial, applyStarter, maybeAutoSend, sendCurrentStarter })", context);
  return { api, elements, ports, makePort, timers,
    get requestCount() { return requestCount; },
    log: () => elements.get("log").textContent,
    disconnectPort(serialPort) { serialListeners.get("disconnect")?.({ target: serialPort }); },
    async advance(ms) {
      now += ms;
      for (const [id, timer] of [...timers]) {
        if (timer.at <= now && timers.delete(id)) timer.callback();
      }
      await setImmediate();
    }
  };
}

test("reconnecting sends the current starter again even when it has not changed", async () => {
  const b = bridge();
  const port = b.makePort();
  b.api.applyStarter(starter(1));
  await b.api.connectSerial();
  await b.api.disconnectSerial();
  await b.api.connectSerial();
  assert.deepEqual(port.writes, [line(1), line(1)]);
  await b.api.disconnectSerial();
});

test("starter changes during a slow write are serialized and the latest starter is sent", async () => {
  const b = bridge();
  const pending = deferred();
  const port = b.makePort({ write: () => port.writes.length === 1 ? pending.promise : undefined });
  await b.api.connectSerial();
  b.api.applyStarter(starter(1));
  await setImmediate();
  b.api.applyStarter(starter(2));
  b.api.applyStarter(starter(3));
  pending.resolve();
  await setImmediate();
  await b.api.maybeAutoSend();
  assert.deepEqual(port.writes, [line(1), line(3)]);
  assert.doesNotMatch(b.log(), /failed|locked/);
  await b.api.disconnectSerial();
});

test("a write that never settles times out visibly and releases the port for reconnect", async () => {
  const b = bridge();
  const port = b.makePort({ write: () => new Promise(() => {}) });
  await b.api.connectSerial();
  b.api.applyStarter(starter(1));
  await setImmediate();
  await b.advance(5000);
  assert.match(b.log(), /timed out/i);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial reconnecting (1/5)");
  assert.equal(port.closeCount, 1);
  assert.equal(port.writable, null);
  const replacement = b.makePort();
  await b.api.connectSerial();
  assert.deepEqual(replacement.writes, [line(1)]);
  await b.api.disconnectSerial();
  assert.equal(b.timers.size, 0);
});

test("Disconnect cancels an in-flight write before closing, without losing the port handle", async () => {
  const b = bridge();
  const port = b.makePort({ write: () => new Promise(() => {}) });
  await b.api.connectSerial();
  b.api.applyStarter(starter(1));
  await setImmediate();
  await b.api.toggleSerial();
  assert.equal(port.closeCount, 1, b.log());
  assert.equal(port.writable, null);
  assert.doesNotMatch(b.log(), /locked/);
  assert.equal(b.timers.size, 0);
});

test("USB disconnect events stop a silent read and write session", async () => {
  const b = bridge();
  const port = b.makePort();
  await b.api.connectSerial();
  b.disconnectPort(b.makePort());
  await setImmediate();
  assert.equal(port.closeCount, 0, "unrelated ports must not end this connection");
  b.disconnectPort(port);
  await setImmediate();
  assert.equal(port.closeCount, 1);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial reconnecting (1/5)");
  assert.match(b.log(), /USB.*disconnect/i);
  await b.api.disconnectSerial();
  assert.equal(b.timers.size, 0);
});

test("a rejected write disconnects, and rapid Connect clicks open only one port", async () => {
  const b = bridge();
  const port = b.makePort({ write: async () => { throw new Error("Device unavailable"); } });
  await Promise.all([b.api.toggleSerial(), b.api.toggleSerial()]);
  assert.equal(port.openCount, 1);
  b.api.applyStarter(starter(1));
  await setImmediate();
  assert.match(b.log(), /Device unavailable/);
  assert.equal(port.closeCount, 1);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial reconnecting (1/5)");
  await b.api.disconnectSerial();
  assert.equal(b.timers.size, 0);
});

test("a failed close retains the handle so Disconnect can retry", async () => {
  const b = bridge();
  let fail = true;
  const port = b.makePort({ close: async () => { if (fail) throw new Error("Close failed"); } });
  await b.api.toggleSerial();
  await b.api.toggleSerial();
  assert.match(b.log(), /Close failed/);
  fail = false;
  await b.api.toggleSerial();
  assert.equal(port.closeCount, 2);
  assert.equal(port.openCount, 1);
  assert.equal(b.elements.get("connectButton").textContent, "Connect Serial");
});

test("a timed-out close remains retryable without issuing overlapping close calls", async () => {
  const b = bridge();
  const pending = deferred();
  const port = b.makePort({ close: () => pending.promise });
  await b.api.toggleSerial();
  const disconnecting = b.api.toggleSerial();
  await setImmediate();
  await b.advance(3000);
  await disconnecting;
  assert.match(b.log(), /close timed out/);
  assert.equal(b.elements.get("connectButton").textContent, "Retry Disconnect");
  assert.equal(b.elements.get("connectButton").disabled, false);
  const retrying = b.api.toggleSerial();
  await setImmediate();
  assert.equal(port.closeCount, 1);
  pending.resolve();
  await retrying;
  assert.equal(b.elements.get("connectButton").textContent, "Connect Serial");
  assert.equal(b.timers.size, 0);
});

test("old queued starter sends are cancelled on disconnect and cannot reach the replacement port", async () => {
  const b = bridge();
  const pending = deferred();
  const oldPort = b.makePort({ write: () => pending.promise });
  await b.api.connectSerial();
  b.api.applyStarter(starter(1));
  await setImmediate();
  b.api.applyStarter(starter(2));
  await b.api.disconnectSerial();
  const newPort = b.makePort();
  await b.api.connectSerial();
  pending.resolve();
  await setImmediate();
  assert.deepEqual(oldPort.writes, [line(1)]);
  assert.deepEqual(newPort.writes, [line(2)]);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial connected");
  await b.api.disconnectSerial();
});

test("recoverable read errors keep the session usable, while EOF cancels a pending write", async () => {
  const b = bridge();
  const pending = deferred();
  const port = b.makePort({ write: () => port.writes.length === 2 ? pending.promise : undefined });
  await b.api.connectSerial();
  const oldInput = port.input;
  let newInput;
  port.readable = new ReadableStream({ start(controller) { newInput = controller; } });
  oldInput.error(new Error("Recoverable framing error"));
  await setImmediate();
  assert.match(b.log(), /framing error/);
  assert.equal(port.closeCount, 0);
  b.api.applyStarter(starter(1));
  await setImmediate();
  b.api.applyStarter(starter(2));
  await setImmediate();
  newInput.close();
  await setImmediate();
  assert.equal(port.closeCount, 1);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial reconnecting (1/5)");
  await b.api.disconnectSerial();
  assert.equal(b.timers.size, 0);
});

test("enabling Auto-send sends an unsent current starter", async () => {
  const b = bridge();
  const port = b.makePort();
  b.elements.get("autoSend").checked = false;
  b.api.applyStarter(starter(1));
  await b.api.connectSerial();
  assert.deepEqual(port.writes, []);
  b.elements.get("autoSend").checked = true;
  b.elements.get("autoSend").listeners.get("change")?.();
  await setImmediate();
  assert.deepEqual(port.writes, [line(1)]);
  await b.api.disconnectSerial();
});

test("device loss and Illegal invocation reconnect to the selected port and send the latest starter", async () => {
  const b = bridge();
  const port = b.makePort();
  b.api.applyStarter(starter(1));
  await b.api.connectSerial();
  const unrelated = b.makePort();
  let failGetter = true;
  Object.defineProperty(port, "readable", {
    configurable: true,
    get() {
      if (failGetter) { failGetter = false; throw new TypeError("Illegal invocation"); }
      return null;
    },
    set(value) { Object.defineProperty(port, "readable", { configurable: true, writable: true, value }); }
  });
  port.input.error(new Error("The device has been lost."));
  await setImmediate();
  assert.match(b.log(), /Serial read stopped: The device has been lost/);
  assert.match(b.log(), /Serial read failed: Illegal invocation/);
  assert.equal(port.closeCount, 1);
  assert.equal(b.elements.get("connectButton").textContent, "Cancel Reconnect");
  assert.equal(b.elements.get("sendButton").disabled, true);
  b.api.applyStarter(starter(2));
  await b.advance(1999);
  assert.equal(port.openCount, 1, "must wait before retrying");
  await b.advance(1);
  await b.advance(1000);
  assert.equal(port.openCount, 2);
  assert.equal(b.requestCount, 1, "automatic retries must not open the port chooser");
  assert.equal(unrelated.openCount, 0, "must not switch to another identical USB device");
  assert.deepEqual(port.writes, [line(1), line(2)]);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial connected");
  assert.equal(b.elements.get("sendButton").disabled, false);
  assert.match(b.log(), /reconnected on attempt 1\/5/);
  await b.api.disconnectSerial();
  assert.equal(b.timers.size, 0);
});

test("reconnect waits through temporary open failures and resends an unchanged starter", async () => {
  const b = bridge();
  const port = b.makePort({ open: async (options) => {
    assert.equal(options.baudRate, 115200);
    if (port.openCount === 2 || port.openCount === 3) throw new Error("Port unavailable");
  } });
  b.api.applyStarter(starter(1));
  await b.api.connectSerial();
  b.disconnectPort(port);
  b.disconnectPort(port);
  await setImmediate();
  for (let attempt = 1; attempt <= 3; attempt++) await b.advance(2000);
  await b.advance(1000);
  assert.equal(port.openCount, 4);
  assert.equal(b.requestCount, 1);
  assert.deepEqual(port.writes, [line(1), line(1)]);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial connected");
  assert.match(b.log(), /reconnected on attempt 3\/5/);
  await b.api.disconnectSerial();
  assert.equal(b.timers.size, 0);
});

test("unavailable ports stop after five attempts and remain manually connectable", async () => {
  const b = bridge();
  const port = b.makePort({ open: async () => {
    if (port.openCount > 1) throw new Error("Device still missing");
  } });
  await b.api.connectSerial();
  b.disconnectPort(port);
  await setImmediate();
  for (let attempt = 1; attempt <= 5; attempt++) await b.advance(2000);
  assert.equal(port.openCount, 6);
  assert.equal(b.requestCount, 1);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial disconnected");
  assert.equal(b.elements.get("connectButton").textContent, "Connect Serial");
  assert.equal(b.elements.get("connectButton").disabled, false);
  assert.match(b.log(), /stopped after 5 attempts/);
  assert.equal(b.timers.size, 0);
  await b.advance(60000);
  assert.equal(port.openCount, 6);
  const replacement = b.makePort();
  await b.api.toggleSerial();
  assert.equal(replacement.openCount, 1);
  await b.api.disconnectSerial();
});

test("repeated send failures during reconnect share the same five-attempt limit", async () => {
  const b = bridge();
  const port = b.makePort({ write: async () => { throw new Error("Device unavailable"); } });
  await b.api.connectSerial();
  b.api.applyStarter(starter(1));
  await setImmediate();
  for (let attempt = 1; attempt <= 5; attempt++) await b.advance(2000);
  assert.equal(port.openCount, 6);
  assert.equal(port.closeCount, 6);
  assert.equal(port.writes.length, 6);
  assert.match(b.log(), /stopped after 5 attempts/);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial disconnected");
  assert.equal(b.timers.size, 0);
});

test("immediate EOF during reconnect does not reset the attempt limit", async () => {
  const b = bridge();
  const port = b.makePort();
  const open = port.open.bind(port);
  port.open = async (options) => {
    await open(options);
    if (port.openCount > 1) port.input.close();
  };
  await b.api.connectSerial();
  b.disconnectPort(port);
  await setImmediate();
  for (let attempt = 1; attempt <= 5; attempt++) await b.advance(2000);
  assert.equal(port.openCount, 6);
  assert.equal(port.closeCount, 6);
  assert.match(b.log(), /stopped after 5 attempts/);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial disconnected");
  assert.equal(b.timers.size, 0);
});

test("manual disconnect and Cancel Reconnect prevent future automatic attempts", async () => {
  for (const lost of [false, true]) {
    const b = bridge();
    const port = b.makePort();
    await b.api.toggleSerial();
    if (lost) {
      b.disconnectPort(port);
      await setImmediate();
      assert.equal(b.elements.get("connectButton").disabled, false);
    }
    await Promise.all([b.api.toggleSerial(), b.api.toggleSerial()]);
    assert.equal(b.elements.get("serialStatus").textContent, "Serial disconnected");
    assert.equal(b.elements.get("connectButton").textContent, "Connect Serial");
    assert.equal(b.timers.size, 0);
    await b.advance(60000);
    assert.equal(port.openCount, 1);
    assert.equal(port.closeCount, 1);
  }
});

test("cancelling an in-flight reopen closes the late port without sending the starter", async () => {
  const b = bridge();
  const pending = deferred();
  const port = b.makePort({ open: () => port.openCount === 2 ? pending.promise : undefined });
  b.api.applyStarter(starter(1));
  await b.api.connectSerial();
  b.disconnectPort(port);
  await setImmediate();
  await b.advance(2000);
  assert.equal(port.openCount, 2);
  const cancelling = b.api.toggleSerial();
  pending.resolve();
  await cancelling;
  assert.equal(port.closeCount, 2);
  assert.equal(port.readable, null);
  assert.deepEqual(port.writes, [line(1)]);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial disconnected");
  assert.equal(b.timers.size, 0);
});

test("reconnect retries failed cleanup before reopening the original port", async () => {
  const b = bridge();
  let failClose = true;
  const port = b.makePort({ close: async () => { if (failClose) throw new Error("Close failed"); } });
  await b.api.connectSerial();
  b.disconnectPort(port);
  await setImmediate();
  await b.advance(2000);
  assert.equal(port.openCount, 1, "must not open a port whose cleanup failed");
  failClose = false;
  await b.advance(2000);
  assert.equal(port.closeCount, 3);
  assert.equal(port.openCount, 2);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial connected");
  await b.api.disconnectSerial();
  assert.equal(b.timers.size, 0);
});

test("reconnect waits for a timed-out close without overlapping close or open calls", async () => {
  const b = bridge();
  const pending = deferred();
  const port = b.makePort({ close: () => pending.promise });
  await b.api.connectSerial();
  b.disconnectPort(port);
  await setImmediate();
  await b.advance(3000);
  assert.match(b.log(), /close timed out/);
  await b.advance(2000);
  assert.equal(port.closeCount, 1);
  assert.equal(port.openCount, 1, "must wait for the pending close");
  pending.resolve();
  await setImmediate();
  assert.equal(port.closeCount, 1);
  assert.equal(port.openCount, 2);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial connected");
  await b.api.disconnectSerial();
  assert.equal(b.timers.size, 0);
});

test("automatic reconnect honors disabled Auto-send", async () => {
  const b = bridge();
  const port = b.makePort();
  b.elements.get("autoSend").checked = false;
  b.api.applyStarter(starter(1));
  await b.api.connectSerial();
  b.disconnectPort(port);
  await setImmediate();
  await b.advance(2000);
  assert.equal(port.openCount, 2);
  assert.deepEqual(port.writes, []);
  assert.equal(b.elements.get("serialStatus").textContent, "Serial connected");
  await b.api.disconnectSerial();
  assert.equal(b.timers.size, 0);
});
