import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { setImmediate } from "node:timers/promises";
import test from "node:test";
import vm from "node:vm";

const extension = new URL("../../chrome-starter-serial-extension/", import.meta.url);
const contentSource = readFileSync(new URL("content.js", extension), "utf8");

test("failed content-script delivery retries the same starter instead of silently forgetting it", async () => {
  for (const failure of ["rejection", "negative response", "no response", "invalid context"]) {
    const messages = [];
    const warnings = [];
    let fail = true;
    const context = vm.createContext({
      console: { warn: (...args) => warnings.push(args) },
      document: { querySelector: () => null, querySelectorAll: () => [], documentElement: {} },
      window: { setInterval() {}, setTimeout() {} },
      MutationObserver: class { observe() {} },
      chrome: { runtime: { sendMessage(message) {
        messages.push(message);
        if (!fail) return Promise.resolve({ ok: true });
        if (failure === "invalid context") throw new Error("Extension context invalidated");
        if (failure === "rejection") return Promise.reject(new Error("Worker unavailable"));
        return Promise.resolve(failure === "negative response" ? { ok: false, error: "Storage failed" } : undefined);
      } } }
    });
    vm.runInContext(contentSource, context);
    vm.runInContext('parseCurrentStarter = () => ({ startNumber: "1", firstName: "Test", lastName: "Starter", dogName: "Dog" }); sendCurrentStarter();', context);
    await setImmediate();
    assert.equal(warnings.length, 1, failure);
    fail = false;
    vm.runInContext("sendCurrentStarter();", context);
    await setImmediate();
    vm.runInContext("sendCurrentStarter();", context);
    assert.equal(messages.length, 2, `${failure}: retry once, then deduplicate after success`);
  }
});

test("an older failed content message cannot clear a newer successful starter", async () => {
  let rejectFirst;
  let number = 1;
  const messages = [];
  const context = vm.createContext({
    console: { warn() {} },
    document: { querySelector: () => null, querySelectorAll: () => [], documentElement: {} },
    window: { setInterval() {}, setTimeout() {} },
    MutationObserver: class { observe() {} },
    currentNumber: () => number,
    chrome: { runtime: { sendMessage(message) {
      messages.push(message);
      if (messages.length === 1) return new Promise((resolve, reject) => { rejectFirst = reject; });
      return Promise.resolve({ ok: true });
    } } }
  });
  vm.runInContext(contentSource, context);
  vm.runInContext('parseCurrentStarter = () => ({ startNumber: currentNumber(), firstName: "Test", lastName: "Starter", dogName: "Dog" }); sendCurrentStarter();', context);
  number = 2;
  vm.runInContext("sendCurrentStarter();", context);
  await setImmediate();
  rejectFirst(new Error("Old delivery failed"));
  await setImmediate();
  vm.runInContext("sendCurrentStarter();", context);
  assert.equal(messages.length, 2);
});

test("background storage failures return a negative acknowledgement for the scraper to retry", async () => {
  let listener;
  const replies = [];
  const broadcasts = [];
  let fail = true;
  const context = vm.createContext({ chrome: {
    action: { onClicked: { addListener() {} }, setBadgeText() {}, setBadgeBackgroundColor() {} },
    runtime: {
      onMessage: { addListener(callback) { listener = callback; } },
      sendMessage(message) { broadcasts.push(message); return Promise.resolve(); }
    },
    storage: { local: { async set() { if (fail) throw new Error("Storage unavailable"); } } }
  } });
  vm.runInContext(readFileSync(new URL("background.js", extension), "utf8"), context);
  const message = { type: "STARTER_UPDATE", payload: { firstName: "Test", dogName: "Dog" } };
  const send = () => listener(message, { tab: { id: 1, url: "https://webmelden.de/" } }, (reply) => replies.push(reply));
  assert.equal(send(), true);
  await setImmediate();
  assert.equal(replies[0].ok, false);
  assert.equal(replies[0].error, "Storage unavailable");
  assert.equal(broadcasts.length, 0);
  fail = false;
  send();
  await setImmediate();
  assert.equal(replies[1].ok, true);
  assert.equal(broadcasts[0].type, "STARTER_CHANGED");
});
