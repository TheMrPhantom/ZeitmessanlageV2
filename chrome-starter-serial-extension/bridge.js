const BAUD_RATE = 115200;
const WRITE_TIMEOUT_MS = 5000;
const CLOSE_TIMEOUT_MS = 3000;
const RECONNECT_DELAY_MS = 2000;
const RECONNECT_ATTEMPTS = 5;
const RECONNECT_STABLE_MS = 1000;

let latestStarter = null;
let connection = null;
let serialBusy = false;
let reconnecting = null;
let diagnosticSequence = 0;
const diagnosticDownloads = new Map();

const connectButton = document.getElementById("connectButton");
const sendButton = document.getElementById("sendButton");
const autoSend = document.getElementById("autoSend");
const starterName = document.getElementById("starterName");
const dogName = document.getElementById("dogName");
const serialLine = document.getElementById("serialLine");
const serialStatus = document.getElementById("serialStatus");
const pageStatus = document.getElementById("pageStatus");
const log = document.getElementById("log");
const diagnosticsStatus = document.getElementById("diagnosticsStatus");

function appendLog(message) {
  const time = new Date().toLocaleTimeString();
  log.textContent = `[${time}] ${message}\n${log.textContent}`.slice(0, 5000);
}

async function saveDiagnosticReport(report, portInfo) {
  const timestamp = report.startedAt.replace(/[:.]/g, "-");
  const filename = `DogDog-logs/controller-${report.severity}-${timestamp}-${++diagnosticSequence}.txt`;
  const usbId = (value) => value === undefined ? "unknown" : `0x${value.toString(16).padStart(4, "0")}`;
  const text = [
    "DogDog controller serial diagnostic report",
    `First warning/error (UTC): ${report.startedAt}`,
    `Capture ended (UTC): ${report.endedAt}`,
    `Severity: ${report.severity}`,
    `Warnings: ${report.warningCount}; errors/trace markers: ${report.errorCount}`,
    `Serial: ${BAUD_RATE} baud; USB VID ${usbId(portInfo.usbVendorId)}; PID ${usbId(portInfo.usbProductId)}`,
    `Capture ended because: ${report.reason}`,
    "",
    "Controller output in receive order, including preceding context and any emitted stack trace.",
    "Warnings/errors do not always include a stack trace. Addresses are preserved as received.",
    "",
    ...report.lines,
    ""
  ].join("\r\n");

  try {
    // A data URL lets Chrome finish writing even after the bridge tab closes.
    const id = await chrome.downloads.download({
      url: `data:text/plain;charset=utf-8,${encodeURIComponent(text)}`,
      filename,
      conflictAction: "uniquify",
      saveAs: false
    });
    diagnosticDownloads.set(id, filename);
    diagnosticsStatus.textContent = `Saving controller diagnostic: ${filename}`;
    appendLog(`Controller diagnostic download started: ${filename}`);
    // A small local download may already be complete before the listener sees it.
    const [download] = await chrome.downloads.search({ id });
    if (download) updateDiagnosticDownload(download.id, download.state, download.error);
  } catch (error) {
    diagnosticsStatus.textContent = `Could not save controller diagnostic: ${error.message}`;
    appendLog(`Controller diagnostic save failed: ${error.message}`);
  }
}

function updateDiagnosticDownload(id, state, error) {
  const filename = diagnosticDownloads.get(id);
  if (!filename || !["complete", "interrupted"].includes(state)) return;
  diagnosticDownloads.delete(id);
  const message = state === "complete" ? `Controller diagnostic saved: ${filename}` :
    `Controller diagnostic download failed: ${filename} (${error || "interrupted"})`;
  diagnosticsStatus.textContent = message;
  appendLog(message);
}

chrome.downloads.onChanged.addListener((delta) => {
  updateDiagnosticDownload(delta.id, delta.state?.current, delta.error?.current);
});

function sanitizeField(value) {
  return String(value ?? "")
    .replace(/[|\r\n]/g, " ")
    .replace(/\s+/g, " ")
    .trim();
}

function starterKey(starter) {
  return [
    starter?.startNumber,
    starter?.firstName,
    starter?.lastName,
    starter?.dogName
  ].map(sanitizeField).join("|");
}

function buildSerialLine(starter) {
  return [
    "competitor",
    sanitizeField(starter.firstName),
    sanitizeField(starter.lastName),
    sanitizeField(starter.dogName)
  ].join("|");
}

function updateStarterView() {
  if (!latestStarter) {
    starterName.textContent = "-";
    dogName.textContent = "-";
    serialLine.textContent = "-";
    pageStatus.textContent = "Waiting for a webmelden starter page...";
    sendButton.disabled = true;
    return;
  }

  starterName.textContent = latestStarter.starterName || [latestStarter.firstName, latestStarter.lastName].filter(Boolean).join(" ");
  dogName.textContent = latestStarter.dogName || "-";
  serialLine.textContent = buildSerialLine(latestStarter);
  pageStatus.textContent = latestStarter.pageTitle || latestStarter.pageUrl || "Starter detected";
  sendButton.disabled = !connection || connection.stopping;
}

function updateSerialView() {
  const connected = Boolean(connection && !connection.stopping);
  serialStatus.textContent = connected ? "Serial connected" : reconnecting ?
    `Serial reconnecting (${reconnecting.attempt || 1}/${RECONNECT_ATTEMPTS})` : "Serial disconnected";
  serialStatus.classList.toggle("connected", connected);
  serialStatus.classList.toggle("disconnected", !connected);
  connectButton.textContent = reconnecting ? "Cancel Reconnect" :
    connection ? (connection.stopping ? "Retry Disconnect" : "Disconnect") : "Connect Serial";
  connectButton.disabled = serialBusy || Boolean(connection?.closeTask && !reconnecting);
  sendButton.disabled = !connected || !latestStarter;
}

async function waitForSerial(operation, timeout, message, signal) {
  let timer;
  let onAbort;
  try {
    return await Promise.race([
      operation,
      new Promise((resolve, reject) => {
        timer = setTimeout(() => reject(new Error(message)), timeout);
        onAbort = () => reject(new Error("Serial connection closed."));
        if (signal?.aborted) onAbort();
        else signal?.addEventListener("abort", onAbort, { once: true });
      })
    ]);
  } finally {
    clearTimeout(timer);
    signal?.removeEventListener("abort", onAbort);
  }
}

async function writeLine(session, line) {
  if (!session.port.writable || session.stopping) {
    throw new Error("Serial port is not connected.");
  }

  const writer = session.port.writable.getWriter();
  try {
    await waitForSerial(writer.write(new TextEncoder().encode(`${line}\r\n`)),
      WRITE_TIMEOUT_MS, "Serial write timed out after 5 seconds. Reconnect the controller.",
      session.abortController.signal);
  } catch (error) {
    // Abort the underlying serial write, but do not wait for a stalled driver.
    writer.abort(error).catch(() => {});
    throw error;
  } finally {
    writer.releaseLock();
  }
}

async function sendCurrentStarter(reason = "manual") {
  if (!latestStarter) {
    appendLog("No starter detected yet.");
    return;
  }

  const session = connection;
  if (!session || session.stopping) throw new Error("Serial port is not connected.");

  // Only one writer may own the stream. Read the latest starter when the queued
  // send starts, so rapid changes replace obsolete pending auto-sends.
  const sending = session.sendQueue.then(async () => {
    if (connection !== session || session.stopping || !latestStarter) return;
    const starter = latestStarter;
    const key = starterKey(starter);
    if (reason === "auto" && (!autoSend.checked || key === session.lastSentKey)) return;

    const line = buildSerialLine(starter);
    try {
      session.activeWrite = writeLine(session, line);
      await session.activeWrite;
    } catch (error) {
      if (session.stopping) return;
      // Keep cleanup separate from the send queue: shutdown cancels queued sends.
      recoverSerialConnection(session, `Serial send failed: ${error.message}`);
      throw error;
    } finally {
      session.activeWrite = null;
    }
    if (connection !== session || session.stopping) return;
    // Record the snapshot actually written, never a starter that arrived mid-write.
    session.lastSentKey = key;
    appendLog(`${reason === "auto" ? "Auto-sent" : "Sent"}: ${line}`);
  });
  session.sendQueue = sending.catch(() => {});
  await sending;
}

async function maybeAutoSend() {
  if (!autoSend.checked || !connection || connection.stopping || !latestStarter) {
    return;
  }

  await sendCurrentStarter("auto");
}

async function readSerialOutput(session, diagnostics) {
  const serialPort = session.port;
  const signal = session.abortController.signal;
  try {
    while (serialPort.readable && !signal.aborted) {
      const reader = serialPort.readable.getReader();
      session.reader = reader;
      try {
        while (!signal.aborted) {
          const { value, done } = await reader.read();
          if (done) {
            return;
          }
          if (value) {
            diagnostics.push(value);
          }
        }
      } catch (error) {
        if (!signal.aborted) {
          appendLog(`Serial read stopped: ${error.message}`);
        }
      } finally {
        session.reader = null;
        reader.releaseLock();
      }
    }
  } finally {
    diagnostics.close(session.endReason || "serial connection ended");
  }
}

function closeSerialConnection(session, reason) {
  if (session.closeTask) return session.closeTask;
  session.stopping = true;
  session.endReason = reason;
  session.abortController.abort();
  appendLog(reason);
  session.closeTask = (async () => {
    try {
      // Cancellation wakes both read() and our pending write waiter. Their
      // finally blocks release stream locks before port.close() is attempted.
      const cancelling = session.reader?.cancel();
      await waitForSerial(Promise.allSettled([cancelling, session.readTask, session.activeWrite]),
        CLOSE_TIMEOUT_MS, "Serial stream cleanup timed out.");
    } catch (error) {
      appendLog(error.message);
    }
    try {
      // Keep a pending close promise so Retry Disconnect never races close().
      session.portCloseTask ??= session.port.close().catch((error) => {
        session.portCloseTask = null;
        throw error;
      });
      await waitForSerial(session.portCloseTask, CLOSE_TIMEOUT_MS, "Serial port close timed out.");
      if (connection === session) connection = null;
    } catch (error) {
      // Losing this handle would strand an open port and make reconnect fail.
      appendLog(`Could not close serial port: ${error.message} Use Retry Disconnect; if the port is missing, check USB and chrome://device-log.`);
    } finally {
      session.closeTask = null;
      updateSerialView();
    }
  })();
  updateSerialView();
  return session.closeTask;
}

async function openSerialConnection(selectedPort, retry = null) {
  await selectedPort.open({ baudRate: BAUD_RATE });
  const session = {
    port: selectedPort, reader: null, activeWrite: null,
    abortController: new AbortController(), sendQueue: Promise.resolve(), lastSentKey: "",
    readTask: null, closeTask: null, portCloseTask: null, stopping: false
  };
  connection = session;
  // open() cannot be aborted. A cancelled attempt must close its port as soon
  // as it finishes, before starting reads or sending a starter.
  if (retry?.cancelled) {
    await closeSerialConnection(session, "Disconnected.");
    return session;
  }
  const portInfo = selectedPort.getInfo();
  const diagnostics = new ControllerDiagnostics({
    onLine: (line) => appendLog(`Controller: ${line}`),
    onCapture: () => {
      diagnosticsStatus.textContent = "Controller warning/error detected; collecting diagnostic output...";
    },
    onReport: (report) => { saveDiagnosticReport(report, portInfo); }
  });
  session.readTask = readSerialOutput(session, diagnostics);
  session.readTask.then(() => {
    if (!session.stopping) recoverSerialConnection(session, "Serial connection ended.");
  }, (error) => {
    if (!session.stopping) recoverSerialConnection(session, `Serial read failed: ${error.message}`);
  });
  updateSerialView();
  appendLog(`Connected at ${BAUD_RATE} baud.`);
  await maybeAutoSend();
  return session;
}

function recoverSerialConnection(session, reason) {
  if (connection !== session || session.stopping) return;
  // Read, write, and USB errors may arrive together. An existing retry loop
  // also owns failures during its new session, so its attempt limit persists.
  if (reconnecting) {
    closeSerialConnection(session, reason);
    return;
  }
  const retry = { port: session.port, attempt: 0, cancelled: false, timer: null, wake: null, task: null };
  reconnecting = retry;
  const closing = closeSerialConnection(session, reason);
  appendLog(`Will try to reconnect to the same serial port ${RECONNECT_ATTEMPTS} times, ${RECONNECT_DELAY_MS / 1000} seconds apart.`);
  retry.task = reconnectSerial(retry, closing);
}

async function waitForReconnect(retry, delay, session = null) {
  try {
    const waiting = new Promise((resolve) => {
      retry.wake = resolve;
      retry.timer = setTimeout(() => resolve(true), delay);
    });
    return await Promise.race(session ? [waiting, session.readTask.then(() => false, () => false)] : [waiting]);
  } finally {
    clearTimeout(retry.timer);
    retry.timer = retry.wake = null;
  }
}

async function reconnectSerial(retry, closing) {
  try {
    await closing;
    for (retry.attempt = 1; retry.attempt <= RECONNECT_ATTEMPTS; retry.attempt++) {
      if (retry.cancelled) return;
      updateSerialView();
      await waitForReconnect(retry, RECONNECT_DELAY_MS);
      if (retry.cancelled) return;
      appendLog(`Serial reconnect attempt ${retry.attempt}/${RECONNECT_ATTEMPTS}.`);
      try {
        // Retry incomplete cleanup without overlapping close() or open().
        if (connection) await closeSerialConnection(connection, "Closing serial port before reconnecting.");
        if (retry.cancelled) return;
        if (connection) throw new Error("The previous serial connection could not be closed.");
        const session = await openSerialConnection(retry.port, retry);
        if (retry.cancelled) return;
        // A driver can open successfully and immediately lose its read stream.
        // Keep the attempt budget until the new session has stayed up briefly.
        const stable = !session.stopping && await waitForReconnect(retry, RECONNECT_STABLE_MS, session);
        if (retry.cancelled) return;
        if (!stable || connection !== session || session.stopping) throw new Error("Serial connection ended during reconnect.");
        appendLog(`Serial reconnected on attempt ${retry.attempt}/${RECONNECT_ATTEMPTS}.`);
        return;
      } catch (error) {
        if (!retry.cancelled) appendLog(`Serial reconnect attempt ${retry.attempt} failed: ${error.message}`);
      }
    }
    const nextStep = connection ? "Use Retry Disconnect to finish closing the port." : "Use Connect Serial to try again.";
    appendLog(`Automatic reconnect stopped after ${RECONNECT_ATTEMPTS} attempts. Check USB. ${nextStep}`);
  } finally {
    if (reconnecting === retry) reconnecting = null;
    updateSerialView();
  }
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    appendLog("Web Serial is not available in this Chrome profile.");
    return;
  }
  if (reconnecting) await disconnectSerial();
  const selectedPort = await navigator.serial.requestPort();
  await openSerialConnection(selectedPort);
}

async function disconnectSerial() {
  const retry = reconnecting;
  if (retry) {
    retry.cancelled = true;
    clearTimeout(retry.timer);
    retry.wake?.();
    appendLog("Automatic reconnect cancelled.");
  }
  if (connection) await closeSerialConnection(connection, "Disconnected.");
  await retry?.task;
}

async function toggleSerial() {
  if (serialBusy || (connection?.closeTask && !reconnecting)) return;
  serialBusy = true;
  updateSerialView();
  try {
    if (connection || reconnecting) {
      await disconnectSerial();
    } else {
      await connectSerial();
    }
  } catch (error) {
    const hint = error.name === "NotFoundError" ?
      " If the port list is empty, check USB/Device Manager and chrome://device-log." : "";
    appendLog(`Serial error: ${error.message}${hint}`);
  } finally {
    serialBusy = false;
    updateSerialView();
  }
}

function applyStarter(starter) {
  latestStarter = starter;
  updateStarterView();
  maybeAutoSend().catch((error) => appendLog(`Auto-send failed: ${error.message}`));
}

connectButton.addEventListener("click", toggleSerial);
autoSend.addEventListener("change", () => {
  maybeAutoSend().catch((error) => appendLog(`Auto-send failed: ${error.message}`));
});
sendButton.addEventListener("click", () => {
  sendCurrentStarter().catch((error) => appendLog(`Send failed: ${error.message}`));
});

navigator.serial?.addEventListener("disconnect", (event) => {
  const disconnectedPort = event.port || event.target;
  if (connection?.port === disconnectedPort) {
    recoverSerialConnection(connection, "USB serial device disconnected.");
  }
});

chrome.runtime.onMessage.addListener((message) => {
  if (message?.type === "STARTER_CHANGED") {
    applyStarter(message.payload);
  }
});

chrome.runtime.sendMessage({ type: "GET_LATEST_STARTER" }, (response) => {
  if (response?.payload) {
    applyStarter(response.payload);
  } else {
    updateStarterView();
  }
});

updateSerialView();
