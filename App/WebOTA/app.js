"use strict";

const MAX_FIRMWARE_SIZE = 0x000F0000;
const STAGES = ["uploading", "downloading", "transferring", "installing"];
const STAGE_LABELS = {
  idle: "等待选择固件",
  queued: "升级任务已排队",
  uploading: "正在上传固件到服务器",
  downloading: "ESP-01S 正在通过 HTTPS 下载",
  preparing: "STM32 正在擦除 W25Q 暂存区",
  transferring: "ESP-01S 正在通过 UART 发送固件",
  verifying: "STM32 正在校验 W25Q 固件",
  installing: "Bootloader 正在安装固件",
  rebooting: "设备正在重启",
  completed: "升级完成",
  failed: "升级失败",
  cancelled: "升级已取消"
};

const elements = Object.fromEntries([
  "connectionBadge", "connectionDot", "connectionText", "connectButton", "apiLabel", "apiBase", "apiHint",
  "deviceIdField", "deviceId", "deviceStatus", "currentVersion", "wifiRssi", "lastSeen", "fileInput",
  "dropZone", "dropTitle", "dropSubtitle", "firmwareSize", "firmwareCrc", "totalPackets", "lastPacketSize",
  "firmwareSha", "firmwareVersion", "baudRate", "frameSize", "ackTimeout", "maxRetries", "startPacket",
  "progressPercent", "progressBar", "stageText", "byteProgress", "stageList", "startButton", "cancelButton",
  "logConsole", "clearLogButton", "toast"
].map((id) => [id, document.getElementById(id)]));

const state = {
  mode: "remote",
  file: null,
  arrayBuffer: null,
  crc32: null,
  sha256: null,
  xhr: null,
  taskId: null,
  pollTimer: null,
  busy: false,
  lastStatusSignature: ""
};

const crcTable = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n += 1) {
    let c = n;
    for (let k = 0; k < 8; k += 1) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    table[n] = c >>> 0;
  }
  return table;
})();

function calculateCrc32(buffer) {
  const bytes = new Uint8Array(buffer);
  let crc = 0xFFFFFFFF;
  for (const byte of bytes) crc = crcTable[(crc ^ byte) & 0xFF] ^ (crc >>> 8);
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

async function calculateSha256(buffer) {
  if (!window.crypto?.subtle) return null;
  const digest = await window.crypto.subtle.digest("SHA-256", buffer);
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function parseVersion() {
  const raw = elements.firmwareVersion.value.trim();
  if (!/^(0x[0-9a-f]+|\d+)$/i.test(raw)) throw new Error("固件版本必须是十进制或 0x 开头的十六进制整数");
  const value = Number.parseInt(raw, raw.toLowerCase().startsWith("0x") ? 16 : 10);
  if (!Number.isSafeInteger(value) || value < 0 || value > 0xFFFFFFFF) throw new Error("固件版本必须在 0 到 0xFFFFFFFF 之间");
  return value >>> 0;
}

function makeStartPacket() {
  const bytes = new Uint8Array(14);
  const view = new DataView(bytes.buffer);
  bytes[0] = 0xA5;
  bytes[1] = 0xA5;
  view.setUint32(2, parseVersion(), true);
  view.setUint32(6, state.file?.size ?? 0, true);
  view.setUint16(10, Number(elements.frameSize.value), true);
  bytes[12] = 0x5A;
  bytes[13] = 0x5A;
  return bytes;
}

function bytesToHex(bytes) {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0").toUpperCase()).join(" ");
}

function formatBytes(bytes) {
  if (!Number.isFinite(bytes) || bytes <= 0) return "0 B";
  const units = ["B", "KiB", "MiB"];
  const index = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1);
  const value = bytes / (1024 ** index);
  return `${value.toFixed(index === 0 ? 0 : 2)} ${units[index]}`;
}

function addLog(message, level = "info") {
  const row = document.createElement("div");
  row.className = `log-entry ${level}`;
  const time = document.createElement("span");
  time.className = "log-time";
  time.textContent = new Date().toLocaleTimeString("zh-CN", { hour12: false });
  const text = document.createElement("span");
  text.className = "log-message";
  text.textContent = message;
  row.append(time, text);
  elements.logConsole.append(row);
  elements.logConsole.scrollTop = elements.logConsole.scrollHeight;
}

let toastTimer;
function showToast(message, type = "info") {
  clearTimeout(toastTimer);
  elements.toast.textContent = message;
  elements.toast.className = `toast show ${type === "error" ? "error" : ""}`;
  toastTimer = setTimeout(() => { elements.toast.className = "toast"; }, 3200);
}

function normalizeBaseUrl() {
  const value = elements.apiBase.value.trim().replace(/\/$/, "");
  if (!value) throw new Error("请填写 API 地址");
  let url;
  try { url = new URL(value); } catch { throw new Error("API 地址格式不正确"); }
  if (state.mode === "remote" && url.protocol !== "https:" && !["localhost", "127.0.0.1"].includes(url.hostname)) {
    throw new Error("远程模式必须使用 HTTPS API");
  }
  return url.toString().replace(/\/$/, "");
}

function devicePath(suffix = "") {
  const base = normalizeBaseUrl();
  if (state.mode === "local") return `${base}/api${suffix}`;
  const id = elements.deviceId.value.trim();
  if (!id) throw new Error("请填写设备 ID");
  return `${base}/api/devices/${encodeURIComponent(id)}${suffix}`;
}

function setMode(mode) {
  state.mode = mode;
  document.querySelectorAll(".mode-option").forEach((button) => {
    const active = button.dataset.mode === mode;
    button.classList.toggle("active", active);
    button.setAttribute("aria-checked", String(active));
  });
  const remote = mode === "remote";
  elements.apiLabel.textContent = remote ? "HTTPS API 地址" : "ESP-01S 地址";
  elements.apiHint.textContent = remote
    ? "网页和 ESP-01S 都通过这个 HTTPS 服务交换升级任务。"
    : "浏览器直接访问 ESP-01S；只适用于同一局域网。";
  elements.deviceIdField.hidden = !remote;
  elements.startButton.textContent = remote ? "开始远程升级" : "开始局域网升级";
  const stored = localStorage.getItem(`otaApiBase:${mode}`);
  elements.apiBase.value = stored || (remote ? "https://ota.example.com" : "http://192.168.4.1");
  updateConnection(false, "尚未连接");
  addLog(`切换到${remote ? "远程服务器" : "局域网直连"}模式`);
}

function updateConnection(online, text) {
  elements.connectionDot.className = `status-dot ${online ? "online" : text.includes("失败") ? "error" : ""}`;
  elements.connectionText.textContent = text;
  elements.deviceStatus.textContent = online ? "在线" : "未知";
}

function updatePacketPreview() {
  try {
    elements.startPacket.textContent = bytesToHex(makeStartPacket());
    if (state.file) {
      const frameSize = Number(elements.frameSize.value);
      const total = Math.ceil(state.file.size / frameSize);
      const last = state.file.size % frameSize || frameSize;
      elements.totalPackets.textContent = `${total} 包`;
      elements.lastPacketSize.textContent = `${last} B`;
    }
  } catch (error) {
    elements.startPacket.textContent = error.message;
  }
  updateStartAvailability();
}

function updateStartAvailability() {
  let valid = Boolean(state.file && state.arrayBuffer && !state.busy);
  try {
    parseVersion();
    normalizeBaseUrl();
    if (state.mode === "remote" && !elements.deviceId.value.trim()) valid = false;
  } catch { valid = false; }
  elements.startButton.disabled = !valid;
}

async function analyzeFile(file) {
  if (!file) return;
  if (!file.name.toLowerCase().endsWith(".bin")) {
    showToast("请选择 .bin 固件文件", "error");
    return;
  }
  if (file.size === 0 || file.size > MAX_FIRMWARE_SIZE) {
    showToast(`固件必须大于 0 B 且不超过 ${formatBytes(MAX_FIRMWARE_SIZE)}`, "error");
    return;
  }

  state.file = file;
  state.arrayBuffer = null;
  state.crc32 = null;
  state.sha256 = null;
  elements.dropTitle.textContent = file.name;
  elements.dropSubtitle.textContent = "正在读取并校验固件…";
  elements.firmwareSize.textContent = `${formatBytes(file.size)} / 0x${file.size.toString(16).toUpperCase()}`;
  elements.firmwareCrc.textContent = "计算中…";
  elements.firmwareSha.textContent = "计算中…";
  updatePacketPreview();
  addLog(`已选择固件 ${file.name}，大小 ${file.size} bytes`);

  try {
    const buffer = await file.arrayBuffer();
    const crc = calculateCrc32(buffer);
    const sha = await calculateSha256(buffer);
    if (state.file !== file) return;
    state.arrayBuffer = buffer;
    state.crc32 = crc;
    state.sha256 = sha;
    elements.firmwareCrc.textContent = `0x${crc.toString(16).padStart(8, "0").toUpperCase()}`;
    elements.firmwareSha.textContent = sha || "当前浏览器环境不支持 SHA-256";
    elements.dropSubtitle.textContent = "固件校验完成；可以调整传输参数并开始升级";
    addLog(`CRC32 = 0x${crc.toString(16).padStart(8, "0").toUpperCase()}`, "success");
    if (sha) addLog(`SHA-256 = ${sha}`);
  } catch (error) {
    state.arrayBuffer = null;
    addLog(`固件读取失败：${error.message}`, "error");
    showToast("固件读取失败", "error");
  }
  updateStartAvailability();
}

async function testConnection() {
  let url;
  try { url = devicePath(""); } catch (error) {
    showToast(error.message, "error");
    return;
  }
  elements.connectButton.disabled = true;
  updateConnection(false, "正在连接…");
  addLog(`GET ${url}`);
  try {
    const response = await fetch(url, { credentials: "include", headers: { Accept: "application/json" } });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    updateConnection(data.online !== false, data.online === false ? "设备离线" : "设备在线");
    elements.deviceStatus.textContent = data.online === false ? "离线" : "在线";
    elements.currentVersion.textContent = data.firmwareVersion ?? data.version ?? "—";
    elements.wifiRssi.textContent = Number.isFinite(data.rssi) ? `${data.rssi} dBm` : "—";
    elements.lastSeen.textContent = data.lastSeen ? new Date(data.lastSeen).toLocaleString("zh-CN") : "刚刚";
    addLog(`设备连接成功，当前版本 ${elements.currentVersion.textContent}`, "success");
  } catch (error) {
    updateConnection(false, "连接失败");
    addLog(`设备连接失败：${error.message}`, "error");
    showToast("连接失败，请检查API地址和服务端CORS配置", "error");
  } finally {
    elements.connectButton.disabled = false;
  }
}

function setBusy(busy) {
  state.busy = busy;
  elements.startButton.disabled = busy;
  elements.cancelButton.disabled = !busy;
  elements.fileInput.disabled = busy;
  document.querySelectorAll(".mode-option").forEach((button) => { button.disabled = busy; });
  if (!busy) updateStartAvailability();
}

function renderProgress(stage, progress = 0, sentBytes = 0, totalBytes = state.file?.size ?? 0) {
  const safeProgress = Math.max(0, Math.min(100, Number(progress) || 0));
  elements.progressBar.style.width = `${safeProgress}%`;
  elements.progressPercent.textContent = `${Math.round(safeProgress)}%`;
  elements.stageText.textContent = STAGE_LABELS[stage] || stage || "处理中";
  elements.byteProgress.textContent = `${formatBytes(sentBytes)} / ${formatBytes(totalBytes)}`;

  const normalized = stage === "preparing" ? "transferring"
    : ["verifying", "rebooting", "completed"].includes(stage) ? "installing"
      : stage;
  const activeIndex = STAGES.indexOf(normalized);
  elements.stageList.querySelectorAll("li").forEach((item, index) => {
    item.classList.toggle("active", index === activeIndex && stage !== "completed");
    item.classList.toggle("done", stage === "completed" || (activeIndex >= 0 && index < activeIndex));
  });
}

function parseJson(text) {
  try { return text ? JSON.parse(text) : {}; } catch { return {}; }
}

function startUpgrade() {
  if (!state.file || !state.arrayBuffer || state.busy) return;
  let url;
  let version;
  try {
    url = devicePath("/ota");
    version = parseVersion();
  } catch (error) {
    showToast(error.message, "error");
    return;
  }

  const frameSize = Number(elements.frameSize.value);
  const form = new FormData();
  form.append("firmware", state.file, state.file.name);
  form.append("firmwareVersion", String(version));
  form.append("firmwareVersionHex", `0x${version.toString(16).padStart(8, "0").toUpperCase()}`);
  form.append("firmwareSize", String(state.file.size));
  form.append("crc32", `0x${state.crc32.toString(16).padStart(8, "0").toUpperCase()}`);
  form.append("sha256", state.sha256 || "");
  form.append("frameSize", String(frameSize));
  form.append("baudRate", elements.baudRate.value);
  form.append("ackTimeoutMs", String(Math.round(Number(elements.ackTimeout.value) * 1000)));
  form.append("maxRetries", elements.maxRetries.value);
  form.append("startPacketHex", bytesToHex(makeStartPacket()));

  localStorage.setItem(`otaApiBase:${state.mode}`, normalizeBaseUrl());
  localStorage.setItem("otaDeviceId", elements.deviceId.value.trim());
  state.taskId = null;
  state.lastStatusSignature = "";
  setBusy(true);
  renderProgress("uploading", 0, 0, state.file.size);
  addLog(`POST ${url}`);
  addLog(`起始包：${bytesToHex(makeStartPacket())}`);
  addLog(`传输参数：${elements.baudRate.value} baud, ${frameSize} bytes/包, ACK ${elements.ackTimeout.value}s, 重试 ${elements.maxRetries}次`);

  const xhr = new XMLHttpRequest();
  state.xhr = xhr;
  xhr.open("POST", url);
  xhr.withCredentials = true;
  xhr.setRequestHeader("Accept", "application/json");
  xhr.upload.onprogress = (event) => {
    if (!event.lengthComputable) return;
    const progress = (event.loaded / event.total) * 100;
    renderProgress("uploading", progress, Math.min(event.loaded, state.file.size), state.file.size);
  };
  xhr.onerror = () => finishWithError("上传失败：网络连接异常或服务端未允许CORS");
  xhr.onabort = () => {
    addLog("上传请求已中止", "warn");
    renderProgress("cancelled", 0, 0, state.file.size);
    setBusy(false);
  };
  xhr.onload = () => {
    const data = parseJson(xhr.responseText);
    if (xhr.status < 200 || xhr.status >= 300) {
      finishWithError(data.message || `服务器拒绝升级：HTTP ${xhr.status}`);
      return;
    }
    state.xhr = null;
    state.taskId = data.taskId || data.id || null;
    addLog(data.message || "固件上传完成，升级任务已创建", "success");
    if (data.stage === "completed" || data.completed === true) {
      finishSuccessfully(data.message || "升级完成");
      return;
    }
    renderProgress(data.stage || "queued", data.progress || 0, data.sentBytes || 0, data.totalBytes || state.file.size);
    pollStatus();
  };
  xhr.send(form);
}

async function pollStatus() {
  if (!state.busy) return;
  let url;
  try {
    url = devicePath("/ota/status");
    if (state.taskId) url += `?taskId=${encodeURIComponent(state.taskId)}`;
    const response = await fetch(url, { credentials: "include", cache: "no-store", headers: { Accept: "application/json" } });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    const stage = data.stage || "queued";
    renderProgress(stage, data.progress, data.sentBytes, data.totalBytes || state.file.size);
    const signature = `${stage}|${data.progress}|${data.message || ""}`;
    if (signature !== state.lastStatusSignature) {
      addLog(data.message || STAGE_LABELS[stage] || stage, stage === "failed" ? "error" : stage === "completed" ? "success" : "info");
      state.lastStatusSignature = signature;
    }
    if (stage === "completed" || data.completed === true) {
      finishSuccessfully(data.message || "升级完成");
      return;
    }
    if (["failed", "cancelled"].includes(stage)) {
      finishWithError(data.message || STAGE_LABELS[stage], stage === "cancelled");
      return;
    }
  } catch (error) {
    addLog(`状态查询暂时失败：${error.message}，稍后重试`, "warn");
  }
  state.pollTimer = window.setTimeout(pollStatus, 1200);
}

function finishSuccessfully(message) {
  clearTimeout(state.pollTimer);
  state.pollTimer = null;
  renderProgress("completed", 100, state.file?.size ?? 0, state.file?.size ?? 0);
  setBusy(false);
  addLog(message, "success");
  showToast(message);
  testConnection();
}

function finishWithError(message, cancelled = false) {
  clearTimeout(state.pollTimer);
  state.pollTimer = null;
  state.xhr = null;
  renderProgress(cancelled ? "cancelled" : "failed", 0, 0, state.file?.size ?? 0);
  setBusy(false);
  addLog(message, cancelled ? "warn" : "error");
  showToast(message, cancelled ? "info" : "error");
}

async function cancelUpgrade() {
  if (!state.busy) return;
  if (state.xhr) {
    state.xhr.abort();
    state.xhr = null;
  }
  clearTimeout(state.pollTimer);
  try {
    let url = devicePath("/ota");
    if (state.taskId) url += `?taskId=${encodeURIComponent(state.taskId)}`;
    await fetch(url, { method: "DELETE", credentials: "include", headers: { Accept: "application/json" } });
  } catch (error) {
    addLog(`取消请求发送失败：${error.message}`, "warn");
  }
  finishWithError("升级已由用户停止", true);
}

document.querySelectorAll(".mode-option").forEach((button) => button.addEventListener("click", () => setMode(button.dataset.mode)));
elements.connectButton.addEventListener("click", testConnection);
elements.dropZone.addEventListener("click", () => { if (!state.busy) elements.fileInput.click(); });
elements.fileInput.addEventListener("change", () => analyzeFile(elements.fileInput.files[0]));
elements.dropZone.addEventListener("dragover", (event) => { event.preventDefault(); elements.dropZone.classList.add("dragging"); });
elements.dropZone.addEventListener("dragleave", () => elements.dropZone.classList.remove("dragging"));
elements.dropZone.addEventListener("drop", (event) => {
  event.preventDefault();
  elements.dropZone.classList.remove("dragging");
  if (!state.busy) analyzeFile(event.dataTransfer.files[0]);
});
[elements.firmwareVersion, elements.frameSize].forEach((element) => element.addEventListener("input", updatePacketPreview));
[elements.apiBase, elements.deviceId].forEach((element) => element.addEventListener("input", updateStartAvailability));
elements.startButton.addEventListener("click", startUpgrade);
elements.cancelButton.addEventListener("click", cancelUpgrade);
elements.clearLogButton.addEventListener("click", () => { elements.logConsole.innerHTML = ""; addLog("日志已清空"); });

elements.deviceId.value = localStorage.getItem("otaDeviceId") || elements.deviceId.value;
setMode("remote");
updatePacketPreview();
renderProgress("idle", 0, 0, 0);
addLog("网页已就绪，选择固件后将自动计算CRC32和SHA-256");
