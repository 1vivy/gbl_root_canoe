#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/canoe-webui-test.$$
WEBROOT="$TMP/webroot"
PORT=$((20000 + ($$ % 20000)))
SERVER_PID=

cleanup() {
  if [ -n "$SERVER_PID" ]; then
    kill "$SERVER_PID" 2>/dev/null || :
    wait "$SERVER_PID" 2>/dev/null || :
  fi
  if [ "${KEEP_TMP:-0}" != 1 ]; then rm -rf "$TMP"; fi
}
trap cleanup EXIT INT TERM HUP

mkdir -p "$WEBROOT"
cp -R "$ROOT/targets/magisk_module/module/webroot/." "$WEBROOT/"

cat > "$WEBROOT/mock.js" <<'EOF'
window.__ksuMock = {
  bootctlAvailable: true,
  requests: [],
  toasts: [],
  entries: [{id: "android-a", title: "Android A", image: "boot_a.efi", role: "active", mode: 2, options: null, unknown: []}],
  policy: {menu_mode: "silent", key_window_ms: 1200, menu_timeout_s: 5},
};
window.ksu = {
  toast(message) { window.__ksuMock.toasts.push(message); },
  exec(command, _options, callbackName) {
    const respond = (errno, stdout, stderr) => window.setTimeout(() => window[callbackName](errno, stdout, stderr), 0);
    if (command === "getprop ro.boot.slot_suffix") {
      respond(0, "_a\n", "");
      return;
    }
    if (command === "bootctl get-active-boot-slot") {
      respond(window.__ksuMock.bootctlAvailable ? 0 : 1, window.__ksuMock.bootctlAvailable ? "0\n" : "", "");
      return;
    }
    const token = command.split(" ").pop();
    let request;
    try {
      const padded = token.replace(/-/g, "+").replace(/_/g, "/") + "=".repeat((4 - (token.length % 4)) % 4);
      request = JSON.parse(decodeURIComponent(escape(atob(padded))));
    } catch (error) {
      const padded = token.replace(/-/g, "+").replace(/_/g, "/") + "=".repeat((4 - (token.length % 4)) % 4);
      return;
    }
    window.__ksuMock.requests.push({ command, request });
    switch (request.verb) {
      case "slot.status":
        respond(0, JSON.stringify({ok: true, operation: "slot.status", active_slot: "a", inactive_slot: "b", source: request.bootctl_output ? "bootctl" : "explicit", installed: ["a"]}), "");
        return;
      case "entry.list":
        respond(0, JSON.stringify({ok: true, operation: "entry.list", generation: 3, entries: window.__ksuMock.entries}), "");
        return;
      case "bls.list":
        respond(0, JSON.stringify({ok: true, operation: "bls.list", entries: [{name: "pmos.conf", entry: {title: "postmarketOS", kind: "linux", image: "\\vmlinuz", initrd: "\\initrd", devicetree: null, options: "", unknown: [], rejected_lines: 0}}]}), "");
        return;
      case "default.get":
        respond(0, JSON.stringify({ok: true, operation: "default.get", default: "android-a"}), "");
        return;
      case "config.show":
        respond(0, JSON.stringify({ok: true, operation: "config.show", config: window.__ksuMock.policy}), "");
        return;
      case "config.set-policy":
        if (request.key_window_ms > 10000 || request.menu_timeout_s > 300) {
          respond(0, JSON.stringify({ok: false, error: {code: "policy.range", message: "key_window_ms must be in 0..=10000"}}), "");
          return;
        }
        window.__ksuMock.policy = {menu_mode: request.menu_mode, key_window_ms: request.key_window_ms, menu_timeout_s: request.menu_timeout_s};
        respond(0, JSON.stringify({ok: true, operation: "config.policy", config: window.__ksuMock.policy, generation: 4, mark: "policy"}), "");
        return;
      case "default.set":
        if (request.id === "bls:missing") {
          respond(0, JSON.stringify({ok: false, error: {code: "default.target", message: "default target bls:missing was not discovered"}}), "");
          return;
        }
        respond(0, JSON.stringify({ok: true, operation: "default.set", generation: 4, default: request.id}), "");
        return;
      case "entry.mode":
        respond(0, JSON.stringify({ok: true, operation: "entry.mode", generation: 5}), "");
        return;
      case "install":
        respond(0, JSON.stringify({ok: true, operation: "install", receipt: {active_slot: "a", installed: request.both ? ["a", "b"] : [request.inactive ? "b" : "a"], generation: 6, signer_changed: false, backup_present: true}}), "");
        return;
      case "ota-apply":
        respond(0, JSON.stringify({ok: true, operation: "ota-apply", receipt: {active_slot: "a", installed: ["b"], generation: 7, signer_changed: false, backup_present: true}}), "");
        return;
      default:
        respond(1, JSON.stringify({ok: false, error: {code: "request", message: `unknown ${request.verb}`}}), "");
        return;
    }
  }
};
EOF

cat > "$WEBROOT/test-driver.js" <<'EOF'
const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const waitFor = async (predicate, message) => {
  for (let attempt = 0; attempt < 200; attempt += 1) {
    if (predicate()) return;
    await delay(20);
  }
  throw new Error(message);
};
const assert = (condition, message) => {
  if (!condition) throw new Error(message);
};
const requests = () => window.__ksuMock.requests.map(item => item.request);

try {
  await waitFor(() => document.querySelector("#activeSlot").textContent === "A" && document.querySelector("#entryTableBody tr[data-entry-id='android-a']"), "slot status or entries were not rendered");
  assert(document.querySelector("#slotSource").textContent.includes("bootctl"), "slot provenance omitted bootctl");
  assert(document.querySelector("#entryTableBody tr[data-entry-id='android-a']"), "entry.list row was not rendered");
  assert(document.querySelector("#blsTableBody td").textContent === "pmos.conf", "bls.list row was not rendered");
  assert(document.querySelector("#entryDetail").textContent.includes("boot_a.efi"), "entry detail was not rendered");
  assert(document.querySelector("#defaultSelect").value === "android-a", "default.get was not rendered");
  assert(document.querySelector("#defaultSelect option[value='bls:pmos']").textContent.includes("BLS"), "BLS default candidate was not rendered");
  assert(document.querySelector("#menuModeSelect").value === "silent", "silent policy was not rendered");
  assert(document.querySelector("#keyWindowInput").value === "1200", "key window was not rendered");
  assert(document.querySelector("#menuTimeoutInput").value === "5" && document.querySelector("#menuTimeoutInput").disabled, "silent timeout control was not disabled");
  assert(document.querySelector("meta[http-equiv=Content-Security-Policy]"), "CSP meta is missing");

  document.querySelector("#installTarget").value = "inactive";
  document.querySelector("#installTarget").dispatchEvent(new Event("change"));
  assert(!document.querySelector("#inactiveConfirmation").hidden, "inactive confirmation was not shown");
  assert(document.querySelector("#inactiveStatusConfirm").nextElementSibling.textContent.includes("I know the status"), "inactive confirmation label is missing");
  const beforeRefusal = requests().length;
  document.querySelector("#installButton").click();
  await waitFor(() => document.querySelector("#taskMessage").textContent.includes("I know the status"), "inactive install was not gated");
  assert(requests().length === beforeRefusal, "gated inactive install reached the core");

  document.querySelector("#inactiveStatusConfirm").checked = true;
  document.querySelector("#installButton").click();
  await waitFor(() => requests().some(request => request.verb === "install"), "install request did not reach the core");
  const installRequest = requests().find(request => request.verb === "install");
  await waitFor(() => document.querySelector("#taskMessage").textContent === "Ready", "install did not settle");
  document.querySelector("#menuModeSelect").value = "menu";
  document.querySelector("#menuModeSelect").dispatchEvent(new Event("change"));
  assert(!document.querySelector("#menuTimeoutInput").disabled, "menu timeout stayed disabled in Menu mode");
  document.querySelector("#keyWindowInput").value = "2500";
  document.querySelector("#menuTimeoutInput").value = "12";
  document.querySelector("#savePolicyButton").click();
  await waitFor(() => requests().some(request => request.verb === "config.set-policy"), "policy request did not reach the core");
  const policyRequest = requests().find(request => request.verb === "config.set-policy");
  assert(policyRequest.menu_mode === "menu" && policyRequest.key_window_ms === 2500 && policyRequest.menu_timeout_s === 12, "policy request fields were incorrect");
  await waitFor(() => document.querySelector("#taskMessage").textContent === "Ready", "policy did not settle");

  document.querySelector("#defaultSelect").value = "bls:pmos";
  document.querySelector("#saveDefaultButton").click();
  await waitFor(() => requests().some(request => request.verb === "default.set" && request.id === "bls:pmos"), "BLS default.set request did not reach the core");
  await waitFor(() => document.querySelector("#taskMessage").textContent === "Ready", "BLS default did not settle");
  const missingOption = document.createElement("option");
  missingOption.value = "bls:missing";
  missingOption.textContent = "BLS · bls:missing · missing";
  document.querySelector("#defaultSelect").append(missingOption);
  document.querySelector("#defaultSelect").value = "bls:missing";
  document.querySelector("#saveDefaultButton").click();
  await waitFor(() => document.querySelector("#taskMessage").textContent.includes("default.target: default target bls:missing was not discovered"), "missing BLS refusal was not rendered");

  document.querySelector("#keyWindowInput").value = "10001";
  document.querySelector("#savePolicyButton").click();
  await waitFor(() => document.querySelector("#taskMessage").textContent.includes("policy.range: key_window_ms must be in 0..=10000"), "policy range refusal was not rendered");
  document.querySelector("#keyWindowInput").value = "1200";

  document.querySelector("#otaButton").click();
  await waitFor(() => requests().some(request => request.verb === "ota-apply"), "ota-apply request did not reach the core");
  const otaRequest = requests().find(request => request.verb === "ota-apply");
  await waitFor(() => document.querySelector("#taskMessage").textContent === "Ready", "OTA did not settle");
  assert(otaRequest.target_slot === "b", "OTA did not target the inactive slot");
  for (const item of window.__ksuMock.requests) {
    if (item.request.verb !== "slot.status") assert(/^canoe-bootmgr --boot-root \/mnt\/vendor\/persist\/efisp --request-b64 [A-Za-z0-9_-]+$/.test(item.command), "core command contained an unsafe token");
  }

  window.__ksuMock.bootctlAvailable = false;
  document.querySelector("#refreshButton").click();
  await waitFor(() => document.querySelector("#otaButton").disabled && document.querySelector("#otaHint").textContent.includes("OTA refused"), "OTA stayed enabled without bootctl/GPT metadata");
  assert(document.querySelector("#otaHint").textContent.includes("OTA refused"), "metadata refusal was not explained");

  document.documentElement.dataset.testResult = "pass";
  document.body.insertAdjacentHTML("beforeend", '<pre id="webuiTestResult">WEBUI_TEST_PASS</pre>');
} catch (error) {
  document.documentElement.dataset.testResult = "fail";
  const result = document.createElement("pre");
  result.id = "webuiTestResult";
  result.textContent = `WEBUI_TEST_FAIL: ${error.stack || error.message}`;
  document.body.append(result);
}
EOF

python3 - "$WEBROOT/index.html" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
needle = '<script type="module" src="app.js"></script>'
replacement = '\n'.join(['<script src="mock.js"></script>', needle, '<script type="module" src="test-driver.js"></script>'])
if needle not in text:
    raise SystemExit("app.js script tag not found")
path.write_text(text.replace(needle, replacement, 1))
PY

if [ -n "${CHROME_BIN:-}" ]; then
  CHROME=$CHROME_BIN
else
  CHROME=
  for candidate in google-chrome-stable google-chrome chromium chromium-browser; do
    if command -v "$candidate" >/dev/null 2>&1; then
      CHROME=$(command -v "$candidate")
      break
    fi
  done
fi
[ -n "$CHROME" ] || { echo "FAIL: no Chrome/Chromium binary for WebUI test" >&2; exit 1; }

python3 -u -m http.server "$PORT" --bind 127.0.0.1 --directory "$WEBROOT" > "$TMP/server.log" 2>&1 &
SERVER_PID=$!
sleep 1
"$CHROME" --headless=new --no-sandbox --disable-gpu --disable-dev-shm-usage --disable-background-networking --user-data-dir="$TMP/chrome" --virtual-time-budget=12000 --dump-dom "http://127.0.0.1:$PORT/" > "$TMP/dom.html" 2> "$TMP/chrome.log"

if ! grep -q 'data-test-result="pass"' "$TMP/dom.html" || ! grep -q 'WEBUI_TEST_PASS' "$TMP/dom.html"; then
  cat "$TMP/dom.html" >&2
  cat "$TMP/chrome.log" >&2
  echo "FAIL: browser-driven WebUI fixture" >&2
  exit 1
fi

echo "ok - browser-driven WebUI core protocol and slot controls"
