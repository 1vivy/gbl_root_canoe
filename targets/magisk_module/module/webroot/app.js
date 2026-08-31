import { core, CoreError, readSlotStatus } from "./protocol.js";
import { element, renderEntries, renderMode, renderPolicy, renderSlot, setBusy } from "./render.js";
import { toast } from "./kernelsu.js";

const state = {
  slot: null,
  bootctlAvailable: false,
  entries: [],
  blsEntries: [],
  defaultId: null,
  policy: null,
  selectedId: null,
  busy: false,
};

function messageFrom(error) {
  if (error instanceof CoreError) return `${error.code}: ${error.message}`;
  if (error instanceof Error) return error.message;
  return String(error);
}

function showMessage(message, kind) {
  const node = element("taskMessage");
  node.textContent = message;
  node.className = `stat-value stat-small ${kind || ""}`;
  element("stateChip").textContent = message;
  element("stateChip").className = `chip ${kind === "error" ? "chip-danger" : "chip"}`;
}

function appendLog(value) {
  const output = element("logOutput");
  const next = `${output.textContent ? `${output.textContent}\n` : ""}${value}`;
  output.textContent = next.slice(-12000);
  output.scrollTop = output.scrollHeight;
}

function selectedEntry() {
  return state.entries.find((entry) => entry.id === state.selectedId) || null;
}

async function refreshSlot() {
  const result = await readSlotStatus();
  state.slot = result.response;
  state.bootctlAvailable = result.bootctlAvailable;
  renderSlot(state.slot, state.bootctlAvailable);
  appendLog(JSON.stringify(state.slot));
}

async function refreshEntries() {
  const entries = await core({ verb: "entry.list" });
  const bls = await core({ verb: "bls.list" });
  const defaultResult = await core({ verb: "default.get" });
  const configResult = await core({ verb: "config.show" });
  state.entries = Array.isArray(entries.entries) ? entries.entries : [];
  state.blsEntries = Array.isArray(bls.entries) ? bls.entries : [];
  state.defaultId = typeof defaultResult.default === "string" ? defaultResult.default : null;
  state.policy = configResult.config || configResult;
  if (!state.entries.some((entry) => entry.id === state.selectedId)) {
    state.selectedId = state.entries.length ? state.entries[0].id : null;
  }
  renderEntries(state.entries, state.blsEntries, state.selectedId, state.defaultId, selectEntry);
  renderMode(selectedEntry());
  renderPolicy(state.policy);
  appendLog(JSON.stringify({ operation: "entry.list", count: state.entries.length, bls: state.blsEntries.length }));
}

async function refreshAll(force) {
  if (state.busy && !force) return;
  setBusy(true);
  showMessage("Reading boot manager state...", "");
  try {
    await refreshSlot();
    await refreshEntries();
    showMessage("Ready", "ok");
  } catch (error) {
    showMessage(messageFrom(error), "error");
    appendLog(`ERROR ${messageFrom(error)}`);
  } finally {
    state.busy = false;
    setBusy(false);
  }
}

function selectEntry(id) {
  state.selectedId = id;
  renderEntries(state.entries, state.blsEntries, state.selectedId, state.defaultId, selectEntry);
  renderMode(selectedEntry());
}

async function runAction(action) {
  if (state.busy) return;
  state.busy = true;
  setBusy(true);
  showMessage("Running boot manager operation...", "");
  try {
    const response = await action();
    appendLog(JSON.stringify(response));
    toast("Canoe boot manager operation completed");
    showMessage("Operation completed", "ok");
    await refreshAll(true);
  } catch (error) {
    const message = messageFrom(error);
    appendLog(`ERROR ${message}`);
    toast(message);
    showMessage(message, "error");
  } finally {
    state.busy = false;
    setBusy(false);
  }
}

function installRequest() {
  if (!state.slot || !state.slot.active_slot) throw new Error("Install refused: active slot metadata is unknown");
  const staged = element("stagePath").value.trim();
  if (!staged) throw new Error("Install refused: staged directory is required");
  const target = element("installTarget").value;
  const active = state.slot.active_slot;
  const request = { verb: "install", staged, active_slot: active, mode: Number(element("entryModeSelect").value) };
  if (target === "inactive") {
    if (!element("inactiveStatusConfirm").checked) {
      throw new Error("Enable 'I know the status of my inactive slot' before installing there");
    }
    if (!state.slot.inactive_slot) throw new Error("Install refused: inactive slot metadata is unknown");
    request.inactive = true;
    request.i_know_inactive_status = true;
  } else if (target === "both") {
    request.slot = active;
    request.both = true;
  } else {
    request.slot = active;
  }
  return request;
}

function otaRequest() {
  if (!state.slot || !state.slot.inactive_slot) {
    throw new Error("OTA refused: bootctl/GPT metadata is unavailable");
  }
  if (!state.bootctlAvailable && state.slot.source !== "gpt") {
    throw new Error("OTA refused: bootctl/GPT metadata is unavailable; the running slot is never relabelled");
  }
  const staged = element("stagePath").value.trim();
  if (!staged) throw new Error("OTA refused: staged directory is required");
  return { verb: "ota-apply", staged, target_slot: state.slot.inactive_slot };
}

function install() {
  return runAction(() => core(installRequest()));
}

function ota() {
  return runAction(() => core(otaRequest()));
}

function saveDefault() {
  const id = element("defaultSelect").value;
  if (!id) return;
  return runAction(() => core({ verb: "default.set", id }));
}
function savePolicy() {
  const menuMode = element("menuModeSelect").value;
  const keyWindow = Number(element("keyWindowInput").value);
  const menuTimeout = Number(element("menuTimeoutInput").value);
  return runAction(() => core({
    verb: "config.set-policy",
    menu_mode: menuMode,
    key_window_ms: keyWindow,
    menu_timeout_s: menuTimeout,
  }));
}

function saveMode() {
  const entry = selectedEntry();
  if (!entry) {
    showMessage("Select an entry before saving its mode", "error");
    return;
  }
  return runAction(() => core({ verb: "entry.mode", id: entry.id, mode: Number(element("entryModeSelect").value) }));
}

function bind() {
  element("refreshButton").addEventListener("click", refreshAll);
  element("installButton").addEventListener("click", install);
  element("otaButton").addEventListener("click", ota);
  element("saveDefaultButton").addEventListener("click", saveDefault);
  element("saveEntryModeButton").addEventListener("click", saveMode);
  element("savePolicyButton").addEventListener("click", savePolicy);
  element("menuModeSelect").addEventListener("change", () => {
    element("menuTimeoutInput").disabled = element("menuModeSelect").value === "silent";
  });
  element("installTarget").addEventListener("change", () => {
    element("inactiveConfirmation").hidden = element("installTarget").value !== "inactive";
  });
  refreshAll();
}
bind();
