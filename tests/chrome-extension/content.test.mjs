import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { promisify } from "node:util";
import { pathToFileURL } from "node:url";
import test from "node:test";

const browser = [process.env.CHROME_PATH,
  "C:/Program Files/Google/Chrome/Application/chrome.exe",
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
  "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
].find((path) => path && existsSync(path));
const source = readFileSync(new URL("../../chrome-starter-serial-extension/content.js", import.meta.url), "utf8");
const fixture = readFileSync(new URL("fixtures/webmelden-live.html", import.meta.url), "utf8");

// Run the content script against a real browser DOM without extension installation.
function checkScraper(html) {
  const results = [];
  const messages = [];
  const timeouts = [];
  let poll;
  window.chrome = { runtime: { sendMessage(message) {
    messages.push(message);
    return Promise.resolve();
  } } };
  window.setInterval = (callback) => { poll = callback; };
  window.setTimeout = (callback) => { timeouts.push(callback); };

  function load(markup) {
    const parsed = new DOMParser().parseFromString(markup, "text/html");
    parsed.querySelectorAll("script, link, style, img").forEach((node) => node.remove());
    document.body.className = parsed.body.className;
    document.body.innerHTML = parsed.body.innerHTML;
  }
  function equal(actual, expected) {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
      throw new Error(`Expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    }
  }
  function check(name, callback) {
    try { callback(); results.push({ name }); }
    catch (error) { results.push({ name, error: error.message }); }
  }
  function fields(starter) {
    return starter && [starter.startNumber, starter.firstName, starter.lastName, starter.dogName];
  }

  load(html);
  // Evaluate as a global script, as Chrome does for content scripts.
  (0, eval)(window.scraperSource + "\nwindow.scraper = { parseCurrentStarter, sendCurrentStarter };");
  const { parseCurrentStarter, sendCurrentStarter } = window.scraper;
  check("anonymized live entry page reads the current starter", () => {
    equal(fields(parseCurrentStarter()), ["101", "Testa", "EXAMPLE", "DemoDogA"]);
    equal(messages[0].type, "STARTER_UPDATE");
    equal(messages[0].payload.source, "webmelden");
  });
  check("current form wins over earlier pending and last starter tables", () => {
    const current = document.querySelector("form.wm-live-results-current");
    const pending = document.querySelector(".wm-live-results-draggable").closest("form");
    pending.querySelector("tr.akt").cells[3].textContent = "Wrong STARTER";
    current.before(pending);
    equal(fields(parseCurrentStarter()), ["101", "Testa", "EXAMPLE", "DemoDogA"]);
  });
  check("current form works without row highlighting", () => {
    document.querySelector("form.wm-live-results-current tr.akt").className = "oddtr";
    equal(fields(parseCurrentStarter()), ["101", "Testa", "EXAMPLE", "DemoDogA"]);
  });
  check("empty current form does not send a pending starter", () => {
    document.querySelector("form.wm-live-results-current tr:not(.header)").remove();
    equal(parseCurrentStarter(), null);
    sendCurrentStarter();
    equal(messages.length, 1);
  });
  check("missing current form on live page does not send a pending starter", () => {
    document.querySelector("form.wm-live-results-current").remove();
    equal(parseCurrentStarter(), null);
  });
  check("columns follow headers and preserve Unicode names", () => {
    load(html);
    const table = document.querySelector("form.wm-live-results-current table");
    const header = table.querySelector("tr.header");
    const row = table.querySelector("tr.akt");
    row.cells[3].textContent = "Testc MÜSTER";
    row.cells[6].textContent = "  „DemoDogC“  ";
    header.prepend(header.cells[6]);
    row.prepend(row.cells[6]);
    equal(fields(parseCurrentStarter()), ["101", "Testc", "MÜSTER", "DemoDogC"]);
  });
  check("polling sends a changed starter once", () => {
    poll();
    poll();
    equal(messages.length, 2);
    equal(fields(messages[1].payload), ["101", "Testc", "MÜSTER", "DemoDogC"]);
  });
  check("legacy table layout remains supported", () => {
    load('<p>Aktueller Starter</p><form name="ergebnisform"><table>' +
      '<tr class="header"><th>StartNr</th><th>Starter</th><th>Hund</th></tr>' +
      '<tr class="akt"><td>104</td><td>Teste TEST</td><td>`DemoDogE`</td></tr>' +
      '</table></form>');
    equal(fields(parseCurrentStarter()), ["104", "Teste", "TEST", "DemoDogE"]);
  });

  // Allow the real MutationObserver to deliver the preceding DOM changes.
  return Promise.resolve().then(() => {
    while (timeouts.length) timeouts.shift()();
    load(html);
    return Promise.resolve();
  }).then(() => {
    while (timeouts.length) timeouts.shift()();
    const current = document.querySelector("form.wm-live-results-current");
    const before = messages.length;
    const next = current.querySelector("tr.akt").cloneNode(true);
    next.className = "oddtr";
    next.cells[0].textContent = "102";
    next.cells[3].textContent = "Testc MÜSTER";
    next.cells[6].textContent = "`DemoDogC`";
    current.querySelector("tbody").append(next);
    return Promise.resolve().then(() => {
      while (timeouts.length) timeouts.shift()();
      current.querySelector("tr.akt").className = "oddtr";
      next.className = "akt";
      return Promise.resolve();
    }).then(() => {
      check("class-only starter changes trigger a scan and one update", () => {
        equal(timeouts.length, 1);
        while (timeouts.length) timeouts.shift()();
        equal(messages.length, before + 1);
        equal(fields(messages.at(-1).payload), ["102", "Testc", "MÜSTER", "DemoDogC"]);
      });
      return results;
    });
  });
}

test("webmelden scraper browser regressions", { skip: !browser && "Set CHROME_PATH to a Chrome/Chromium executable" }, async (t) => {
  const directory = await mkdtemp(join(tmpdir(), "dogdog-scraper-test-"));
  try {
    const page = join(directory, "test.html");
    const literal = (value) => JSON.stringify(value).replaceAll("<", "\\u003c");
    await writeFile(page, `<!doctype html><meta charset="utf-8"><body><script>
      window.scraperSource = ${literal(source)};
      Promise.resolve().then(() => (${checkScraper.toString()})(${literal(fixture)}))
      .catch(error => [{ name: 'browser harness', error: error.stack }]).then(results => {
        const output = document.createElement('pre');
        output.id = 'scraper-test-results';
        output.textContent = JSON.stringify(results);
        document.body.append(output);
      });
    </script>`);
    const args = ["--headless=new", "--disable-gpu", "--disable-software-rasterizer",
      "--no-first-run", "--no-default-browser-check", "--disable-background-networking",
      `--user-data-dir=${join(directory, "profile")}`, "--dump-dom", pathToFileURL(page).href];
    if (process.env.CHROME_TEST_NO_SANDBOX === "1") args.unshift("--no-sandbox");
    const { stdout, stderr } = await promisify(execFile)(browser, args, { timeout: 30000, maxBuffer: 1024 * 1024 });
    const output = stdout.match(/<pre id="scraper-test-results">(.*?)<\/pre>/s);
    assert.ok(output, `Browser did not produce test results: ${stderr}\n${stdout.slice(-2000)}`);
    const results = JSON.parse(output[1].replaceAll("&quot;", '"').replaceAll("&lt;", "<").replaceAll("&gt;", ">").replaceAll("&amp;", "&"));
    assert.equal(results.length, 9, JSON.stringify(results));
    for (const result of results) {
      await t.test(result.name, () => assert.equal(result.error, undefined));
    }
  } finally {
    await rm(directory, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
  }
});
