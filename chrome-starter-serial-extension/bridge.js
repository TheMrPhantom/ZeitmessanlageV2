const BAUD_RATE = 115200;

let latestStarter = null;
let port = null;
let currentReader = null;
let lastSentKey = "";
let readAbortController = null;
let readTask = null;
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
  sendButton.disabled = !port;
}

function updateSerialView() {
  const connected = Boolean(port);
  serialStatus.textContent = connected ? "Serial connected" : "Serial disconnected";
  serialStatus.classList.toggle("connected", connected);
  serialStatus.classList.toggle("disconnected", !connected);
  connectButton.textContent = connected ? "Disconnect" : "Connect Serial";
  sendButton.disabled = !connected || !latestStarter;
}

async function writeLine(line) {
  if (!port?.writable) {
    throw new Error("Serial port is not connected.");
  }

  const writer = port.writable.getWriter();
  try {
    await writer.write(new TextEncoder().encode(`${line}\r\n`));
  } finally {
    writer.releaseLock();
  }
}

async function sendCurrentStarter(reason = "manual") {
  if (!latestStarter) {
    appendLog("No starter detected yet.");
    return;
  }

  const line = buildSerialLine(latestStarter);
  await writeLine(line);
  lastSentKey = starterKey(latestStarter);
  appendLog(`${reason === "auto" ? "Auto-sent" : "Sent"}: ${line}`);
}

async function maybeAutoSend() {
  if (!autoSend.checked || !port || !latestStarter) {
    return;
  }

  const key = starterKey(latestStarter);
  if (key === lastSentKey) {
    return;
  }

  await sendCurrentStarter("auto");
}

async function readSerialOutput(serialPort, signal, diagnostics) {
  try {
    while (serialPort.readable && !signal.aborted) {
      const reader = serialPort.readable.getReader();
      currentReader = reader;
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
        currentReader = null;
        reader.releaseLock();
      }
    }
  } finally {
    diagnostics.close(signal.aborted ? "serial disconnected by user" : "serial connection ended");
    if (!signal.aborted && port === serialPort) {
      port = null;
      updateSerialView();
      appendLog("Serial connection ended.");
      await serialPort.close().catch(() => {});
    }
  }
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    appendLog("Web Serial is not available in this Chrome profile.");
    return;
  }

  const selectedPort = await navigator.serial.requestPort();
  await selectedPort.open({ baudRate: BAUD_RATE });
  port = selectedPort;
  const portInfo = selectedPort.getInfo();
  const diagnostics = new ControllerDiagnostics({
    onLine: (line) => appendLog(`Controller: ${line}`),
    onCapture: () => {
      diagnosticsStatus.textContent = "Controller warning/error detected; collecting diagnostic output...";
    },
    onReport: (report) => { saveDiagnosticReport(report, portInfo); }
  });
  readAbortController = new AbortController();
  readTask = readSerialOutput(selectedPort, readAbortController.signal, diagnostics)
    .catch((error) => appendLog(`Serial read failed: ${error.message}`));
  updateSerialView();
  appendLog(`Connected at ${BAUD_RATE} baud.`);
  await maybeAutoSend();
}

async function disconnectSerial() {
  readAbortController?.abort();
  readAbortController = null;
  await currentReader?.cancel().catch(() => {});
  await readTask;
  readTask = null;

  if (port) {
    await port.close();
    port = null;
  }

  updateSerialView();
  appendLog("Disconnected.");
}

async function toggleSerial() {
  try {
    if (port) {
      await disconnectSerial();
    } else {
      await connectSerial();
    }
  } catch (error) {
    appendLog(`Serial error: ${error.message}`);
    port = null;
    updateSerialView();
  }
}

function applyStarter(starter) {
  latestStarter = starter;
  updateStarterView();
  maybeAutoSend().catch((error) => appendLog(`Auto-send failed: ${error.message}`));
}

connectButton.addEventListener("click", toggleSerial);
sendButton.addEventListener("click", () => {
  sendCurrentStarter().catch((error) => appendLog(`Send failed: ${error.message}`));
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
