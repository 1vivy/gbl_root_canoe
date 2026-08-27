import { exec as ksuExec, moduleInfo as ksuModuleInfo, toast } from "./kernelsu.js";

const IMAGE_NAMES = ["abl"];
const BOOT_MODES = new Set(["0", "1", "2"]);
const SUPPLIED_IMAGE_SPECS = [
  { path: "/data/local/tmp/canoe/abl.img", available: "suppliedAblAvailable", checkbox: "suppliedAblCheckbox", hint: "suppliedAblHint", missing: "suppliedAblMissing" },
  { path: "/data/local/tmp/canoe/vbmeta.img", available: "suppliedVbmetaAvailable", checkbox: "suppliedVbmetaCheckbox", hint: "suppliedVbmetaHint", missing: "suppliedVbmetaMissing" }
];

const state = {
  confirmStep: 0,
  pendingAction: null,
  moduleDir: "",
  scriptPath: "",
  status: null,
  pollTimer: null,
  pollInFlight: false,
  pollCount: 0,
  prevStatusRaw: "",
  prevLogRaw: "",
  taskStarted: false,
  activeTaskId: "",
  completionNotifiedTaskId: "",
  activeTaskKind: "",
  suppliedAblAvailable: false,
  suppliedVbmetaAvailable: false,
  lang: "zh"
};

const i18n = {
  zh: {
    pageTitle: "假回锁 - BL Flasher",
    ksuWebUI: "KernelSU Module WebUI",
    heroDesc: "自动识别当前活动槽位，若新版本存在GBL漏洞则跳过BL刷写；将BL镜像刷写到另一槽位，在persist的efisp目录安装修补后的ABL与匹配profile，并将BDS刷入efisp分区",
    slotStatus: "槽位状态",
    refresh: "刷新",
    currentSlot: "当前槽位",
    targetSlot: "目标槽位",
    imageCount: "镜像数量",
    taskStatus: "任务状态",
    entryMode: "当前启动项模式",
    saveMode: "保存",
    modeDefaulted: "当前启动项未显式设置，使用文件 fallback Mode 1",
    modeSaved: "启动项模式已保存",
    modeUnavailable: "无法读取 canoe.cfg 启动项模式",
    mode2ProfileMissing: "Mode 2 profile 缺失；请先安装有效的 boot.efi.gm2p",
    mode2ProfileInvalid: "Mode 2 profile 无效；请重新安装匹配的 boot.efi.gm2p",
    modeProfileToolMissing: "mode2_profile 工具缺失；请重新安装模块",
    flash: "刷写到另一槽位",
    bdsTools: "仅更新BDS和Tools",
    patchPart: "修补分区",
    confirmBdsTools: "确认更新 BDS/Tools",
    confirmPatchPart: "确认修补分区",
    modalBdsStep1: "将把 BDS.efi 刷入 efisp 分区，并用模块自带的 efisp 文件夹（tools）替换 persist 上的启动根目录。不会改动 ABL 与 boot.efi。",
    modalBdsStep2: "第二次确认: 这是高风险写入操作，错误的 BDS 或 efisp 写入可能导致无法进入启动菜单。确认后将立即开始。",
    modalPatchStep1: "将对当前活动槽位执行勾选的修补操作。调试模式下不会执行实际修补。",
    modalPatchStep2: "第二次确认：修补分区属于高风险操作，错误会导致系统无法启动，确认后立即执行。",
    toastStartBdsTools: "BDS/Tools 更新任务已启动",
    toastBdsToolsDone: "BDS/Tools 更新任务运行成功",
    clearLog: "清空日志",
    updateEfisp: "更新 efisp（默认开启）",
    debugMode: "调试模式（仅处理不刷写，efisp 目录使用模块 tmp/efisp）",
    lblPatchVendorBoot: "修补 vendor_boot 分区",
    patchVendorBootHint: "Mode 2 已取代原有的分区修补绕过方案。",
    lblSuppliedAbl: "使用提供的 abl.img",
    lblSuppliedVbmeta: "使用提供的 vbmeta.img",
    suppliedAblMissing: "未找到非空文件 /data/local/tmp/canoe/abl.img",
    suppliedVbmetaMissing: "未找到非空文件 /data/local/tmp/canoe/vbmeta.img",
    warning: "刷写对象是 bootloader 相关分区，风险较高。开始前请确认镜像与机型严格匹配。",
    imageMap: "镜像映射",
    partition: "分区名",
    source: "源分区 (当前槽位)",
    target: "目标分区",
    action: "操作",
    waiting: "等待读取模块状态",
    log: "实时日志",
    autoPoll: "自动轮询最近 200 行",
    risk: "高风险操作",
    confirmFlash: "确认操作",
    cancel: "取消",
    continue: "继续",
    waitingStatus: "等待检测",
    slotUnknown: "槽位未知",
    logWaiting: "等待日志输出...",
    toastRunning: "已有任务在运行",
    toastStartDebug: "调试任务已启动",
    toastStartFlash: "刷写任务已启动",
    toastStartPatch: "分区修补任务已启动",
    toastDebugDone: "调试任务运行成功",
    toastFlashDone: "刷写任务运行成功",
    toastPatchDone: "分区修补任务运行成功",
    toastStartMode: "启动项模式保存任务已启动",
    toastModeDone: "启动项模式已保存",
    toastBlDone: "BL 刷写完成，但 efisp 未更新",
    toastFailed: "任务执行失败",
    toastStartError: "任务启动失败",
    statusIdle: "状态: idle",
    statusRunning: "状态: running",
    statusSuccess: "状态: success",
    statusWarning: "状态: warning",
    statusError: "状态: error",
    toastLogBusy: "任务运行中，暂时不能清空日志",
    toastLogCleared: "日志已清空",
    modalStep1Debug: "调试模式：将执行所有处理流程但不刷写分区，生成的文件保存在 tmp 目录。",
    modalStep1Normal: (slot) => `第一次确认: 将把当前槽位的 BL 分区拷贝到槽位 ${slot}`,
    modalStep1PatchOnly: (slot, name) => `第一次确认: 仅对目标槽位 ${slot} 执行 ${name} 修补，不刷写 ABL、不更新 efisp。`,
    modalStep2: "第二次确认: 这是高风险写入操作，错误操作可能导致目标槽位无法启动。确认后将立即开始。",
    withEfisp: "，并更新 efisp。",
    noEfisp: "，不更新 efisp。",
    withPatch: (name) => `，同步对目标槽位执行 ${name} 修补`,
    confirmSlot: "请确认槽位无误。",
    taskRunning:"任务运行中",
    waitOperate:"等待操作",
    copyPart:"分区拷贝",
    statusReadFail:"状态读取失败",
    startFail:"启动失败"
  },
  en: {
    pageTitle: "Fake Lock - BL Flasher",
    ksuWebUI: "KernelSU Module WebUI",
    heroDesc: "Auto-detect the active slot. Skip BL flashing when the new build has the GBL exploit, copy BL images to the inactive slot, install the patched ABL with its matching profile under persist/efisp, and flash BDS to efisp.",
    slotStatus: "Slot Status",
    refresh: "Refresh",
    currentSlot: "Current Slot",
    targetSlot: "Target Slot",
    imageCount: "Image Count",
    taskStatus: "Task Status",
    entryMode: "Current Boot Entry Mode",
    saveMode: "Save",
    modeDefaulted: "This entry has no explicit mode; using the file fallback Mode 1",
    modeSaved: "Boot entry mode saved",
    modeUnavailable: "Unable to read the canoe.cfg boot-entry mode",
    mode2ProfileMissing: "Mode 2 profile is missing; install a valid boot.efi.gm2p first",
    mode2ProfileInvalid: "Mode 2 profile is invalid; reinstall the matching boot.efi.gm2p",
    modeProfileToolMissing: "mode2_profile is missing; reinstall the module",
    flash: "Flash To Other Slot",
    bdsTools: "Update BDS & Tools Only",
    patchPart: "Patch Partitions",
    confirmBdsTools: "Confirm BDS/Tools Update",
    confirmPatchPart: "Confirm Partition Patch",
    modalBdsStep1: "Will flash BDS.efi to the efisp partition and replace the persist boot root with the bundled efisp folder (tools). The ABL and boot.efi are not touched.",
    modalBdsStep2: "2nd Confirm: This is a high-risk write. A wrong BDS or efisp write may prevent the boot menu from loading. It starts immediately after confirm.",
    modalPatchStep1: "Will patch the active slot with selected options. No actual patch in debug mode.",
    modalPatchStep2: "2nd Confirm: Partition patching is high-risk, bad patch may cause boot failure. Start immediately after confirm.",
    toastStartBdsTools: "BDS/Tools update started",
    toastBdsToolsDone: "BDS/Tools update task succeeded",
    clearLog: "Clear Log",
    updateEfisp: "Update efisp (on by default)",
    debugMode: "Debug Mode (process only, no flash; efisp dir uses module tmp/efisp)",
    lblPatchVendorBoot: "Patch vendor_boot partition",
    patchVendorBootHint: "Mode 2 supersedes the former partition workaround.",
    lblSuppliedAbl: "Use supplied abl.img",
    lblSuppliedVbmeta: "Use supplied vbmeta.img",
    suppliedAblMissing: "Non-empty /data/local/tmp/canoe/abl.img was not found",
    suppliedVbmetaMissing: "Non-empty /data/local/tmp/canoe/vbmeta.img was not found",
    warning: "Flashing bootloader partitions is high risk. Verify images match your device before starting.",
    imageMap: "Image Mapping",
    partition: "Partition",
    source: "Source (Current)",
    target: "Target",
    action: "Action",
    waiting: "Waiting for module status",
    log: "Live Log",
    autoPoll: "Auto poll last 200 lines",
    risk: "HIGH RISK",
    confirmFlash: "Confirm Action",
    cancel: "Cancel",
    continue: "Continue",
    waitingStatus: "Waiting",
    slotUnknown: "Slot Unknown",
    logWaiting: "Waiting for log...",
    toastRunning: "Task is already running",
    toastStartDebug: "Debug task started",
    toastStartFlash: "Flash task started",
    toastStartPatch: "Partition patch task started",
    toastDebugDone: "Debug task succeeded",
    toastFlashDone: "Flash task succeeded",
    toastPatchDone: "Partition patch task succeeded",
    toastStartMode: "Boot-entry mode save started",
    toastModeDone: "Boot-entry mode saved",
    toastBlDone: "BL flashed, but efisp not updated",
    toastFailed: "Task finished (failed)",
    statusIdle: "Status: idle",
    statusRunning: "Status: running",
    statusSuccess: "Status: success",
    statusWarning: "Status: warning",
    statusError: "Status: error",
    toastStartError: "Failed to start task",
    toastLogBusy: "Cannot clear log while task is running",
    toastLogCleared: "Log cleared",
    modalStep1Debug: "Debug Mode: All processes run without flashing partitions. Files saved to tmp directory.",
    modalStep1Normal: (slot) => `1st Confirm: Copy BL partition from current slot to ${slot}`,
    modalStep1PatchOnly: (slot, name) => `1st Confirm: Patch ${name} on target slot ${slot} only. No ABL flash, no efisp update.`,
    modalStep2: "2nd Confirm: This is a high-risk write operation. Wrong action may brick the target slot. Flash will start immediately after confirm.",
    withEfisp: ", and update efisp.",
    noEfisp: ", efisp not updated.",
    withPatch: (name) => `, apply ${name} patch to target slot`,
    confirmSlot: "Please confirm slot is correct.",
    taskRunning:"Task Running",
    waitOperate:"Waiting",
    copyPart:"Copy",
    statusReadFail:"Status Read Failed",
    startFail:"Start Failed"
  }
};

const elements = {
  stateChip: document.getElementById("stateChip"),
  slotChip: document.getElementById("slotChip"),
  currentSlot: document.getElementById("currentSlot"),
  targetSlot: document.getElementById("targetSlot"),
  imageCount: document.getElementById("imageCount"),
  taskMessage: document.getElementById("taskMessage"),
  updatedAt: document.getElementById("updatedAt"),
  imageTableBody: document.getElementById("imageTableBody"),
  logOutput: document.getElementById("logOutput"),
  flashButton: document.getElementById("flashButton"),
  bdsToolsButton: document.getElementById("bdsToolsButton"),
  patchPartButton: document.getElementById("patchPartButton"),
  clearLogButton: document.getElementById("clearLogButton"),
  refreshButton: document.getElementById("refreshButton"),
  preferredModeSelect: document.getElementById("preferredModeSelect"),
  saveModeButton: document.getElementById("saveModeButton"),
  modeStatusText: document.getElementById("modeStatusText"),
  modeControl: document.querySelector(".mode-control"),
  confirmModal: document.getElementById("confirmModal"),
  confirmText: document.getElementById("confirmText"),
  nextConfirmButton: document.getElementById("nextConfirmButton"),
  cancelConfirmButton: document.getElementById("cancelConfirmButton"),
  updateEfispCheckbox: document.getElementById("updateEfispCheckbox"),
  debugModeCheckbox: document.getElementById("debugModeCheckbox"),
  patchVendorBootCheckbox: document.getElementById("patchVendorBootCheckbox"),
  suppliedAblCheckbox: document.getElementById("suppliedAblCheckbox"),
  suppliedVbmetaCheckbox: document.getElementById("suppliedVbmetaCheckbox"),
  suppliedAblHint: document.getElementById("suppliedAblHint"),
  suppliedVbmetaHint: document.getElementById("suppliedVbmetaHint"),
  pageTitle: document.getElementById("pageTitle")
};

function applyLanguage(lang) {
  state.lang = lang;
  const t = i18n[lang];
  document.documentElement.lang = lang === "zh" ? "zh-CN" : "en";
  elements.pageTitle.textContent = t.pageTitle;
  document.querySelector("#lblKsuWebUI").textContent = t.ksuWebUI;
  document.querySelector(".hero-copy").textContent = t.heroDesc;
  document.querySelector("#lblSlotStatus").textContent = t.slotStatus;
  document.querySelector("#lblCurrentSlot").textContent = t.currentSlot;
  document.querySelector("#lblTargetSlot").textContent = t.targetSlot;
  document.querySelector("#lblImageCount").textContent = t.imageCount;
  document.querySelector("#lblTaskStatus").textContent = t.taskStatus;
  document.querySelector("#lblPreferredMode").textContent = t.entryMode;
  document.querySelector("#lblUpdateEfisp").textContent = t.updateEfisp;
  document.querySelector("#lblDebugMode").textContent = t.debugMode;
  document.querySelector("#lblPatchVendorBoot").textContent = t.lblPatchVendorBoot;
  document.querySelector("#lblPatchVendorBootHint").textContent = t.patchVendorBootHint;
  document.querySelector("#lblSuppliedAbl").textContent = t.lblSuppliedAbl;
  document.querySelector("#lblSuppliedVbmeta").textContent = t.lblSuppliedVbmeta;
  if (!state.suppliedAblAvailable) elements.suppliedAblHint.textContent = t.suppliedAblMissing;
  if (!state.suppliedVbmetaAvailable) elements.suppliedVbmetaHint.textContent = t.suppliedVbmetaMissing;
  document.querySelector("#lblWarning").textContent = t.warning;
  document.querySelector("#lblImageMap").textContent = t.imageMap;
  document.querySelector("#tblPartition").textContent = t.partition;
  document.querySelector("#tblSource").textContent = t.source;
  document.querySelector("#tblTarget").textContent = t.target;
  document.querySelector("#tblAction").textContent = t.action;
  const waitingCell = document.querySelector("#tblWaiting");
  if (waitingCell) waitingCell.textContent = t.waiting;
  document.querySelector("#lblLog").textContent = t.log;
  document.querySelector("#lblAutoPoll").textContent = t.autoPoll;
  document.querySelector("#modalRisk").textContent = t.risk;
  document.querySelector("#modalTitle").textContent = t.confirmFlash;
  if (elements.logOutput.textContent === "等待日志输出..." || elements.logOutput.textContent === "Waiting for log...") {
    elements.logOutput.textContent = t.logWaiting;
  }
  if (elements.stateChip.textContent === "等待检测" || elements.stateChip.textContent === "Waiting") {
    elements.stateChip.textContent = t.waitingStatus;
  }
  document.querySelectorAll("[data-i18n]").forEach(el => {
    const key = el.dataset.i18n;
    if (t[key]) el.textContent = t[key];
  });
}

function shellQuote(value) {
  return `'${String(value).replace(/'/g, `'\\''`)}'`;
}

function moduleInfo() {
  const raw = ksuModuleInfo();
  return typeof raw === "string" ? JSON.parse(raw) : raw;
}

async function runScript(action, arg) {
  const parts = [`MODDIR=${shellQuote(state.moduleDir)}`, "sh", shellQuote(state.scriptPath), action];
  if (arg) parts.push(shellQuote(arg));
  const command = parts.join(" ");
  const timeout = new Promise((_, reject) => {
    setTimeout(() => reject(new Error("KernelSU exec timeout")), 8000);
  });
  const { errno, stdout, stderr } = await Promise.race([ksuExec(command), timeout]);
  if (errno !== 0) {
    const error = new Error(stderr || `Command failed: ${errno}`);
    error.commandOutput = stdout || "";
    throw error;
  }
  return stdout || "";
}
function parseKeyValueOutput(output) {
  const info = {};
  for (const line of output.split(/[\r\n|]+/)) {
    if (!line) continue;
    const eq = line.indexOf("=");
    if (eq > 0) info[line.slice(0, eq)] = line.slice(eq + 1);
  }
  return info;
}

async function refreshSuppliedImages() {
  await Promise.all(SUPPLIED_IMAGE_SPECS.map(async spec => {
    let available = false;
    try {
      const result = await ksuExec(`test -s ${shellQuote(spec.path)}`);
      available = result.errno === 0;
    } catch {}
    state[spec.available] = available;
    const checkbox = elements[spec.checkbox];
    if (!available) checkbox.checked = false;
    checkbox.disabled = !available;
    const hint = elements[spec.hint];
    hint.hidden = available;
    if (!available) hint.textContent = i18n[state.lang][spec.missing];
  }));
}

function escapeHtml(str) {
  return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function renderTable(currentSlot, targetSlot) {
  const t = i18n[state.lang];
  if (currentSlot === "-" || targetSlot === "-") {
    elements.imageTableBody.innerHTML = `<tr><td colspan="4" class="empty-row">${t.waiting}</td></tr>`;
    return;
  }
  elements.imageTableBody.innerHTML = IMAGE_NAMES.map(name => {
    const src = `/dev/block/by-name/${name}${currentSlot}`;
    const dst = `/dev/block/by-name/${name}${targetSlot}`;
    return `<tr><td>${escapeHtml(name)}</td><td class="caption">${escapeHtml(src)}</td><td>${escapeHtml(dst)}</td><td><span class="status-pill ok">${t.copyPart}</span></td></tr>`;
  }).join("");
}

function setControlsUnavailable(message) {
  state.status = null;
  closeConfirmModal();
  elements.stateChip.textContent = i18n[state.lang].statusReadFail;
  elements.stateChip.className = "chip chip-danger";
  elements.taskMessage.textContent = message;
  elements.flashButton.disabled = true;
  elements.bdsToolsButton.disabled = true;
  elements.patchPartButton.disabled = true;
  elements.clearLogButton.disabled = true;
  elements.preferredModeSelect.disabled = true;
  elements.saveModeButton.disabled = true;
  elements.updateEfispCheckbox.disabled = true;
  elements.debugModeCheckbox.disabled = true;
  elements.patchVendorBootCheckbox.disabled = true;
  elements.suppliedAblCheckbox.disabled = true;
  elements.suppliedVbmetaCheckbox.disabled = true;
  elements.modeControl.setAttribute("aria-busy", "true");
  elements.modeStatusText.textContent = i18n[state.lang].modeUnavailable;
  elements.refreshButton.disabled = false;
}
function setTaskControlsBusy() {
  elements.flashButton.disabled = true;
  elements.bdsToolsButton.disabled = true;
  elements.patchPartButton.disabled = true;
  elements.clearLogButton.disabled = true;
  elements.preferredModeSelect.disabled = true;
  elements.saveModeButton.disabled = true;
  elements.updateEfispCheckbox.disabled = true;
  elements.debugModeCheckbox.disabled = true;
  elements.patchVendorBootCheckbox.disabled = true;
  elements.suppliedAblCheckbox.disabled = true;
  elements.suppliedVbmetaCheckbox.disabled = true;
  elements.modeControl.setAttribute("aria-busy", "true");
}

function renderModeStartError(error) {
  const t = i18n[state.lang];
  const out = parseKeyValueOutput(error.commandOutput || "");
  const messages = {
    MODE2_PROFILE_MISSING: t.mode2ProfileMissing,
    MODE2_PROFILE_INVALID: t.mode2ProfileInvalid,
    MODE_PROFILE_TOOL_MISSING: t.modeProfileToolMissing
  };
  const message = messages[out.ERROR_CODE] || out.ERROR || error.message;
  elements.modeStatusText.textContent = message;
  elements.taskMessage.textContent = message;
  toast(message);
}


function renderStatus(status) {
  state.status = status;
  const t = i18n[state.lang];
  const cur = status.CURRENT_SLOT || "-";
  const tar = status.TARGET_SLOT || "-";
  const run = status.RUNNING === "1";
  const st = status.STATE || "idle";
  const visibleState = run ? "running" : st;
  const msg = status.MESSAGE || (visibleState === "idle" ? "idle" : t.waitOperate);
  const statusLabels = {
    idle: t.statusIdle,
    running: t.statusRunning,
    success: t.statusSuccess,
    warning: t.statusWarning,
    error: t.statusError
  };
  elements.currentSlot.textContent = cur;
  elements.targetSlot.textContent = tar;
  elements.imageCount.textContent = IMAGE_NAMES.length;
  elements.taskMessage.textContent = msg;
  elements.updatedAt.textContent = status.UPDATED_AT || "-";
  elements.stateChip.textContent = statusLabels[visibleState] || `${state.lang === "zh" ? "状态" : "Status"}: ${visibleState}`;
  elements.stateChip.className = "chip";
  if (st === "success") elements.stateChip.classList.add("chip-success");
  else if (st === "error") elements.stateChip.classList.add("chip-danger");
  else if (st === "warning" || run) elements.stateChip.classList.add("chip-warn");
  elements.slotChip.textContent = (cur !== "-" && tar !== "-") ? `${state.lang === "zh" ? "当前" : "Current"} ${cur} → ${state.lang === "zh" ? "目标" : "Target"} ${tar}` : t.slotUnknown;
  const entryMode = status.ENTRY_MODE || "";
  const modeAvailable = BOOT_MODES.has(entryMode);
  const entryId = status.ENTRY_ID || (cur === "_b" ? "android-b" : "android-a");
  document.querySelector("#lblPreferredMode").textContent = `${t.entryMode}: ${entryId}`;
  if (modeAvailable) {
    elements.preferredModeSelect.value = entryMode;
    const selectedLabel = elements.preferredModeSelect.selectedOptions[0].textContent;
    elements.modeStatusText.textContent = status.ENTRY_MODE_DEFAULTED === "1" ?
      t.modeDefaulted : `${t.modeSaved}: ${selectedLabel}`;
  } else {
    elements.modeStatusText.textContent = t.modeUnavailable;
  }
  elements.modeControl.setAttribute("aria-busy", run ? "true" : "false");
  elements.flashButton.disabled = run || cur === "-" || tar === "-";
  elements.bdsToolsButton.disabled = run;
  elements.patchPartButton.disabled = run;
  elements.clearLogButton.disabled = run;
  elements.preferredModeSelect.disabled = run || !modeAvailable;
  elements.saveModeButton.disabled = run || !modeAvailable;
  elements.updateEfispCheckbox.disabled = run;
  elements.debugModeCheckbox.disabled = run;
  elements.patchVendorBootCheckbox.disabled = run;
  elements.suppliedAblCheckbox.disabled = run || !state.suppliedAblAvailable;
  elements.suppliedVbmetaCheckbox.disabled = run || !state.suppliedVbmetaAvailable;
  renderTable(cur, tar);
}
function notifyTaskFinished(stateStr, taskKind = "") {
  const t = i18n[state.lang];
  const msg = state.status?.MESSAGE || "";
  const normalized = msg.toLowerCase();
  if (stateStr === "success") {
    if (taskKind === "mode") toast(t.toastModeDone);
    else if (msg.includes("修补") || normalized.includes("patch")) toast(t.toastPatchDone);
    else if (msg.includes("调试") || normalized.includes("debug")) toast(t.toastDebugDone);
    else if (normalized.includes("bds")) toast(t.toastBdsToolsDone);
    else toast(t.toastFlashDone);
  } else if (stateStr === "warning") toast(t.toastBlDone);
  else if (stateStr === "error") toast(t.toastFailed);
}
function rememberPendingTask(taskId, taskKind = "") {
  if (!taskId) return;
  state.taskStarted = true;
  state.activeTaskId = taskId;
  state.activeTaskKind = taskKind;
  try {
    localStorage.setItem("blFlasherPendingTaskId", taskId);
    localStorage.setItem("blFlasherPendingTaskKind", taskKind);
  } catch {}
}

function clearPendingTask() {
  state.taskStarted = false;
  state.activeTaskId = "";
  state.activeTaskKind = "";
  try {
    localStorage.removeItem("blFlasherPendingTaskId");
    localStorage.removeItem("blFlasherPendingTaskKind");
  } catch {}
}

function applyStatus(s, notify = true) {
  if (!s?.STATE) return s;
  const taskId = s.TASK_ID || "";
  renderStatus(s);
  const isRunning = s.RUNNING === "1";
  const terminalState = ["success", "warning", "error"].includes(s.STATE);
  const pendingMatches = state.activeTaskId && taskId === state.activeTaskId;
  if (!isRunning && terminalState && pendingMatches) {
    const finishedTaskKind = state.activeTaskKind;
    clearPendingTask();
    if (notify && taskId !== state.completionNotifiedTaskId) {
      state.completionNotifiedTaskId = taskId;
      notifyTaskFinished(s.STATE, finishedTaskKind);
    }
    if (s.STATE === "error" && finishedTaskKind === "mode") refreshStatus(true);
  } else if (state.activeTaskId &&
             (taskId !== state.activeTaskId || !isRunning)) {
    clearPendingTask();
  }
  return s;
}

async function refreshStatus(force = false) {
  try {
    const raw = await runScript("status");
    if (!raw) throw new Error("empty status response");
    if (!force && raw === state.prevStatusRaw) return state.status;
    state.prevStatusRaw = raw;
    const s = parseKeyValueOutput(raw);
    if (s.USER_LANG === "en") applyLanguage("en");
    else if (s.USER_LANG === "zh") applyLanguage("zh");
    return applyStatus(s);
  } catch (e) {
    console.error("refreshStatus failed:", e);
    state.prevStatusRaw = "";
    setControlsUnavailable(`${i18n[state.lang].statusReadFail}: ${e.message}`);
    return null;
  }
}


async function refreshLog() {
  try {
    const raw = (await runScript("tail", "200")).trim();
    if (raw === state.prevLogRaw) return;
    state.prevLogRaw = raw;
    const log = raw.replace(/@NL@/g, String.fromCharCode(10));
    elements.logOutput.textContent = log || i18n[state.lang].logWaiting;
    elements.logOutput.scrollTop = elements.logOutput.scrollHeight;
  } catch (e) {
    elements.logOutput.textContent = `${state.lang === "zh" ? "日志读取失败" : "Log Read Failed"}: ${e.message}`;
  }
}

function closeConfirmModal() {
  state.confirmStep = 0;
  state.pendingAction = null;
  elements.confirmModal.classList.add("hidden");
  elements.confirmModal.setAttribute("aria-hidden", "true");
  elements.nextConfirmButton.textContent = i18n[state.lang].continue;
}

function getPatchArgString() {
  const parts = [];
  if (elements.patchVendorBootCheckbox.checked) parts.push("vendor_boot=1");
  if (elements.suppliedAblCheckbox.checked) parts.push("abl=supplied");
  if (elements.suppliedVbmetaCheckbox.checked) parts.push("vbmeta=supplied");
  if (elements.debugModeCheckbox.checked) parts.push("debug=1");
  return parts.join(",");
}

function getSelectedPatchName() {
  if (elements.patchVendorBootCheckbox.checked) return "vendor_boot";
  return "";
}

function openConfirmModal(action) {
  const t = i18n[state.lang];
  state.pendingAction = action;
  state.confirmStep = 1;
  if (action === "bds-tools") {
    document.querySelector("#modalTitle").textContent = t.confirmBdsTools;
    elements.confirmText.textContent = t.modalBdsStep1;
    elements.nextConfirmButton.textContent = t.continue;
  } else if (action === "patch-part") {
    document.querySelector("#modalTitle").textContent = t.confirmPatchPart;
    elements.confirmText.textContent = t.modalPatchStep1;
    elements.nextConfirmButton.textContent = t.continue;
  } else {
    document.querySelector("#modalTitle").textContent = t.confirmFlash;
    const tar = state.status?.TARGET_SLOT || "?";
    const efisp = elements.updateEfispCheckbox?.checked;
    const dbg = elements.debugModeCheckbox?.checked;
    const patchName = getSelectedPatchName();
    let msg = "";

    // 仅修补模式：不更新efisp且勾选了修补
    if (!efisp && patchName) {
      msg = t.modalStep1PatchOnly(tar, patchName);
    } else if (dbg) {
      msg = t.modalStep1Debug;
    } else {
      msg = t.modalStep1Normal(tar);
      msg += efisp ? t.withEfisp : t.noEfisp;
      if (patchName) msg += t.withPatch(patchName);
      msg += t.confirmSlot;
    }

    elements.confirmText.textContent = msg;
    elements.nextConfirmButton.textContent = dbg ? (state.lang === "zh" ? "开始调试" : "Start Debug") : t.continue;
  }
  elements.confirmModal.classList.remove("hidden");
  elements.confirmModal.setAttribute("aria-hidden", "false");
}

function handleConfirmProgress() {
  const t = i18n[state.lang];
  if (state.confirmStep === 1) {
    if (state.pendingAction === "bds-tools") {
      state.confirmStep = 2;
      elements.confirmText.textContent = t.modalBdsStep2;
      elements.nextConfirmButton.textContent = state.lang === "zh" ? "开始更新" : "Start Update";
      return;
    } else if(state.pendingAction === "patch-part"){
      state.confirmStep = 2;
      elements.confirmText.textContent = t.modalPatchStep2;
      elements.nextConfirmButton.textContent = state.lang === "zh" ? "确认修补" : "Confirm Patch";
      return;
    }
    const dbg = elements.debugModeCheckbox?.checked;
    if (!dbg) {
      state.confirmStep = 2;
      elements.confirmText.textContent = t.modalStep2;
      elements.nextConfirmButton.textContent = state.lang === "zh" ? "确认执行" : "Confirm";
      return;
    }
    closeConfirmModal();
    startFlash();
    return;
  }
  const action = state.pendingAction;
  closeConfirmModal();
  if (action === "bds-tools") startBdsTools();
  else if(action === "patch-part") startPatchPart();
  else startFlash();
}

function showTaskStarting(message, taskId) {
  renderStatus({
    ...(state.status || {}),
    RUNNING: "1",
    STATE: "running",
    MESSAGE: message,
    TASK_ID: taskId || state.activeTaskId,
    UPDATED_AT: new Date().toLocaleString()
  });
}

function handleStartResult(out, startedMessage, taskKind = "") {
  const t = i18n[state.lang];
  if (out.ALREADY_RUNNING) {
    toast(t.toastRunning);
    return;
  }
  if (out.STARTED === "1" || out.FINISHED) {
    rememberPendingTask(out.TASK_ID || "", taskKind);
    if (out.STARTED === "1") {
      showTaskStarting(startedMessage, out.TASK_ID || "");
      toast(startedMessage);
    }
    return;
  }
  toast(t.toastStartError);
}

async function startFlash() {
  const t = i18n[state.lang];
  const efisp = elements.updateEfispCheckbox?.checked;
  const dbg = elements.debugModeCheckbox?.checked;
  const baseMode = dbg ? "debug" : (efisp ? "update-efisp" : "skip-efisp");
  const patchArgs = getPatchArgString();
  const fullMode = patchArgs ? `${baseMode},${patchArgs}` : baseMode;

  setTaskControlsBusy();
  try {
    const out = parseKeyValueOutput(await runScript("start", fullMode));
    handleStartResult(out, dbg ? t.toastStartDebug : t.toastStartFlash);
  } catch (e) { toast(`${t.startFail}: ${e.message}`); }
  await manualRefresh();
}

async function startPatchPart() {
  const t = i18n[state.lang];
  setTaskControlsBusy();
  try {
    const out = parseKeyValueOutput(await runScript("start-patch", getPatchArgString()));
    handleStartResult(out, t.toastStartPatch);
  } catch (e) { toast(`${t.startFail}: ${e.message}`); }
  await manualRefresh();
}

async function startBdsTools() {
  const t = i18n[state.lang];
  setTaskControlsBusy();
  try {
    const out = parseKeyValueOutput(await runScript("start", "update-bds-tools"));
    handleStartResult(out, t.toastStartBdsTools);
  } catch (e) { toast(`${t.startFail}: ${e.message}`); }
  await manualRefresh();
}

async function savePreferredMode() {
  const t = i18n[state.lang];
  const mode = elements.preferredModeSelect.value;
  if (!BOOT_MODES.has(mode)) {
    toast(t.toastStartError);
    return;
  }
  setTaskControlsBusy();
  let startError = null;
  try {
    const out = parseKeyValueOutput(await runScript("start-mode", mode));
    handleStartResult(out, t.toastStartMode, "mode");
  } catch (error) {
    startError = error;
  }
  await manualRefresh();
  if (startError) renderModeStartError(startError);
}

async function clearLog() {
  const t = i18n[state.lang];
  try {
    const out = parseKeyValueOutput(await runScript("clear-log"));
    if (out.BUSY === "1") { toast(t.toastLogBusy); return; }
    toast(t.toastLogCleared);
  } catch (e) { toast(`${state.lang === "zh" ? "清空失败" : "Clear Failed"}: ${e.message}`); }
  await manualRefresh();
}

function schedulePoll(delay) {
  if (state.pollTimer !== null) clearTimeout(state.pollTimer);
  state.pollTimer = null;
  if (delay !== null && !document.hidden) state.pollTimer = setTimeout(poll, delay);
}

async function poll(forceStatus = false) {
  if (state.pollInFlight) return;
  state.pollInFlight = true;
  try {
    await refreshLog();
    const running = state.taskStarted || state.status?.RUNNING === "1";
    if (forceStatus || !running || state.pollCount++ % 3 === 0) await refreshStatus();
  } catch (e) {
    console.error("poll failed:", e);
  } finally {
    state.pollInFlight = false;
    const running = state.taskStarted || state.status?.RUNNING === "1";
    schedulePoll(running ? 1000 : 8000);
  }
}

function startPolling() {
  schedulePoll(0);
}

async function manualRefresh() {
  state.prevStatusRaw = "";
  state.prevLogRaw = "";
  schedulePoll(null);
  await refreshSuppliedImages();
  await poll(true);
}
async function init() {
  try {
    const info = moduleInfo();
    if(!info) return;
    state.moduleDir = info.moduleDir;
    state.scriptPath = `${state.moduleDir}/bin/bl_flasher.sh`;
    try {
      state.activeTaskId = localStorage.getItem("blFlasherPendingTaskId") || "";
      state.activeTaskKind = localStorage.getItem("blFlasherPendingTaskKind") || "";
    } catch {}
    state.taskStarted = Boolean(state.activeTaskId);
    await refreshSuppliedImages();
    await refreshStatus();
    await refreshLog();
  } catch (e) {
    elements.stateChip.textContent = state.lang === "zh" ? "WebUI 初始化失败" : "WebUI Init Failed";
    elements.stateChip.className = "chip chip-danger";
    elements.taskMessage.textContent = e.message;
    elements.flashButton.disabled = true;
    elements.bdsToolsButton.disabled = true;
    elements.patchPartButton.disabled = true;
    elements.clearLogButton.disabled = true;
    elements.preferredModeSelect.disabled = true;
    elements.updateEfispCheckbox.disabled = true;
    elements.debugModeCheckbox.disabled = true;
    elements.patchVendorBootCheckbox.disabled = true;
    elements.suppliedAblCheckbox.disabled = true;
    elements.suppliedVbmetaCheckbox.disabled = true;
    elements.saveModeButton.disabled = true;
    return;
  }

  elements.refreshButton.addEventListener("click", manualRefresh);
  elements.flashButton.addEventListener("click", () => openConfirmModal("flash"));
  elements.bdsToolsButton.addEventListener("click", () => openConfirmModal("bds-tools"));
  elements.saveModeButton.addEventListener("click", savePreferredMode);
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) schedulePoll(null);
    else manualRefresh();
  });

  elements.patchPartButton.addEventListener("click", () => openConfirmModal("patch-part"));
  elements.clearLogButton.addEventListener("click", clearLog);
  elements.cancelConfirmButton.addEventListener("click", closeConfirmModal);
  elements.nextConfirmButton.addEventListener("click", handleConfirmProgress);
  elements.confirmModal.addEventListener("click", e => e.target === elements.confirmModal && closeConfirmModal());

  startPolling();
}

init();
