// Web Serial reads arbitrary byte chunks, not complete log lines.
class ControllerDiagnostics {
  static IDLE_MS = 2000;
  static MAX_CAPTURE_MS = 15000;
  static MAX_LINE_CHARS = 16384;
  static MAX_REPORT_CHARS = 262144;
  static MAX_CONTEXT_CHARS = 16384;
  static MAX_CONTEXT_LINES = 50;

  constructor({ onLine, onReport, onCapture = () => {}, now = () => new Date(),
    setTimer = setTimeout, clearTimer = clearTimeout }) {
    this.onLine = onLine;
    this.onReport = onReport;
    this.onCapture = onCapture;
    this.now = now;
    this.setTimer = setTimer;
    this.clearTimer = clearTimer;
    this.decoder = new TextDecoder();
    this.buffer = "";
    this.context = [];
    this.contextChars = 0;
    this.capture = null;
    this.idleTimer = null;
    this.limitTimer = null;
  }

  static severity(line) {
    if (/^\s*W\s+\([^)]*\)\s*\S.*:/.test(line) || /^\s*(?:\[WARN(?:ING)?\]|WARN(?:ING)?\b)/i.test(line)) {
      return "warning";
    }
    if (/^\s*E\s+\([^)]*\)\s*\S.*:/.test(line) ||
        /^\s*(?:\[ERROR\]|ERROR\b|Guru Meditation Error\b|panic\b|assert failed\b|assertion\b.*failed|abort\(\) was called|ESP_ERROR_CHECK failed|Backtrace:|Stack dump detected|Brownout detector was triggered)/i.test(line)) {
      return "error";
    }
    return null;
  }

  push(bytes) {
    this.buffer += this.decoder.decode(bytes, { stream: true });
    this.drainLines();
    // A slowly transmitted, incomplete trace line also keeps the capture alive.
    if (this.capture || this.buffer) this.scheduleIdle();
  }

  drainLines() {
    while (this.buffer.length) {
      const separator = this.buffer.search(/[\r\n]/);
      if (separator < 0 && this.buffer.length < ControllerDiagnostics.MAX_LINE_CHARS) break;
      let length = separator < 0 ? ControllerDiagnostics.MAX_LINE_CHARS :
        Math.min(separator, ControllerDiagnostics.MAX_LINE_CHARS);
      // Do not split a UTF-16 surrogate pair when bounding a very long line.
      if (length === ControllerDiagnostics.MAX_LINE_CHARS &&
          /[\uD800-\uDBFF]/.test(this.buffer[length - 1]) &&
          /[\uDC00-\uDFFF]/.test(this.buffer[length])) length--;
      const line = this.buffer.slice(0, length);
      const terminated = separator === length;
      const delimiterLength = terminated && this.buffer.slice(length, length + 2) === "\r\n" ? 2 : 1;
      this.buffer = this.buffer.slice(length + (terminated ? delimiterLength : 0));
      this.acceptLine(line);
    }
  }

  acceptLine(rawLine) {
    // Remove ANSI colors before matching the ESP-IDF severity prefix and saving text.
    const line = rawLine.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "");
    if (!line.trim()) return;
    this.onLine(line);
    const severity = ControllerDiagnostics.severity(line);
    if (severity && !this.capture) {
      this.capture = {
        startedAt: this.now().toISOString(),
        severity,
        lines: [...this.context],
        chars: this.contextChars,
        warningCount: 0,
        errorCount: 0
      };
      this.limitTimer = this.setTimer(() => this.finish("capture time limit reached"), ControllerDiagnostics.MAX_CAPTURE_MS);
      this.onCapture();
    }
    if (this.capture) {
      if (severity === "warning") this.capture.warningCount++;
      if (severity === "error") {
        this.capture.errorCount++;
        this.capture.severity = "error";
      }
      this.capture.lines.push(line);
      this.capture.chars += line.length + 1;
      this.scheduleIdle();
      if (this.capture.chars >= ControllerDiagnostics.MAX_REPORT_CHARS) {
        this.flush("capture size limit reached");
      }
    }
    this.context.push(line);
    this.contextChars += line.length + 1;
    while (this.context.length > ControllerDiagnostics.MAX_CONTEXT_LINES ||
           this.contextChars > ControllerDiagnostics.MAX_CONTEXT_CHARS) {
      this.contextChars -= this.context.shift().length + 1;
    }
  }

  scheduleIdle() {
    this.clearTimer(this.idleTimer);
    this.idleTimer = this.setTimer(() => this.finish("serial output idle"), ControllerDiagnostics.IDLE_MS);
  }

  finish(reason) {
    // Preserve a final line even if the controller never sends a newline.
    const tail = this.buffer;
    this.buffer = "";
    if (tail) this.acceptLine(tail);
    this.flush(reason);
  }

  flush(reason) {
    this.clearTimer(this.idleTimer);
    this.clearTimer(this.limitTimer);
    this.idleTimer = this.limitTimer = null;
    if (!this.capture) return;
    const report = this.capture;
    this.capture = null;
    this.onReport({ ...report, endedAt: this.now().toISOString(), reason });
  }

  close(reason = "serial connection closed") {
    this.buffer += this.decoder.decode();
    this.drainLines();
    this.finish(reason);
  }
}
