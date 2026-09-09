const defaultState: any = {
  type: "state",
  busy: false,
  operation: "",
  progress: { done: 0, total: 0, message: "Connecting to Creo..." },
  activeModel: { name: "", isAssembly: false },
  settings: {
    weak: { folder: "", useSelection: true, recursive: false, latest: true },
    accuracy: { folder: "", useSelection: true, recursive: false, latest: true, parts: true, assemblies: true },
    inspection: {
      folder: "", recursive: false, latest: true, parts: true, assemblies: true,
      family: true, generic: false, step: true, autoArrange: true, rowsAlongX: false,
      useZ: false, columns: 5, gap: 50
    },
    instances: { folder: "", codes: "", recursive: false, latest: true, columns: 5, gap: 50 }
  },
  weakResults: [],
  accuracyResults: [],
  inspectionResults: [],
  instanceResults: []
};

let state: any = structuredClone(defaultState);
let initialized = false;
const ui: any = {
  page: "overview",
  weakSearch: "",
  weakIssuesOnly: false,
  accuracySearch: "",
  accuracyIssuesOnly: false,
  instanceUnresolvedOnly: false
};
const draft: any = structuredClone(defaultState.settings);

const app: any = document.getElementById("app");
const webview = window.chrome && window.chrome.webview ? window.chrome.webview : null;

function encode(value: any) {
  return encodeURIComponent(String(value == null ? "" : value));
}

function send(action: string, ...args: any[]) {
  if (!webview) return;
  webview.postMessage([action, ...args.map(encode)].join("\t"));
}

function esc(value: any) {
  return String(value == null ? "" : value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function bool(value: any) { return value ? "1" : "0"; }
function checked(value: any) { return value ? "checked" : ""; }
function disabled(value: any) { return value ? "disabled" : ""; }
function lower(value: any) { return String(value || "").toLowerCase(); }

function setPath(root: any, path: string, value: any) {
  const parts = path.split(".");
  let target = root;
  for (let i = 0; i < parts.length - 1; i += 1) target = target[parts[i]];
  target[parts[parts.length - 1]] = value;
}

function syncDraft(next: any) {
  if (!initialized) {
    Object.assign(draft.weak, next.settings.weak || {});
    Object.assign(draft.accuracy, next.settings.accuracy || {});
    Object.assign(draft.inspection, next.settings.inspection || {});
    Object.assign(draft.instances, next.settings.instances || {});
    initialized = true;
    return;
  }

  for (const tool of ["weak", "accuracy", "inspection", "instances"]) {
    const previousFolder = state.settings[tool] ? state.settings[tool].folder : "";
    const nextFolder = next.settings[tool] ? next.settings[tool].folder : "";
    if (previousFolder !== nextFolder) draft[tool].folder = nextFolder;
  }
}

function badge(status: any) {
  const klass = lower(status).replaceAll(" ", "-");
  return `<span class="badge ${esc(klass)}">${esc(status || "-")}</span>`;
}

function progressPercent() {
  if (!state.progress.total) return 0;
  return Math.max(0, Math.min(100, Math.round((state.progress.done / state.progress.total) * 100)));
}

function navButton(page: string, icon: string, label: string) {
  return `<button class="${ui.page === page ? "active" : ""}" data-page="${page}"><span class="nav-icon">${icon}</span><span>${label}</span></button>`;
}

function header(title: string, subtitle: string) {
  return `
    <div class="page-header">
      <div>
        <h1 class="page-title">${title}</h1>
        <p class="page-subtitle">${subtitle}</p>
      </div>
      <div class="header-actions">
        <button class="btn ghost small" data-action="refresh" ${disabled(state.busy)}>Refresh</button>
        <button class="btn small" data-action="maximize">Full screen</button>
      </div>
    </div>`;
}

function emptyRow(columns: number, message: string) {
  return `<tr><td colspan="${columns}"><div class="empty">${esc(message)}</div></td></tr>`;
}

function pageOverview() {
  const weakIssues = state.weakResults.filter((r: any) => lower(r.status) !== "pass").length;
  const accuracyIssues = state.accuracyResults.filter((r: any) => lower(r.status) !== "pass").length;
  const inspectionAdded = state.inspectionResults.filter((r: any) => lower(r.status) === "added").length;
  const instancesAdded = state.instanceResults.filter((r: any) => lower(r.status) === "added").length;
  const activeText = state.activeModel.name
    ? `Active model: ${esc(state.activeModel.name)}${state.activeModel.isAssembly ? " · Assembly tools ready" : " · Open an assembly for builder tools"}`
    : "No active model. QC can use Creo selection or folders; builder tools require an active assembly.";

  return `
    ${header("Control Panel", "A focused TypeScript front end over the existing native Creo TOOLKIT backend.")}
    <div class="hero">
      <div>
        <h2>Creo session</h2>
        <p>${activeText}</p>
      </div>
      <button class="btn" data-action="runAll" ${disabled(state.busy)}>Run QC on selection</button>
    </div>
    <div class="cards">
      <div class="card">
        <div class="card-icon">∿</div>
        <div><h3>Weak Dimensions</h3><p>Find weak sketch dimensions in selected parts or a folder.</p><div class="card-meta">${state.weakResults.length} parts · ${weakIssues} with issues</div></div>
        <button class="btn small" data-page="weak">Open</button>
      </div>
      <div class="card">
        <div class="card-icon">◎</div>
        <div><h3>Accuracy</h3><p>Review absolute and relative accuracy across parts and assemblies.</p><div class="card-meta">${state.accuracyResults.length} models · ${accuracyIssues} with issues</div></div>
        <button class="btn small" data-page="accuracy">Open</button>
      </div>
      <div class="card">
        <div class="card-icon">⊞</div>
        <div><h3>Inspection Builder</h3><p>Add discovered Creo and STEP sources into the active inspection assembly.</p><div class="card-meta">${inspectionAdded} components added · no auto-save</div></div>
        <button class="btn small" data-page="inspection">Open</button>
      </div>
      <div class="card">
        <div class="card-icon">▦</div>
        <div><h3>Instance Builder</h3><p>Resolve family-table instance codes and assemble them into a planned grid.</p><div class="card-meta">${instancesAdded} instances added · ${state.instanceResults.length} requested</div></div>
        <button class="btn small" data-page="instances">Open</button>
      </div>
    </div>`;
}

function weakRows() {
  const rows: any[] = [];
  state.weakResults.forEach((result: any) => {
    if (result.findings && result.findings.length) {
      result.findings.forEach((finding: any) => rows.push({ result, finding }));
    } else {
      rows.push({ result, finding: null });
    }
  });
  return rows.filter((row: any) => {
    const finding = row.finding;
    const status = finding ? finding.status : row.result.status;
    if (ui.weakIssuesOnly && lower(status) === "pass") return false;
    if (!ui.weakSearch) return true;
    const haystack = [row.result.modelName, row.result.sourcePath, row.result.summary,
      finding && finding.featureName, finding && finding.details].join(" ").toLowerCase();
    return haystack.includes(ui.weakSearch.toLowerCase());
  });
}

function pageWeak() {
  const rows = weakRows();
  const totalWeak = state.weakResults.reduce((sum: number, result: any) => sum + Number(result.weakDimensions || 0), 0);
  const issueParts = state.weakResults.filter((result: any) => lower(result.status) !== "pass").length;
  const selection = !!draft.weak.useSelection;
  return `
    ${header("Weak Dimensions", "Check sketch dimensions without changing or saving source models.")}
    <div class="panel">
      <div class="panel-header"><h3>Source</h3><span class="muted">Parts only</span></div>
      <div class="panel-body">
        <div class="form-grid">
          <div class="field span-12">
            <span class="field-label">Mode</span>
            <div class="segmented">
              <button data-mode="weak:selection" class="${selection ? "active" : ""}" ${disabled(state.busy)}>Creo selection</button>
              <button data-mode="weak:folder" class="${!selection ? "active" : ""}" ${disabled(state.busy)}>Folder</button>
            </div>
          </div>
          <div class="field span-8">
            <label>Folder</label>
            <div class="input-row">
              <input class="input" data-draft="weak.folder" value="${esc(draft.weak.folder)}" ${disabled(state.busy || selection)} />
              <button class="btn" data-action="browseWeak" ${disabled(state.busy || selection)}>Browse</button>
            </div>
          </div>
          <div class="field span-4">
            <span class="field-label">Options</span>
            <div class="checks">
              <label class="check"><input type="checkbox" data-draft="weak.recursive" ${checked(draft.weak.recursive)} ${disabled(state.busy || selection)} />Subfolders</label>
              <label class="check"><input type="checkbox" data-draft="weak.latest" ${checked(draft.weak.latest)} ${disabled(state.busy || selection)} />Latest Creo version only</label>
            </div>
          </div>
          <div class="span-12 toolbar">
            <div class="toolbar-left"><span class="notice">Folder runs unload models opened only for checking.</span></div>
            <div class="toolbar-right"><button class="btn primary" data-action="runWeak" ${disabled(state.busy)}>Run check</button></div>
          </div>
        </div>
      </div>
    </div>
    <div class="panel">
      <div class="panel-header">
        <h3>Results</h3>
        <div class="toolbar-right">
          <input class="input search" placeholder="Search results" data-filter="weakSearch" value="${esc(ui.weakSearch)}" />
          <label class="check"><input type="checkbox" data-filter-check="weakIssuesOnly" ${checked(ui.weakIssuesOnly)} />Issues only</label>
          <button class="btn small" data-action="clearWeak" ${disabled(state.busy || !state.weakResults.length)}>Clear</button>
        </div>
      </div>
      <div class="table-wrap"><table><thead><tr><th>Part</th><th>Status</th><th>Feature</th><th>Section</th><th>Dimension</th><th>Details</th></tr></thead><tbody>
        ${rows.length ? rows.map(({result, finding}: any) => `<tr>
          <td class="mono">${esc(result.modelName)}</td>
          <td>${badge(finding ? finding.status : result.status)}</td>
          <td>${finding ? esc((finding.featureName || "-") + (finding.featureId >= 0 ? ` #${finding.featureId}` : "")) : "-"}</td>
          <td>${finding && finding.sectionIndex >= 0 ? finding.sectionIndex : "-"}</td>
          <td>${finding && finding.dimensionId >= 0 ? `sd${finding.dimensionId}` : "-"}</td>
          <td class="details">${esc(finding ? finding.details : result.summary)}</td>
        </tr>`).join("") : emptyRow(6, state.weakResults.length ? "No matching results" : "No results yet")}
      </tbody></table></div>
      <div class="summary-strip"><strong>${state.weakResults.length} parts</strong><span>${issueParts} with issues</span><span>${totalWeak} weak dimensions</span><span>${rows.length} rows shown</span></div>
    </div>`;
}

function accuracyRows() {
  return state.accuracyResults.filter((result: any) => {
    if (ui.accuracyIssuesOnly && lower(result.status) === "pass") return false;
    if (!ui.accuracySearch) return true;
    return [result.modelName, result.sourcePath, result.kind, result.accuracyType, result.details]
      .join(" ").toLowerCase().includes(ui.accuracySearch.toLowerCase());
  });
}

function pageAccuracy() {
  const rows = accuracyRows();
  const selection = !!draft.accuracy.useSelection;
  const issues = state.accuracyResults.filter((result: any) => lower(result.status) !== "pass").length;
  return `
    ${header("Accuracy", "Inspect model accuracy across parts and assemblies while keeping source models unchanged.")}
    <div class="panel">
      <div class="panel-header"><h3>Source</h3><span class="muted">Parts and assemblies</span></div>
      <div class="panel-body"><div class="form-grid">
        <div class="field span-12"><span class="field-label">Mode</span><div class="segmented">
          <button data-mode="accuracy:selection" class="${selection ? "active" : ""}" ${disabled(state.busy)}>Creo selection</button>
          <button data-mode="accuracy:folder" class="${!selection ? "active" : ""}" ${disabled(state.busy)}>Folder</button>
        </div></div>
        <div class="field span-8"><label>Folder</label><div class="input-row">
          <input class="input" data-draft="accuracy.folder" value="${esc(draft.accuracy.folder)}" ${disabled(state.busy || selection)} />
          <button class="btn" data-action="browseAccuracy" ${disabled(state.busy || selection)}>Browse</button>
        </div></div>
        <div class="field span-4"><span class="field-label">Model types</span><div class="checks">
          <label class="check"><input type="checkbox" data-draft="accuracy.parts" ${checked(draft.accuracy.parts)} ${disabled(state.busy)} />Parts</label>
          <label class="check"><input type="checkbox" data-draft="accuracy.assemblies" ${checked(draft.accuracy.assemblies)} ${disabled(state.busy)} />Assemblies</label>
        </div></div>
        <div class="field span-8"><span class="field-label">Folder options</span><div class="checks">
          <label class="check"><input type="checkbox" data-draft="accuracy.recursive" ${checked(draft.accuracy.recursive)} ${disabled(state.busy || selection)} />Subfolders</label>
          <label class="check"><input type="checkbox" data-draft="accuracy.latest" ${checked(draft.accuracy.latest)} ${disabled(state.busy || selection)} />Latest Creo version only</label>
        </div></div>
        <div class="span-4 toolbar"><span></span><button class="btn primary" data-action="runAccuracy" ${disabled(state.busy)}>Run check</button></div>
      </div></div>
    </div>
    <div class="panel">
      <div class="panel-header"><h3>Results</h3><div class="toolbar-right">
        <input class="input search" placeholder="Search results" data-filter="accuracySearch" value="${esc(ui.accuracySearch)}" />
        <label class="check"><input type="checkbox" data-filter-check="accuracyIssuesOnly" ${checked(ui.accuracyIssuesOnly)} />Issues only</label>
        <button class="btn small" data-action="clearAccuracy" ${disabled(state.busy || !state.accuracyResults.length)}>Clear</button>
      </div></div>
      <div class="table-wrap"><table><thead><tr><th>Model</th><th>Kind</th><th>Accuracy type</th><th>Accuracy</th><th>Status</th><th>Details</th></tr></thead><tbody>
        ${rows.length ? rows.map((result: any) => `<tr><td class="mono">${esc(result.modelName)}</td><td>${esc(result.kind || "-")}</td><td>${esc(result.accuracyType || "-")}</td><td class="mono">${result.hasValue ? esc(result.accuracyValue) : "-"}</td><td>${badge(result.status)}</td><td class="details">${esc(result.details)}</td></tr>`).join("") : emptyRow(6, state.accuracyResults.length ? "No matching results" : "No results yet")}
      </tbody></table></div>
      <div class="summary-strip"><strong>${state.accuracyResults.length} models</strong><span>${issues} with issues</span><span>${rows.length} shown</span></div>
    </div>`;
}

function pageInspection() {
  const added = state.inspectionResults.filter((r: any) => lower(r.status) === "added").length;
  const problems = state.inspectionResults.filter((r: any) => !["added", "skipped"].includes(lower(r.status))).length;
  const autoArrange = !!draft.inspection.autoArrange;
  return `
    ${header("Inspection Builder", "Build an inspection assembly from Creo and STEP sources. Components are added to the active assembly and are never auto-saved.")}
    ${!state.activeModel.isAssembly ? `<div class="notice warning" style="margin-bottom:14px">Open or create a Creo assembly before building inspection components.</div>` : ""}
    <div class="panel"><div class="panel-header"><h3>Build setup</h3><span class="muted">Active: ${esc(state.activeModel.name || "No assembly")}</span></div>
      <div class="panel-body"><div class="form-grid">
        <div class="field span-8"><label>Source folder</label><div class="input-row"><input class="input" data-draft="inspection.folder" value="${esc(draft.inspection.folder)}" ${disabled(state.busy)} /><button class="btn" data-action="browseInspection" ${disabled(state.busy)}>Browse</button></div></div>
        <div class="field span-4"><span class="field-label">Folder options</span><div class="checks"><label class="check"><input type="checkbox" data-draft="inspection.recursive" ${checked(draft.inspection.recursive)} ${disabled(state.busy)} />Subfolders</label><label class="check"><input type="checkbox" data-draft="inspection.latest" ${checked(draft.inspection.latest)} ${disabled(state.busy)} />Latest only</label></div></div>
        <div class="field span-12"><span class="field-label">Sources</span><div class="checks">
          <label class="check"><input type="checkbox" data-draft="inspection.parts" ${checked(draft.inspection.parts)} ${disabled(state.busy)} />Creo parts</label>
          <label class="check"><input type="checkbox" data-draft="inspection.assemblies" ${checked(draft.inspection.assemblies)} ${disabled(state.busy)} />Creo assemblies</label>
          <label class="check"><input type="checkbox" data-draft="inspection.family" ${checked(draft.inspection.family)} ${disabled(state.busy)} />Family instances</label>
          <label class="check"><input type="checkbox" data-draft="inspection.generic" ${checked(draft.inspection.generic)} ${disabled(state.busy)} />Include generic</label>
          <label class="check"><input type="checkbox" data-draft="inspection.step" ${checked(draft.inspection.step)} ${disabled(state.busy)} />STEP</label>
        </div></div>
      </div></div>
    <div class="panel"><div class="panel-header"><h3>Placement</h3><span class="muted">Unconstrained grid placement</span></div><div class="panel-body"><div class="form-grid">
      <div class="field span-3"><label>Columns</label><input class="input" type="number" min="1" data-draft="inspection.columns" value="${esc(draft.inspection.columns)}" ${disabled(state.busy || !autoArrange)} /></div>
      <div class="field span-3"><label>Gap</label><input class="input" type="number" min="0" step="any" data-draft="inspection.gap" value="${esc(draft.inspection.gap)}" ${disabled(state.busy || !autoArrange)} /></div>
      <div class="field span-6"><span class="field-label">Layout</span><div class="checks">
        <label class="check"><input type="checkbox" data-draft="inspection.autoArrange" ${checked(draft.inspection.autoArrange)} ${disabled(state.busy)} />Auto arrange</label>
        <label class="check"><input type="checkbox" data-draft="inspection.rowsAlongX" ${checked(draft.inspection.rowsAlongX)} ${disabled(state.busy || !autoArrange)} />Rows advance along X</label>
        <label class="check"><input type="checkbox" data-draft="inspection.useZ" ${checked(draft.inspection.useZ)} ${disabled(state.busy || !autoArrange)} />Use X-Z plane</label>
      </div></div>
      <div class="span-12 toolbar"><span class="notice">Existing assembly contents remain untouched. New components are not automatically constrained or saved.</span><button class="btn primary" data-action="runInspection" ${disabled(state.busy || !state.activeModel.isAssembly)}>Build components</button></div>
    </div></div></div>
    <div class="panel"><div class="panel-header"><h3>Results</h3><button class="btn small" data-action="clearInspection" ${disabled(state.busy || !state.inspectionResults.length)}>Clear</button></div>
      <div class="table-wrap"><table><thead><tr><th>Source</th><th>Kind</th><th>Added model</th><th>Status</th><th>Feature</th><th>Details</th></tr></thead><tbody>
      ${state.inspectionResults.length ? state.inspectionResults.map((result: any) => `<tr><td class="mono">${esc(result.sourceName)}</td><td>${esc(result.sourceKind)}</td><td class="mono">${esc(result.addedModelName || "-")}</td><td>${badge(result.status)}</td><td>${result.featureId >= 0 ? result.featureId : "-"}</td><td class="details">${esc(result.details)}</td></tr>`).join("") : emptyRow(6, "No build results yet")}
      </tbody></table></div><div class="summary-strip"><strong>${state.inspectionResults.length} results</strong><span>${added} added</span><span>${problems} warnings / failures</span></div>
    </div>`;
}

function pageInstances() {
  const rows = state.instanceResults.filter((result: any) => !ui.instanceUnresolvedOnly || ["not found", "failed", "skipped"].includes(lower(result.status)));
  const added = state.instanceResults.filter((r: any) => lower(r.status) === "added").length;
  const unresolved = state.instanceResults.filter((r: any) => ["not found", "failed", "skipped"].includes(lower(r.status))).length;
  return `
    ${header("Instance Builder", "Paste instance codes, review their grid allocation, then resolve exact family-table instances and assemble them into the active assembly.")}
    ${!state.activeModel.isAssembly ? `<div class="notice warning" style="margin-bottom:14px">Open or create a Creo assembly before building instances.</div>` : ""}
    <div class="panel"><div class="panel-header"><h3>Source and requests</h3><span class="muted">Active: ${esc(state.activeModel.name || "No assembly")}</span></div><div class="panel-body"><div class="form-grid">
      <div class="field span-8"><label>Source folder</label><div class="input-row"><input class="input" data-draft="instances.folder" value="${esc(draft.instances.folder)}" ${disabled(state.busy)} /><button class="btn" data-action="browseInstances" ${disabled(state.busy)}>Browse</button></div></div>
      <div class="field span-4"><span class="field-label">Folder options</span><div class="checks"><label class="check"><input type="checkbox" data-draft="instances.recursive" ${checked(draft.instances.recursive)} ${disabled(state.busy)} />Subfolders</label><label class="check"><input type="checkbox" data-draft="instances.latest" ${checked(draft.instances.latest)} ${disabled(state.busy)} />Latest only</label></div></div>
      <div class="field span-8"><label>Instance codes</label><textarea class="textarea" data-draft="instances.codes" placeholder="One code per line, or separate codes with commas, semicolons, or tabs" ${disabled(state.busy)}>${esc(draft.instances.codes)}</textarea></div>
      <div class="field span-2"><label>Columns</label><input class="input" type="number" min="1" data-draft="instances.columns" value="${esc(draft.instances.columns)}" ${disabled(state.busy)} /></div>
      <div class="field span-2"><label>Gap</label><input class="input" type="number" min="0" step="any" data-draft="instances.gap" value="${esc(draft.instances.gap)}" ${disabled(state.busy)} /></div>
      <div class="span-12 toolbar"><span class="notice">Missing instances keep their planned position empty; later requests retain their original grid locations.</span><div class="toolbar-right"><button class="btn" data-action="planInstances" ${disabled(state.busy)}>Plan positions</button><button class="btn primary" data-action="runInstances" ${disabled(state.busy || !state.activeModel.isAssembly)}>Build instances</button></div></div>
    </div></div></div>
    <div class="panel"><div class="panel-header"><h3>Plan / results</h3><div class="toolbar-right"><label class="check"><input type="checkbox" data-filter-check="instanceUnresolvedOnly" ${checked(ui.instanceUnresolvedOnly)} />Unresolved only</label><button class="btn small" data-action="exportInstances" ${disabled(state.busy || !state.instanceResults.length)}>Export CSV</button><button class="btn small" data-action="clearInstances" ${disabled(state.busy || !state.instanceResults.length)}>Clear</button></div></div>
      <div class="table-wrap"><table><thead><tr><th>Code</th><th>Row</th><th>Column</th><th>Generic</th><th>Added model</th><th>Status</th><th>Details</th></tr></thead><tbody>
      ${rows.length ? rows.map((result: any) => `<tr><td class="mono">${esc(result.code)}</td><td>${result.row}</td><td>${result.column}</td><td class="mono">${esc(result.genericName || "-")}</td><td class="mono">${esc(result.addedModelName || "-")}</td><td>${badge(result.status)}</td><td class="details">${esc(result.details)}</td></tr>`).join("") : emptyRow(7, state.instanceResults.length ? "No unresolved results" : "No plan yet")}
      </tbody></table></div><div class="summary-strip"><strong>${state.instanceResults.length} requested</strong><span>${added} added</span><span>${unresolved} unresolved</span><span>${rows.length} shown</span></div>
    </div>`;
}

function currentPage() {
  if (ui.page === "weak") return pageWeak();
  if (ui.page === "accuracy") return pageAccuracy();
  if (ui.page === "inspection") return pageInspection();
  if (ui.page === "instances") return pageInstances();
  return pageOverview();
}

function render() {
  const percent = progressPercent();
  app.innerHTML = `
    <div class="shell">
      <aside class="sidebar">
        <div class="brand"><div class="brand-mark">A</div><div class="brand-copy"><strong>Aventics Toolbox</strong><span>TypeScript UI experiment</span></div></div>
        <nav class="nav">
          ${navButton("overview", "⌂", "Control Panel")}
          ${navButton("weak", "∿", "Weak Dimensions")}
          ${navButton("accuracy", "◎", "Accuracy")}
          ${navButton("inspection", "⊞", "Inspection Builder")}
          ${navButton("instances", "▦", "Instance Builder")}
        </nav>
        <div class="sidebar-footer"><div class="model-label">Creo context</div><div class="model-name" title="${esc(state.activeModel.name)}">${esc(state.activeModel.name || "No active model")}</div></div>
      </aside>
      <main class="main"><div class="page">${currentPage()}</div></main>
      <div class="statusbar">
        <div class="status-text" title="${esc(state.progress.message)}">${esc(state.progress.message || (webview ? "Ready" : "WebView bridge unavailable"))}</div>
        <div class="progress-track"><div class="progress-fill" style="width:${percent}%"></div></div>
        <button class="btn small danger" data-action="cancel" ${disabled(!state.busy)}>Cancel</button>
      </div>
    </div>`;
}

function applyProgress(next: any) {
  state.busy = !!next.busy;
  state.operation = next.operation || "";
  state.progress = next.progress || state.progress;
  const text = app.querySelector(".status-text");
  if (text) {
    text.textContent = state.progress.message || "Ready";
    text.title = state.progress.message || "Ready";
  }
  const fill = app.querySelector(".progress-fill");
  if (fill) fill.style.width = `${progressPercent()}%`;
  const cancel = app.querySelector('[data-action="cancel"]');
  if (cancel) cancel.disabled = !state.busy;
}

function renderFilterAndRestore(filterKey: string, value: string, caret: number | null) {
  ui[filterKey] = value;
  render();
  const replacement = app.querySelector(`[data-filter="${filterKey}"]`);
  if (replacement) {
    replacement.focus();
    if (caret != null && replacement.setSelectionRange) replacement.setSelectionRange(caret, caret);
  }
}

function runWeak() {
  send("runWeak", bool(draft.weak.useSelection), draft.weak.folder, bool(draft.weak.recursive), bool(draft.weak.latest));
}

function runAccuracy() {
  send("runAccuracy", bool(draft.accuracy.useSelection), draft.accuracy.folder, bool(draft.accuracy.recursive),
    bool(draft.accuracy.latest), bool(draft.accuracy.parts), bool(draft.accuracy.assemblies));
}

function runInspection() {
  send("runInspection", draft.inspection.folder, bool(draft.inspection.recursive), bool(draft.inspection.latest),
    bool(draft.inspection.parts), bool(draft.inspection.assemblies), bool(draft.inspection.family),
    bool(draft.inspection.generic), bool(draft.inspection.step), bool(draft.inspection.autoArrange),
    bool(draft.inspection.rowsAlongX), bool(draft.inspection.useZ), draft.inspection.columns, draft.inspection.gap);
}

function planInstances() {
  send("planInstances", draft.instances.codes, draft.instances.columns);
}

function runInstances() {
  send("runInstances", draft.instances.folder, bool(draft.instances.recursive), bool(draft.instances.latest),
    draft.instances.codes, draft.instances.columns, draft.instances.gap);
}

app.addEventListener("click", (event: any) => {
  const target = event.target && event.target.closest ? event.target.closest("button") : null;
  if (!target) return;
  if (target.dataset.page) {
    ui.page = target.dataset.page;
    render();
    return;
  }
  if (target.dataset.mode) {
    const [tool, mode] = target.dataset.mode.split(":");
    draft[tool].useSelection = mode === "selection";
    render();
    return;
  }
  const action = target.dataset.action;
  if (!action) return;
  if (action === "refresh") send("refresh");
  else if (action === "maximize") send("toggleMaximize");
  else if (action === "cancel") send("cancel");
  else if (action === "runAll") send("runAll");
  else if (action === "browseWeak") send("pickFolder", "weak", draft.weak.folder);
  else if (action === "browseAccuracy") send("pickFolder", "accuracy", draft.accuracy.folder);
  else if (action === "browseInspection") send("pickFolder", "inspection", draft.inspection.folder);
  else if (action === "browseInstances") send("pickFolder", "instances", draft.instances.folder);
  else if (action === "runWeak") runWeak();
  else if (action === "runAccuracy") runAccuracy();
  else if (action === "runInspection") runInspection();
  else if (action === "planInstances") planInstances();
  else if (action === "runInstances") runInstances();
  else if (action === "exportInstances") send("exportInstances");
  else if (action === "clearWeak") send("clear", "weak");
  else if (action === "clearAccuracy") send("clear", "accuracy");
  else if (action === "clearInspection") send("clear", "inspection");
  else if (action === "clearInstances") send("clear", "instances");
});

app.addEventListener("input", (event: any) => {
  const target = event.target;
  if (target.dataset && target.dataset.draft) {
    const value = target.type === "checkbox" ? target.checked : target.value;
    setPath(draft, target.dataset.draft, value);
  }
  if (target.dataset && target.dataset.filter) {
    renderFilterAndRestore(target.dataset.filter, target.value, target.selectionStart);
  }
});

app.addEventListener("change", (event: any) => {
  const target = event.target;
  if (target.dataset && target.dataset.draft) {
    const value = target.type === "checkbox" ? target.checked : target.value;
    setPath(draft, target.dataset.draft, value);
    if (target.type === "checkbox") render();
  }
  if (target.dataset && target.dataset.filterCheck) {
    ui[target.dataset.filterCheck] = target.checked;
    render();
  }
});

if (webview) {
  webview.addEventListener("message", (event: MessageEvent) => {
    try {
      const next: any = typeof event.data === "string" ? JSON.parse(event.data) : event.data;
      if (!next) return;
      if (next.type === "progress") {
        applyProgress(next);
        return;
      }
      if (next.type !== "state") return;
      syncDraft(next);
      state = next;
      render();
    } catch (error) {
      console.error("Aventics Toolbox bridge message failed", error);
    }
  });
  send("ready");
} else {
  state.progress.message = "WebView bridge unavailable — UI preview only";
}

render();
