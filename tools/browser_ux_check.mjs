#!/usr/bin/env node
import { spawn } from "node:child_process";
import crypto from "node:crypto";
import fs from "node:fs/promises";
import http from "node:http";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { URL } from "node:url";

const DEFAULT_CHROME = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
const RANGE_BUTTONS = [
  ["ram", "realtime"],
  ["60", "1h"],
  ["360", "6h"],
  ["1440", "1d"],
  ["4320", "3d"],
  ["all", "all-minutes"],
];
const CANVAS_IDS = ["chartCo2", "chartVoc", "chartNox", "chartLux"];
const WARN_CLICK_MS = 500;
const FAIL_CLICK_MS = 2000;

function usage() {
  return `Usage: node tools/browser_ux_check.mjs --url http://192.168.124.67/ [--chrome <path>] [--port <port>]

Runs a dependency-free Chrome DevTools Protocol UX check against the dashboard.

Options:
  --url <url>       Dashboard URL to open. Required.
  --chrome <path>   Chrome executable path. Default: ${DEFAULT_CHROME}
  --port <port>     Remote debugging port. Default: auto.
  --help            Show this help.
`;
}

function parseArgs(argv) {
  const args = {
    chrome: DEFAULT_CHROME,
    port: 0,
    url: "",
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--help" || arg === "-h") {
      args.help = true;
    } else if (arg === "--url") {
      args.url = argv[++i] || "";
    } else if (arg === "--chrome") {
      args.chrome = argv[++i] || "";
    } else if (arg === "--port") {
      args.port = Number(argv[++i] || 0);
    } else {
      throw new Error(`Unknown argument: ${arg}`);
    }
  }
  if (!args.help && !args.url) {
    throw new Error("--url is required");
  }
  if (!args.help) {
    new URL(args.url);
  }
  if (args.port && (!Number.isInteger(args.port) || args.port < 1 || args.port > 65535)) {
    throw new Error("--port must be an integer from 1 to 65535");
  }
  return args;
}

async function getFreePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      const port = address && typeof address === "object" ? address.port : 0;
      server.close(() => resolve(port));
    });
  });
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function httpRequestJson(url, method = "GET") {
  return new Promise((resolve, reject) => {
    const req = http.request(url, { method }, (res) => {
      const chunks = [];
      res.on("data", (chunk) => chunks.push(chunk));
      res.on("end", () => {
        const body = Buffer.concat(chunks).toString("utf8");
        if (res.statusCode < 200 || res.statusCode >= 300) {
          reject(new Error(`${method} ${url} returned ${res.statusCode}: ${body.slice(0, 200)}`));
          return;
        }
        try {
          resolve(JSON.parse(body));
        } catch (error) {
          reject(new Error(`${method} ${url} returned invalid JSON: ${error.message}`));
        }
      });
    });
    req.once("error", reject);
    req.end();
  });
}

async function waitForDevTools(port, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  const versionUrl = `http://127.0.0.1:${port}/json/version`;
  let lastError = null;
  while (Date.now() < deadline) {
    try {
      return await httpRequestJson(versionUrl);
    } catch (error) {
      lastError = error;
      await delay(150);
    }
  }
  throw new Error(`Chrome DevTools did not become ready on port ${port}: ${lastError?.message || "timeout"}`);
}

async function createPageTarget(port) {
  const targetUrl = `http://127.0.0.1:${port}/json/new?${encodeURIComponent("about:blank")}`;
  try {
    return await httpRequestJson(targetUrl, "PUT");
  } catch {
    return await httpRequestJson(targetUrl, "GET");
  }
}

class CdpWebSocket {
  constructor(wsUrl) {
    this.wsUrl = new URL(wsUrl);
    this.socket = null;
    this.buffer = Buffer.alloc(0);
    this.nextId = 1;
    this.pending = new Map();
    this.eventHandlers = new Map();
    this.fragments = [];
  }

  async connect() {
    const key = crypto.randomBytes(16).toString("base64");
    const port = Number(this.wsUrl.port || 80);
    const host = this.wsUrl.hostname;
    const requestPath = `${this.wsUrl.pathname}${this.wsUrl.search}`;
    this.socket = net.createConnection({ host, port });
    await new Promise((resolve, reject) => {
      const onError = (error) => {
        cleanup();
        reject(error);
      };
      const onConnect = () => {
        this.socket.write(
          [
            `GET ${requestPath} HTTP/1.1`,
            `Host: ${host}:${port}`,
            "Upgrade: websocket",
            "Connection: Upgrade",
            `Sec-WebSocket-Key: ${key}`,
            "Sec-WebSocket-Version: 13",
            "",
            "",
          ].join("\r\n"),
        );
      };
      const onData = (chunk) => {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        const headerEnd = this.buffer.indexOf("\r\n\r\n");
        if (headerEnd === -1) return;
        const header = this.buffer.slice(0, headerEnd).toString("utf8");
        if (!header.startsWith("HTTP/1.1 101")) {
          cleanup();
          reject(new Error(`WebSocket upgrade failed: ${header.split("\r\n")[0]}`));
          return;
        }
        this.buffer = this.buffer.slice(headerEnd + 4);
        cleanup();
        this.socket.on("data", (data) => this.handleData(data));
        this.socket.on("close", () => this.rejectAll(new Error("CDP WebSocket closed")));
        this.socket.on("error", (error) => this.rejectAll(error));
        this.drainFrames();
        resolve();
      };
      const cleanup = () => {
        this.socket.off("error", onError);
        this.socket.off("connect", onConnect);
        this.socket.off("data", onData);
      };
      this.socket.once("error", onError);
      this.socket.once("connect", onConnect);
      this.socket.on("data", onData);
    });
  }

  on(method, handler) {
    if (!this.eventHandlers.has(method)) this.eventHandlers.set(method, []);
    this.eventHandlers.get(method).push(handler);
  }

  send(method, params = {}) {
    const id = this.nextId++;
    const message = JSON.stringify({ id, method, params });
    this.writeFrame(Buffer.from(message, "utf8"), 0x1);
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject, method });
    });
  }

  close() {
    if (!this.socket || this.socket.destroyed) return;
    try {
      this.writeFrame(Buffer.alloc(0), 0x8);
    } catch {
      // Ignore close errors during cleanup.
    }
    this.socket.destroy();
  }

  writeFrame(payload, opcode) {
    const length = payload.length;
    let headerLength = 2;
    if (length >= 126 && length <= 0xffff) headerLength += 2;
    else if (length > 0xffff) headerLength += 8;
    const mask = crypto.randomBytes(4);
    const frame = Buffer.alloc(headerLength + 4 + length);
    frame[0] = 0x80 | opcode;
    let offset = 2;
    if (length < 126) {
      frame[1] = 0x80 | length;
    } else if (length <= 0xffff) {
      frame[1] = 0x80 | 126;
      frame.writeUInt16BE(length, 2);
      offset = 4;
    } else {
      frame[1] = 0x80 | 127;
      frame.writeUInt32BE(0, 2);
      frame.writeUInt32BE(length, 6);
      offset = 10;
    }
    mask.copy(frame, offset);
    offset += 4;
    for (let i = 0; i < length; i += 1) {
      frame[offset + i] = payload[i] ^ mask[i % 4];
    }
    this.socket.write(frame);
  }

  handleData(data) {
    this.buffer = Buffer.concat([this.buffer, data]);
    this.drainFrames();
  }

  drainFrames() {
    while (this.buffer.length >= 2) {
      const first = this.buffer[0];
      const second = this.buffer[1];
      const opcode = first & 0x0f;
      let length = second & 0x7f;
      let offset = 2;
      if (length === 126) {
        if (this.buffer.length < 4) return;
        length = this.buffer.readUInt16BE(2);
        offset = 4;
      } else if (length === 127) {
        if (this.buffer.length < 10) return;
        const high = this.buffer.readUInt32BE(2);
        const low = this.buffer.readUInt32BE(6);
        if (high !== 0) throw new Error("CDP frame too large");
        length = low;
        offset = 10;
      }
      const masked = Boolean(second & 0x80);
      const maskOffset = offset;
      if (masked) offset += 4;
      if (this.buffer.length < offset + length) return;
      let payload = this.buffer.slice(offset, offset + length);
      if (masked) {
        const mask = this.buffer.slice(maskOffset, maskOffset + 4);
        payload = Buffer.from(payload.map((byte, i) => byte ^ mask[i % 4]));
      }
      this.buffer = this.buffer.slice(offset + length);

      if (opcode === 0x8) {
        this.close();
        return;
      }
      if (opcode === 0x9) {
        this.writeFrame(payload, 0xA);
        continue;
      }
      if (opcode === 0x1 || opcode === 0x0) {
        this.fragments.push(payload);
        if (first & 0x80) {
          const text = Buffer.concat(this.fragments).toString("utf8");
          this.fragments = [];
          this.handleMessage(text);
        }
      }
    }
  }

  handleMessage(text) {
    let message;
    try {
      message = JSON.parse(text);
    } catch {
      return;
    }
    if (message.id) {
      const pending = this.pending.get(message.id);
      if (!pending) return;
      this.pending.delete(message.id);
      if (message.error) pending.reject(new Error(`${pending.method}: ${message.error.message}`));
      else pending.resolve(message.result);
      return;
    }
    if (message.method) {
      const handlers = this.eventHandlers.get(message.method) || [];
      for (const handler of handlers) {
        try {
          handler(message.params || {});
        } catch {
          // Event handlers must not break the CDP socket reader.
        }
      }
    }
  }

  rejectAll(error) {
    for (const pending of this.pending.values()) {
      pending.reject(error);
    }
    this.pending.clear();
  }
}

async function waitForRuntime(client, expression, timeoutMs, label) {
  const deadline = Date.now() + timeoutMs;
  let lastValue = null;
  while (Date.now() < deadline) {
    const result = await client.send("Runtime.evaluate", {
      expression,
      awaitPromise: true,
      returnByValue: true,
    });
    if (result.exceptionDetails) {
      throw new Error(`${label} threw an exception`);
    }
    lastValue = result.result?.value;
    if (lastValue) return lastValue;
    await delay(200);
  }
  throw new Error(`Timed out waiting for ${label}; last value=${JSON.stringify(lastValue)}`);
}

async function evaluate(client, expression, label) {
  const result = await client.send("Runtime.evaluate", {
    expression,
    awaitPromise: true,
    returnByValue: true,
  });
  if (result.exceptionDetails) {
    const detail = result.exceptionDetails.exception?.description || result.exceptionDetails.text;
    throw new Error(`${label} failed: ${detail}`);
  }
  return result.result?.value;
}

function isAllHistoryRequest(rawUrl) {
  try {
    const requestUrl = new URL(rawUrl);
    return requestUrl.pathname === "/api/history" && requestUrl.searchParams.get("range") === "all";
  } catch {
    return rawUrl.includes("/api/history?range=all");
  }
}

async function run() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) {
    process.stdout.write(usage());
    return;
  }

  const chromePath = args.chrome;
  await fs.access(chromePath);
  const port = args.port || await getFreePort();
  const userDataDir = await fs.mkdtemp(path.join(os.tmpdir(), "esp32-browser-ux-"));
  const chromeArgs = [
    `--remote-debugging-port=${port}`,
    `--user-data-dir=${userDataDir}`,
    "--headless=new",
    "--disable-gpu",
    "--disable-extensions",
    "--disable-background-networking",
    "--disable-default-apps",
    "--no-proxy-server",
    "--proxy-bypass-list=*",
    "--no-first-run",
    "--no-default-browser-check",
    "--window-size=1280,900",
    "about:blank",
  ];

  const chrome = spawn(chromePath, chromeArgs, {
    stdio: ["ignore", "ignore", "pipe"],
    windowsHide: true,
  });
  let chromeStderr = "";
  chrome.stderr.on("data", (chunk) => {
    chromeStderr += chunk.toString("utf8");
    if (chromeStderr.length > 4000) chromeStderr = chromeStderr.slice(-4000);
  });

  let client = null;
  const startedAt = Date.now();
  try {
    await waitForDevTools(port);
    const target = await createPageTarget(port);
    if (!target.webSocketDebuggerUrl) {
      throw new Error("Chrome target did not expose webSocketDebuggerUrl");
    }
    client = new CdpWebSocket(target.webSocketDebuggerUrl);
    await client.connect();

    const allHistoryRequests = [];
    client.on("Network.requestWillBeSent", (params) => {
      const requestUrl = params.request?.url || "";
      if (isAllHistoryRequest(requestUrl)) {
        allHistoryRequests.push({ url: requestUrl, ts: Date.now() });
      }
    });

    await client.send("Runtime.enable");
    await client.send("Page.enable");
    await client.send("Network.enable");

    const preloadStart = Date.now();
    await client.send("Page.navigate", { url: args.url });
    await waitForRuntime(
      client,
      "document.readyState === 'complete' && !!document.querySelector('.rangeBtn[data-range=\"all\"]')",
      30000,
      "dashboard document load",
    );
    await waitForRuntime(
      client,
      "typeof minuteCache !== 'undefined' && minuteCache.loaded === true && !minuteCache.loading",
      90000,
      "dashboard minute history preload",
    );
    const preloadMs = Date.now() - preloadStart;

    const clickMetrics = [];
    for (const [range, label] of RANGE_BUTTONS) {
      const metric = await evaluate(
        client,
        `(${async function clickRange(targetRange) {
          const btn = document.querySelector(`.rangeBtn[data-range="${targetRange}"]`);
          if (!btn) throw new Error(`missing range button ${targetRange}`);
          const start = performance.now();
          btn.click();
          while (performance.now() - start < 2000) {
            await new Promise((resolve) => requestAnimationFrame(() => resolve()));
            if (btn.classList.contains("ok")) {
              await new Promise((resolve) => requestAnimationFrame(() => resolve()));
              break;
            }
          }
          return {
            range: targetRange,
            elapsedMs: performance.now() - start,
            active: btn.classList.contains("ok"),
            meta: document.getElementById("historyMeta")?.textContent || "",
          };
        }.toString()})(${JSON.stringify(range)})`,
        `click range ${range}`,
      );
      metric.label = label;
      clickMetrics.push(metric);
      if (!metric.active) {
        throw new Error(`range ${range} did not become active after click`);
      }
      if (metric.elapsedMs > FAIL_CLICK_MS) {
        throw new Error(`range ${range} click took ${metric.elapsedMs.toFixed(1)}ms`);
      }
    }

    const canvasMetrics = await evaluate(
      client,
      `(${function inspectCanvases(ids) {
        return ids.map((id) => {
          const canvas = document.getElementById(id);
          if (!canvas) return { id, ok: false, reason: "missing" };
          const ctx = canvas.getContext("2d", { willReadFrequently: true });
          if (!ctx) return { id, ok: false, reason: "no 2d context" };
          const width = canvas.width;
          const height = canvas.height;
          const data = ctx.getImageData(0, 0, width, height).data;
          const stridePixels = Math.max(1, Math.floor((width * height) / 6000));
          let nonZero = 0;
          let varied = 0;
          let first = null;
          for (let pixel = 0; pixel < width * height; pixel += stridePixels) {
            const i = pixel * 4;
            const rgba = `${data[i]},${data[i + 1]},${data[i + 2]},${data[i + 3]}`;
            if (data[i] || data[i + 1] || data[i + 2] || data[i + 3]) nonZero += 1;
            if (first === null) first = rgba;
            else if (rgba !== first) varied += 1;
          }
          return {
            id,
            ok: nonZero > 0 && varied > 0,
            width,
            height,
            nonZeroSamples: nonZero,
            variedSamples: varied,
          };
        });
      }.toString()})(${JSON.stringify(CANVAS_IDS)})`,
      "canvas getImageData check",
    );
    const badCanvas = canvasMetrics.find((item) => !item.ok);
    if (badCanvas) {
      throw new Error(`canvas ${badCanvas.id} is blank or unreadable: ${badCanvas.reason || JSON.stringify(badCanvas)}`);
    }

    if (allHistoryRequests.length > 1) {
      throw new Error(`/api/history?range=all requested ${allHistoryRequests.length} times; expected at most 1`);
    }

    console.log("[PASS] dashboard browser UX check");
    console.log(`[METRIC] url=${args.url}`);
    console.log(`[METRIC] chrome_start_ms=${Date.now() - startedAt}`);
    console.log(`[METRIC] preload_ms=${preloadMs}`);
    console.log(`[METRIC] all_history_requests=${allHistoryRequests.length}`);
    for (const metric of clickMetrics) {
      const line = `range=${metric.range} label=${metric.label} click_ms=${metric.elapsedMs.toFixed(1)}`;
      if (metric.elapsedMs > WARN_CLICK_MS) {
        console.log(`[WARN] ${line} exceeds ${WARN_CLICK_MS}ms`);
      } else {
        console.log(`[METRIC] ${line}`);
      }
    }
    for (const metric of canvasMetrics) {
      console.log(
        `[METRIC] canvas=${metric.id} size=${metric.width}x${metric.height} nonzero_samples=${metric.nonZeroSamples} varied_samples=${metric.variedSamples}`,
      );
    }
  } catch (error) {
    console.error(`[FAIL] ${error.message}`);
    if (chromeStderr.trim()) {
      console.error(`[CHROME STDERR] ${chromeStderr.trim().slice(-2000)}`);
    }
    process.exitCode = 1;
  } finally {
    if (client) client.close();
    if (!chrome.killed) chrome.kill();
    await delay(300);
    await fs.rm(userDataDir, { recursive: true, force: true });
  }
}

run().catch((error) => {
  console.error(`[FAIL] ${error.message}`);
  process.exitCode = 1;
});
