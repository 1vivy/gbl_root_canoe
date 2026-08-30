const MODE_LABELS = {
  0: "Mode 0 - Honest unlocked",
  1: "Mode 1 - ABL fake locked",
  2: "Mode 2 - KM/SPSS profile spoof",
};

export function element(id) {
  const value = document.getElementById(id);
  if (!value) throw new Error(`missing WebUI element: ${id}`);
  return value;
}

function addLine(parent, label, value) {
  const line = document.createElement("div");
  const key = document.createElement("dt");
  const text = document.createElement("dd");
  key.textContent = label;
  text.textContent = value;
  line.append(key, text);
  parent.append(line);
}

function slotText(value) {
  return typeof value === "string" ? value.toUpperCase() : "Unknown";
}

export function renderSlot(response, bootctlAvailable) {
  const active = response.active_slot;
  const inactive = response.inactive_slot;
  element("activeSlot").textContent = slotText(active);
  element("inactiveSlot").textContent = slotText(inactive);
  element("slotSource").textContent = `${response.source || "unknown"}${bootctlAvailable ? " (bootctl ready)" : ""}`;
  element("installedSlots").textContent = Array.isArray(response.installed) && response.installed.length
    ? response.installed.map(slotText).join(", ")
    : "None";
  const chip = element("slotChip");
  chip.textContent = active ? `Active ${slotText(active)}` : "Slot unknown";
  chip.className = `chip ${active ? "chip-success" : "chip-warn"}`;
  element("otaButton").disabled = !inactive || !bootctlAvailable;
  element("otaHint").textContent = bootctlAvailable
    ? "Run the Android system updater first, stay booted, then install to the inactive slot before reboot."
    : "OTA refused: bootctl/GPT metadata is unavailable; the running slot will never be relabelled silently.";
}

function renderEntryDetail(entry) {
  const detail = element("entryDetail");
  while (detail.firstChild) detail.removeChild(detail.firstChild);
  if (!entry) {
    detail.textContent = "Select an entry to inspect its persisted fields.";
    return;
  }
  addLine(detail, "ID", entry.id);
  addLine(detail, "Title", entry.title);
  addLine(detail, "Image", entry.image);
  addLine(detail, "Role", entry.role);
  addLine(detail, "Mode", MODE_LABELS[entry.mode] || String(entry.mode));
  addLine(detail, "Options", entry.options || "-");
}

export function renderEntries(entries, blsEntries, selectedId, defaultId, onSelect) {
  const body = element("entryTableBody");
  while (body.firstChild) body.removeChild(body.firstChild);
  if (!entries.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 5;
    cell.className = "empty-row";
    cell.textContent = "No persisted canoe.cfg entries.";
    row.append(cell);
    body.append(row);
  }
  entries.forEach((entry) => {
    const row = document.createElement("tr");
    row.tabIndex = 0;
    row.dataset.entryId = entry.id;
    [entry.id, entry.title, entry.image, entry.role, MODE_LABELS[entry.mode] || String(entry.mode)].forEach((value) => {
      const cell = document.createElement("td");
      cell.textContent = value;
      row.append(cell);
    });
    row.addEventListener("click", () => onSelect(entry.id));
    row.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") onSelect(entry.id);
    });
    if (entry.id === selectedId) row.classList.add("selected-row");
    body.append(row);
  });

  const blsBody = element("blsTableBody");
  while (blsBody.firstChild) blsBody.removeChild(blsBody.firstChild);
  blsEntries.forEach((file) => {
    const row = document.createElement("tr");
    [file.name, file.entry.title || "-", file.entry.kind, file.entry.image].forEach((value) => {
      const cell = document.createElement("td");
      cell.textContent = value;
      row.append(cell);
    });
    blsBody.append(row);
  });
  if (!blsEntries.length) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 4;
    cell.className = "empty-row";
    cell.textContent = "No BLS entries.";
    row.append(cell);
    blsBody.append(row);
  }

  const defaultSelect = element("defaultSelect");
  while (defaultSelect.firstChild) defaultSelect.removeChild(defaultSelect.firstChild);
  entries.forEach((entry) => {
    const option = document.createElement("option");
    option.value = entry.id;
    option.textContent = `${entry.id} - ${entry.title}`;
    defaultSelect.append(option);
  });
  if (defaultId && entries.some((entry) => entry.id === defaultId)) defaultSelect.value = defaultId;
  renderEntryDetail(entries.find((entry) => entry.id === selectedId) || entries[0]);
}

export function renderMode(entry) {
  const mode = entry ? Number(entry.mode) : 0;
  element("entryModeSelect").value = String(mode);
  element("selectedEntry").textContent = entry ? `${entry.id} - ${entry.title}` : "None selected";
}

export function setBusy(busy) {
  document.querySelectorAll("[data-action]").forEach((node) => {
    node.disabled = busy;
  });
  element("refreshButton").disabled = busy;
}
