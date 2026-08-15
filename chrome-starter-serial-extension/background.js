const BRIDGE_PAGE = "bridge.html";
const LATEST_KEY = "latestStarter";

async function findBridgeTab() {
  const bridgeUrl = chrome.runtime.getURL(BRIDGE_PAGE);
  const tabs = await chrome.tabs.query({});
  return tabs.find((tab) => tab.url === bridgeUrl);
}

async function openBridge() {
  const bridgeUrl = chrome.runtime.getURL(BRIDGE_PAGE);
  const existing = await findBridgeTab();

  if (existing) {
    await chrome.tabs.update(existing.id, { active: true });
    if (existing.windowId !== undefined) {
      await chrome.windows.update(existing.windowId, { focused: true });
    }
    return;
  }

  await chrome.tabs.create({ url: bridgeUrl, active: true });
}

chrome.action.onClicked.addListener(() => {
  openBridge();
});

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || typeof message.type !== "string") {
    return false;
  }

  if (message.type === "STARTER_UPDATE") {
    const payload = {
      ...message.payload,
      tabId: sender.tab?.id ?? null,
      pageUrl: sender.tab?.url ?? "",
      pageTitle: sender.tab?.title ?? "",
      receivedAt: Date.now()
    };

    chrome.storage.local.set({ [LATEST_KEY]: payload }).then(() => {
      chrome.action.setBadgeText({ text: "OK" });
      chrome.action.setBadgeBackgroundColor({ color: "#0077ff" });
      chrome.runtime.sendMessage({ type: "STARTER_CHANGED", payload }).catch(() => {});
      sendResponse({ ok: true });
    });
    return true;
  }

  if (message.type === "GET_LATEST_STARTER") {
    chrome.storage.local.get(LATEST_KEY).then((data) => {
      sendResponse({ ok: true, payload: data[LATEST_KEY] ?? null });
    });
    return true;
  }

  return false;
});
