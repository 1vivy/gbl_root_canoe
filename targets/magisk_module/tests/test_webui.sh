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
  rm -rf "$TMP"
}
trap cleanup EXIT INT TERM HUP

mkdir -p "$WEBROOT"
cp -R "$ROOT/targets/magisk_module/module/webroot/." "$WEBROOT/"

cat > "$WEBROOT/mock.js" <<'EOF'
localStorage.setItem("blFlasherPendingTaskId", "stale-task");
localStorage.setItem("blFlasherPendingTaskKind", "mode");
window.__ksuMock = {
  status: "CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=0|PID=|STATE=success|MESSAGE=ready|UPDATED_AT=now|TASK_ID=other-task|PREFERRED_MODE=2|MODE_DEFAULTED=0|MODE_READ_ERROR=0|USER_LANG=en",
  failStatus: false,
  toasts: [],
  startModeFailure: false,
  modeTaskNumber: 0,
  lastModeTaskId: "",
  fatCommands: [],
};
window.ksu = {
  moduleInfo() {
    return JSON.stringify({ moduleDir: "/data/adb/modules/fake" });
  },
  toast(message) {
    window.__ksuMock.toasts.push(message);
  },
  exec(command, _options, callbackName) {
    if (command.includes("canoe_fat_provision.sh")) {
      window.__ksuMock.fatCommands.push(command);
      window[callbackName](0, "EXISTS=1|NON_SPARSE=1|SIZE_OK=1|RUN_COUNT=2|RUNS=100:8,200:4|EXTENT_SOURCE=filefrag|STAMP_VALID=1|STAMP_MATCH=1", "");
      return;
    }
    if (command.includes(" status")) {
      if (window.__ksuMock.failStatus) {
        window[callbackName](1, "", "status unavailable");
      } else {
        window[callbackName](0, window.__ksuMock.status, "");
      }
      return;
    }
    if (command.includes(" tail")) {
      window[callbackName](0, "", "");
      return;
    }
    if (command.includes(" start-mode")) {
      window.__ksuMock.lastStartModeCommand = command;
      if (window.__ksuMock.startModeFailure) {
        window[callbackName](1,
          "STARTED=0|ERROR_CODE=MODE2_PROFILE_MISSING|ERROR=preferred mode preflight failed", "");
        return;
      }
      window.__ksuMock.modeTaskNumber += 1;
      const taskId = `mode-task-${window.__ksuMock.modeTaskNumber}`;
      window.__ksuMock.lastModeTaskId = taskId;
      window.__ksuMock.status = `CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=1|PID=100|STATE=running|MESSAGE=saving preferred mode|UPDATED_AT=now|TASK_ID=${taskId}|PREFERRED_MODE=2|MODE_DEFAULTED=0|MODE_READ_ERROR=0|USER_LANG=en`;
      window[callbackName](0, `STARTED=1|TASK_ID=${taskId}`, "");
      return;
    }
    window[callbackName](0, "", "");
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
const disabled = selector => document.querySelector(selector).disabled;
const optionControls = [
  "#updateEfispCheckbox",
  "#debugModeCheckbox",
  "#patchVendorBootCheckbox",
  "#patchSuperCheckbox"
];

try {
  await waitFor(
    () => document.querySelector("#preferredModeSelect")?.value === "2",
    "saved Mode 2 did not render"
  );
  const labels = [...document.querySelectorAll("#preferredModeSelect option")]
    .map(option => option.textContent);
  assert(JSON.stringify(labels) === JSON.stringify([
    "Mode 0 - Honest unlocked",
    "Mode 1 - ABL fake locked",
    "Mode 2 - KM/SPSS profile spoof"
  ]), "boot mode labels are incomplete");
  assert(document.querySelector("#saveModeButton").textContent === "Save",
    "preferred mode Save action label is missing");
  assert(localStorage.getItem("blFlasherPendingTaskId") === null,
    "stale pending task was not reconciled");
  assert(document.querySelector("#fatProvisionButton")?.textContent === "Create / write FAT file",
    "FAT provision control is missing");
  assert(document.querySelector("#fatVerifyButton")?.textContent === "Verify FAT file",
    "FAT verify control is missing");
  assert(document.querySelector("#fatRestampButton")?.textContent === "Restamp extents",
    "FAT restamp control is missing");
  assert(window.__ksuMock.fatCommands.length === 0,
    "FAT provisioning ran automatically during WebUI initialization");
  document.querySelector("#fatVerifyButton").click();
  await waitFor(
    () => window.__ksuMock.fatCommands.length === 1 &&
      document.querySelector("#fatRunsText").textContent.includes("2"),
    "FAT verify action did not render its extent run count"
  );

  window.__ksuMock.status = "CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=0|PID=|STATE=success|MESSAGE=ready|UPDATED_AT=now|TASK_ID=defaulted-task|PREFERRED_MODE=1|MODE_DEFAULTED=1|MODE_READ_ERROR=0|USER_LANG=en";
  document.querySelector("#refreshButton").click();
  await waitFor(
    () => document.querySelector("#modeStatusText").textContent ===
      "No saved mode; currently using default Mode 1",
    "defaulted Mode 1 status was not rendered"
  );
  assert(document.querySelector("#preferredModeSelect").value === "1",
    "defaulted raw Mode 1 was not selected");

  window.__ksuMock.status = "CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=1|PID=99|STATE=success|MESSAGE=busy|UPDATED_AT=now|TASK_ID=live-task|PREFERRED_MODE=2|MODE_DEFAULTED=0|MODE_READ_ERROR=0|USER_LANG=en";
  document.querySelector("#refreshButton").click();
  await waitFor(() => disabled("#updateEfispCheckbox"), "busy controls were not disabled");
  assert(optionControls.every(disabled), "a busy option checkbox remained enabled");
  assert([
    "#flashButton", "#bdsToolsButton", "#patchPartButton", "#clearLogButton",
    "#preferredModeSelect", "#saveModeButton"
  ].every(disabled), "a busy task action remained enabled");

  window.__ksuMock.status = "CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=0|PID=|STATE=success|MESSAGE=ready|UPDATED_AT=now|TASK_ID=idle-task|PREFERRED_MODE=2|MODE_DEFAULTED=0|MODE_READ_ERROR=0|USER_LANG=en";
  document.querySelector("#refreshButton").click();
  await waitFor(() => !disabled("#saveModeButton"), "mode controls did not re-enable");
  document.querySelector("#preferredModeSelect").value = "0";
  document.querySelector("#saveModeButton").click();
  assert(optionControls.every(disabled), "start request left an option enabled");
  assert([
    "#flashButton", "#bdsToolsButton", "#patchPartButton", "#clearLogButton",
    "#preferredModeSelect", "#saveModeButton"
  ].every(disabled), "start request left a task action enabled");
  await waitFor(
    () => document.querySelector("#stateChip").textContent.includes("running"),
    "mode task did not enter running state"
  );
  assert(window.__ksuMock.lastStartModeCommand.includes(" start-mode '0'"),
    "selected Mode 0 was not passed to the worker");

  const successTaskId = window.__ksuMock.lastModeTaskId;
  window.__ksuMock.status = `CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=0|PID=|STATE=success|MESSAGE=preferred mode saved|UPDATED_AT=now|TASK_ID=${successTaskId}|PREFERRED_MODE=0|MODE_DEFAULTED=0|MODE_READ_ERROR=0|USER_LANG=en`;
  document.querySelector("#refreshButton").click();
  await waitFor(
    () => window.__ksuMock.toasts.includes("Preferred boot mode saved"),
    "successful mode task did not complete through status polling"
  );

  await waitFor(() => !disabled("#saveModeButton"), "mode controls did not re-enable after success");
  document.querySelector("#preferredModeSelect").value = "1";
  document.querySelector("#saveModeButton").click();
  await waitFor(() => window.__ksuMock.modeTaskNumber === 2,
    "second mode task did not start");
  const failedTaskId = window.__ksuMock.lastModeTaskId;
  window.__ksuMock.status = `CURRENT_SLOT=_a|TARGET_SLOT=_b|RUNNING=0|PID=|STATE=error|MESSAGE=preferred mode write failed|UPDATED_AT=now|TASK_ID=${failedTaskId}|PREFERRED_MODE=1|MODE_DEFAULTED=0|MODE_READ_ERROR=0|USER_LANG=en`;
  document.querySelector("#refreshButton").click();
  await waitFor(
    () => document.querySelector("#stateChip").textContent === "Status: error" &&
      document.querySelector("#preferredModeSelect").value === "1",
    "failed mode write did not refresh the backend error and actual raw mode"
  );
  assert(document.querySelector("#stateChip").textContent === "Status: error",
    "failed mode write did not render the backend error state");
  assert(window.__ksuMock.toasts.includes("Task finished (failed)"),
    "failed mode write did not notify failure");

  await waitFor(() => !disabled("#saveModeButton"), "mode controls did not re-enable after failure");
  window.__ksuMock.startModeFailure = true;
  document.querySelector("#preferredModeSelect").value = "2";
  document.querySelector("#saveModeButton").click();
  await waitFor(
    () => document.querySelector("#modeStatusText").textContent ===
      "Mode 2 profile is missing; install a valid boot.efi.gm2p first",
    "missing Mode 2 profile error was not rendered"
  );
  assert(document.querySelector("#taskMessage").textContent ===
    "Mode 2 profile is missing; install a valid boot.efi.gm2p first",
    "missing Mode 2 profile error did not reach task status");
  assert(window.__ksuMock.toasts.includes(
    "Mode 2 profile is missing; install a valid boot.efi.gm2p first"),
    "missing Mode 2 profile error did not reach the toast");

  window.__ksuMock.failStatus = true;
  document.querySelector("#refreshButton").click();
  await waitFor(
    () => document.querySelector("#stateChip").textContent === "Status Read Failed",
    "status read failure was not rendered"
  );
  assert(!disabled("#refreshButton"), "status failure disabled manual refresh");
  assert(optionControls.every(disabled), "status failure left an option enabled");

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
replacement = '\n'.join([
    '<script src="mock.js"></script>',
    needle,
    '<script type="module" src="test-driver.js"></script>',
])
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

python3 -u -m http.server "$PORT" --bind 127.0.0.1 --directory "$WEBROOT" \
  > "$TMP/server.log" 2>&1 &
SERVER_PID=$!
sleep 1

"$CHROME" --headless=new --no-sandbox --disable-gpu --disable-dev-shm-usage \
  --disable-background-networking --user-data-dir="$TMP/chrome" \
  --virtual-time-budget=12000 --dump-dom "http://127.0.0.1:$PORT/" \
  > "$TMP/dom.html" 2> "$TMP/chrome.log"

if ! grep -q 'data-test-result="pass"' "$TMP/dom.html" ||
   ! grep -q 'WEBUI_TEST_PASS' "$TMP/dom.html"; then
  cat "$TMP/dom.html" >&2
  cat "$TMP/chrome.log" >&2
  echo "FAIL: browser-driven WebUI fixture" >&2
  exit 1
fi

echo "ok - browser-driven WebUI mode and task controls"
