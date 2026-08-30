const MAX_OUTPUT_BYTES = 1024 * 1024;
let callbackCounter = 0;

function bridge() {
  if (!window.ksu || typeof window.ksu.exec !== "function") {
    throw new Error("KernelSU exec bridge is unavailable");
  }
  return window.ksu;
}

function callbackName() {
  callbackCounter += 1;
  return `canoe_exec_${Date.now()}_${callbackCounter}`;
}

function bounded(value) {
  const text = typeof value === "string" ? value : "";
  if (text.length > MAX_OUTPUT_BYTES) {
    throw new Error("KernelSU response exceeds 1 MiB");
  }
  return text;
}

export function exec(command, options) {
  return new Promise((resolve, reject) => {
    const name = callbackName();
    let timer;
    const finish = (callback) => (errno, stdout, stderr) => {
      window[name] = undefined;
      if (timer) window.clearTimeout(timer);
      try {
        callback({ errno: Number(errno), stdout: bounded(stdout), stderr: bounded(stderr) });
      } catch (error) {
        reject(error);
      }
    };
    window[name] = finish(resolve);
    timer = window.setTimeout(() => {
      window[name] = undefined;
      reject(new Error("KernelSU exec timed out"));
    }, 10000);
    try {
      bridge().exec(command, JSON.stringify(options || {}), name);
    } catch (error) {
      window[name] = undefined;
      window.clearTimeout(timer);
      reject(error instanceof Error ? error : new Error(String(error)));
    }
  });
}

export function toast(message) {
  const api = bridge();
  if (typeof api.toast === "function") api.toast(String(message));
}
