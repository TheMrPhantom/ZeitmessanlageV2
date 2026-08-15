const BAUD_RATE = 115200;

let latestStarter = null;
let port = null;
let currentReader = null;
let lastSentKey = "";
let readAbortController = null;

const connectButton = document.getElementById("connectButton");
const sendButton = document.getElementById("sendButton");
const autoSend = document.getElementById("autoSend");
const starterName = document.getElementById("starterName");
const dogName = document.getElementById("dogName");
const serialLine = document.getElementById("serialLine");
const serialStatus = document.getElementById("serialStatus");
const pageStatus = document.getElementById("pageStatus");
const log = document.getElementById("log");

function appendLog(message) {
  const time = new Date().toLocaleTimeString();
  log.textContent = `[${time}] ${message}\n${log.textContent}`.slice(0, 5000);
}

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

async function readSerialOutput(signal) {
  const decoder = new TextDecoder();

  while (port?.readable && !signal.aborted) {
    const reader = port.readable.getReader();
    currentReader = reader;
    try {
      while (!signal.aborted) {
        const { value, done } = await reader.read();
        if (done) {
          break;
        }
        if (value) {
          appendLog(`Controller: ${decoder.decode(value).trim()}`);
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
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    appendLog("Web Serial is not available in this Chrome profile.");
    return;
  }

  port = await navigator.serial.requestPort();
  await port.open({ baudRate: BAUD_RATE });
  readAbortController = new AbortController();
  readSerialOutput(readAbortController.signal);
  updateSerialView();
  appendLog(`Connected at ${BAUD_RATE} baud.`);
  await maybeAutoSend();
}

async function disconnectSerial() {
  readAbortController?.abort();
  readAbortController = null;
  await currentReader?.cancel().catch(() => {});

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
