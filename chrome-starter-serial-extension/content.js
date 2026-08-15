const SCAN_INTERVAL_MS = 1000;

let lastSentKey = "";
let scheduled = false;

function textOf(element) {
  return (element?.textContent ?? "").replace(/\s+/g, " ").trim();
}

function cleanDogName(value) {
  return value
    .replace(/\s+/g, " ")
    .trim()
    .replace(/^[`'"“”„]+/, "")
    .replace(/[`'"“”„]+$/, "")
    .trim();
}

function splitStarterName(value) {
  const parts = value.replace(/\s+/g, " ").trim().split(" ").filter(Boolean);

  if (parts.length === 0) {
    return { firstName: "", lastName: "" };
  }

  if (parts.length === 1) {
    return { firstName: parts[0], lastName: "" };
  }

  return {
    firstName: parts.slice(0, -1).join(" "),
    lastName: parts[parts.length - 1]
  };
}

function headerIndex(table, headerName, fallback) {
  const headerRow = table?.querySelector("tr.header");
  const headers = Array.from(headerRow?.cells ?? []).map((cell) => textOf(cell).toLowerCase());
  const index = headers.findIndex((header) => header.includes(headerName.toLowerCase()));
  return index >= 0 ? index : fallback;
}

function findCurrentStarterMarker() {
  return Array.from(document.querySelectorAll("p, h1, h2, h3, div"))
    .find((element) => /aktueller\s+starter/i.test(textOf(element)));
}

function findCurrentStarterRow() {
  const marker = findCurrentStarterMarker();

  if (marker) {
    for (let element = marker.nextElementSibling; element; element = element.nextElementSibling) {
      const markerText = textOf(element);

      if (/noch\s+\d+\s+starter/i.test(markerText)) {
        break;
      }

      if (element.matches?.("tr.akt")) {
        return element;
      }

      const row = element.querySelector?.("tr.akt");
      if (row) {
        return row;
      }
    }
  }

  return document.querySelector('form[name="ergebnisform"] tr.akt') ?? document.querySelector("tr.akt");
}

function parseCurrentStarter() {
  const row = findCurrentStarterRow();
  if (!row) {
    return null;
  }

  const table = row.closest("table");
  const starterIndex = headerIndex(table, "starter", 3);
  const dogIndex = headerIndex(table, "hund", 6);
  const startNumberIndex = headerIndex(table, "startnr", 0);

  const starterName = textOf(row.cells[starterIndex]);
  const dogName = cleanDogName(textOf(row.cells[dogIndex]));
  const startNumber = textOf(row.cells[startNumberIndex]);
  const { firstName, lastName } = splitStarterName(starterName);

  if (!starterName || !dogName) {
    return null;
  }

  return {
    source: "webmelden",
    startNumber,
    starterName,
    firstName,
    lastName,
    dogName,
    detectedAt: Date.now()
  };
}

function starterKey(starter) {
  return [
    starter.startNumber,
    starter.firstName,
    starter.lastName,
    starter.dogName
  ].join("|");
}

function sendCurrentStarter() {
  scheduled = false;

  const starter = parseCurrentStarter();
  if (!starter) {
    return;
  }

  const key = starterKey(starter);
  if (key === lastSentKey) {
    return;
  }

  lastSentKey = key;
  chrome.runtime.sendMessage({ type: "STARTER_UPDATE", payload: starter }).catch(() => {});
}

function scheduleScan() {
  if (scheduled) {
    return;
  }

  scheduled = true;
  window.setTimeout(sendCurrentStarter, 100);
}

sendCurrentStarter();
window.setInterval(sendCurrentStarter, SCAN_INTERVAL_MS);

new MutationObserver(scheduleScan).observe(document.documentElement, {
  childList: true,
  subtree: true,
  characterData: true
});
