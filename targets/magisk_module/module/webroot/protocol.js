import { exec } from "./kernelsu.js";

const CORE_COMMAND = "canoe-bootmgr --boot-root /mnt/vendor/persist/efisp --request-b64";
const GETPROP_COMMAND = "getprop ro.boot.slot_suffix";
const BOOTCTL_COMMAND = "bootctl get-active-boot-slot";

export class CoreError extends Error {
  constructor(code, message, response) {
    super(message);
    this.name = "CoreError";
    this.code = code;
    this.response = response;
  }
}

function isRecord(value) {
  return value !== null && typeof value === "object";
}

function utf8(value) {
  if (typeof TextEncoder === "function") {
    const bytes = new TextEncoder().encode(value);
    let text = "";
    for (let index = 0; index < bytes.length; index += 1) text += String.fromCharCode(bytes[index]);
    return text;
  }
  return unescape(encodeURIComponent(value));
}

function base64Url(value) {
  return btoa(utf8(value)).replace(/=/g, "").replace(/\+/g, "-").replace(/\//g, "_");
}

function parseResponse(stdout) {
  const lines = String(stdout || "").trim().split(/\r?\n/).filter(Boolean);
  if (lines.length !== 1) throw new Error("canoe-bootmgr returned a non-JSONL response");
  let response;
  try {
    response = JSON.parse(lines[0]);
  } catch (error) {
    throw new Error(`canoe-bootmgr returned invalid JSON: ${error instanceof Error ? error.message : String(error)}`);
  }
  if (!isRecord(response) || typeof response.ok !== "boolean") {
    throw new Error("canoe-bootmgr returned an invalid response envelope");
  }
  return response;
}

export async function core(request) {
  const token = base64Url(JSON.stringify(request));
  if (!/^[A-Za-z0-9_-]+$/.test(token)) throw new Error("request encoding produced an unsafe token");
  const result = await exec(`${CORE_COMMAND} ${token}`);
  const response = parseResponse(result.stdout);
  if (!response.ok) {
    const body = isRecord(response.error) ? response.error : {};
    const code = typeof body.code === "string" ? body.code : "operation";
    const message = typeof body.message === "string" ? body.message : "boot manager operation failed";
    throw new CoreError(code, message, response);
  }
  return response;
}

async function fixedProbe(command) {
  const result = await exec(command);
  if (result.errno !== 0) return null;
  const value = String(result.stdout || "").trim();
  return value || null;
}

function slot(value) {
  const normalized = String(value || "").trim().toLowerCase().replace(/^_/, "");
  if (normalized === "0" || normalized === "a") return "a";
  if (normalized === "1" || normalized === "b") return "b";
  return null;
}

export async function readSlotStatus() {
  const explicitRaw = await fixedProbe(GETPROP_COMMAND);
  const bootctlRaw = await fixedProbe(BOOTCTL_COMMAND);
  const request = { verb: "slot.status" };
  if (bootctlRaw !== null && slot(bootctlRaw) !== null) {
    request.bootctl_output = bootctlRaw;
  } else if (slot(explicitRaw) !== null) {
    request.slot = slot(explicitRaw);
  }
  const response = await core(request);
  return { response, bootctlAvailable: bootctlRaw !== null && slot(bootctlRaw) !== null };
}
