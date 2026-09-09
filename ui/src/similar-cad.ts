const scBridge = (window as any).aventicsSimilarCad;
const mainBridge = (window as any).aventicsBridge;

const scDefault = {
  type: "similarState",
  busy: false,
  progress: {
    done: 0, total: 0, indexed: 0, skipped: 0, failed: 0,
    currentModel: "", viewDone: 0, viewTotal: 8, currentView: "",
    message: "Similar CAD Search ready."
  },
  settings: { folder: "", queryImage: "", recursive: false, latest: true, topK: 20, autoCrop: true },
  index: {
    models: 0, views: 0, viewCountPerModel: 8, cachePath: "",
    engine: "hybrid-shape-v2", captureProfile: "canonical-matrix-8-v2"
  },
  query: { ready: false, processedPath: "", aspectRatio: 0, fillRatio: 0, autoCrop: true },
  results: [] as any[],
  library: { requested: false, filter: "", page: 1, pageSize: 24, total: 0, items: [] as any[], inspected: null as any }
};

let scState: any = structuredClone(scDefault);
let scDraft: any = structuredClone(scDefault.settings);
let scActive = false;
let scInitialized = false;
let scSection: "search" | "library" = "search";
let scLibraryFilter = "";

function enc(value: unknown) { return encodeURIComponent(String(value ?? "")); }
function esc(value: unknown) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}
function checked(value: unknown) { return value ? "checked" : ""; }
function disabled(value: unknown) { return value ? "disabled" : ""; }
function send(action: string, ...args: unknown[]) {
  scBridge?.postMessage?.([action, ...args.map(enc)].join("\t"));
}
function fileUrl(filePath: string) {
  if (!filePath) return "";
  const normalized = filePath.replaceAll("\\", "/");
  return encodeURI(normalized.match(/^[a-zA-Z]:\//) ? `file:///${normalized}` : `file://${normalized}`);
}
function shortPath(filePath: string) {
  const parts = String(filePath || "").replaceAll("/", "\\").split("\\");
  return parts.length <= 4 ? filePath : `…\\${parts.slice(-4).join("\\")}`;
}
function number(value: unknown, fallback = 0) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function syncState(next: any) {
  if (!scInitialized) {
    Object.assign(scDraft, next.settings || {});
    scLibraryFilter = String(next.library?.filter || "");
    scInitialized = true;
  } else {
    const settings = next.settings || {};
    if (typeof settings.folder === "string" && settings.folder !== scState.settings?.folder) scDraft.folder = settings.folder;
    if (typeof settings.queryImage === "string" && settings.queryImage !== scState.settings?.queryImage) scDraft.queryImage = settings.queryImage;
    if (typeof settings.recursive === "boolean") scDraft.recursive = settings.recursive;
    if (typeof settings.latest === "boolean") scDraft.latest = settings.latest;
    if (typeof settings.autoCrop === "boolean") scDraft.autoCrop = settings.autoCrop;
    if (number(settings.topK) > 0) scDraft.topK = number(settings.topK);
  }
  scState = next;
}

function ensureNav() {
  const nav = document.querySelector(".nav");
  if (!nav || nav.querySelector("[data-similar-nav]")) return;
  const button = document.createElement("button");
  button.dataset.similarNav = "1";
  button.innerHTML = '<span class="nav-icon">◇</span><span>Similar CAD Search</span>';
  button.onclick = event => {
    event.preventDefault();
    scActive = true;
    scSection = "search";
    render(true);
  };
  nav.appendChild(button);
}

function ensureControlPanelCard() {
  if (scActive || document.querySelector(".page-title")?.textContent !== "Control Panel") return;
  const cards = document.querySelector(".cards");
  if (!cards || cards.querySelector("[data-similar-card]")) return;
  const card = document.createElement("div");
  card.className = "card";
  card.dataset.similarCard = "1";
  card.innerHTML = `
    <div class="card-icon">◇</div>
    <div><h3>Similar CAD Search</h3><p>Find and inspect indexed Creo parts from screenshots, renders, or product images.</p><div class="card-meta">Local index · searchable library · no cloud upload</div></div>
    <button class="btn small" data-open-similar>Open</button>`;
  card.querySelector("[data-open-similar]")?.addEventListener("click", () => {
    scActive = true;
    scSection = "search";
    render(true);
  });
  cards.appendChild(card);
}

function markNav() {
  if (!scActive) return;
  document.querySelectorAll(".nav button").forEach(button => button.classList.remove("active"));
  document.querySelector("[data-similar-nav]")?.classList.add("active");
}

function sectionTabs() {
  return `<div class="sc-section-tabs">
    <button class="${scSection === "search" ? "active" : ""}" data-sc-section="search">Search</button>
    <button class="${scSection === "library" ? "active" : ""}" data-sc-section="library">Library <span>${number(scState.index?.models)}</span></button>
  </div>`;
}

function imagePreview(path: string, emptyTitle: string, emptyText: string, className = "") {
  if (!path) return `<div class="sc-image-empty ${className}"><span>◇</span><strong>${esc(emptyTitle)}</strong><small>${esc(emptyText)}</small></div>`;
  return `<img class="sc-query-image ${className}" src="${esc(fileUrl(path))}" alt="${esc(emptyTitle)}" />`;
}

function queryPanel() {
  const query = scState.query || {};
  return `<div class="panel sc-query-panel">
    <div class="panel-header"><div><h3>Query image</h3><span class="muted">Review what the matcher sees before searching</span></div><span class="sc-engine">Hybrid shape v2</span></div>
    <div class="panel-body">
      <div class="sc-query-compare">
        <div class="sc-preview-block"><div class="sc-preview-label">Original</div><div class="sc-query-preview">${imagePreview(scDraft.queryImage, "No query image", "PNG, JPG, JPEG, or BMP")}</div></div>
        <div class="sc-preview-arrow">→</div>
        <div class="sc-preview-block"><div class="sc-preview-label">Normalized</div><div class="sc-query-preview">${imagePreview(query.ready ? query.processedPath : "", "Not processed", "Choose an image or change isolation mode")}</div></div>
      </div>
      <div class="sc-query-controls sc-query-controls-wide">
        <label>Image</label>
        <div class="input-row"><input class="input" value="${esc(scDraft.queryImage)}" readonly placeholder="Choose an image" /><button class="btn" data-sc-action="chooseImage" ${disabled(scState.busy)}>Choose image</button></div>
        <div class="sc-query-options">
          <label class="check"><input type="checkbox" data-sc-draft="autoCrop" ${checked(scDraft.autoCrop)} ${disabled(scState.busy)} />Automatically isolate and center the main object</label>
          ${query.ready ? `<span>Aspect ${number(query.aspectRatio, 1).toFixed(2)} · foreground ${(number(query.fillRatio) * 100).toFixed(0)}%</span>` : ""}
        </div>
      </div>
    </div>
  </div>`;
}

function librarySetupPanel() {
  const idx = scState.index || {};
  return `<div class="panel sc-library-panel">
    <div class="panel-header"><h3>Creo library</h3><span class="muted">Parts only · canonical ${number(idx.viewCountPerModel, 8)}-view capture</span></div>
    <div class="panel-body"><div class="form-grid">
      <div class="field span-12"><label>Source folder</label><div class="input-row"><input class="input" data-sc-draft="folder" value="${esc(scDraft.folder)}" placeholder="Folder containing .prt files" ${disabled(scState.busy)} /><button class="btn" data-sc-action="chooseFolder" ${disabled(scState.busy)}>Browse</button></div></div>
      <div class="field span-8"><span class="field-label">Folder options</span><div class="checks"><label class="check"><input type="checkbox" data-sc-draft="recursive" ${checked(scDraft.recursive)} ${disabled(scState.busy)} />Subfolders</label><label class="check"><input type="checkbox" data-sc-draft="latest" ${checked(scDraft.latest)} ${disabled(scState.busy)} />Latest Creo version only</label></div></div>
      <div class="field span-4 sc-index-action"><button class="btn" data-sc-action="index" ${disabled(scState.busy || !scDraft.folder)}>Build / refresh index</button></div>
    </div>
    <div class="sc-index-meta"><span><strong>${number(idx.models)}</strong> indexed parts</span><span><strong>${number(idx.views)}</strong> cached views</span><span>${esc(idx.captureProfile || "canonical capture")}</span><span class="sc-engine">${esc(idx.engine || "hybrid-shape-v2")}</span></div>
    </div>
  </div>`;
}

function progressPanel() {
  if (!scState.busy) return "";
  const p = scState.progress || {};
  const total = number(p.total);
  const done = number(p.done);
  const percent = total ? Math.max(0, Math.min(100, Math.round(done * 100 / total))) : 0;
  return `<div class="panel sc-progress-panel">
    <div class="panel-header"><div><h3>Indexing</h3><span class="muted">Creo yields between load, orient, raster, and analysis steps</span></div><button class="btn small danger" data-sc-action="cancel">Cancel</button></div>
    <div class="panel-body">
      <div class="sc-progress-copy"><strong>${esc(p.message || "Indexing...")}</strong><span>${done} / ${total} models</span></div>
      <div class="progress-track sc-progress-track"><div class="progress-fill" style="width:${percent}%"></div></div>
      <div class="sc-index-meta"><span>${number(p.indexed)} rebuilt</span><span>${number(p.skipped)} unchanged</span><span>${number(p.failed)} failed</span>${p.currentModel ? `<span>${esc(p.currentModel)} · ${esc(p.currentView || "prepare")} · ${number(p.viewDone)}/${number(p.viewTotal, 8)}</span>` : ""}</div>
    </div>
  </div>`;
}

function resultRows() {
  const results = Array.isArray(scState.results) ? scState.results : [];
  if (!results.length) return `<div class="sc-empty-results"><strong>No matches yet</strong><span>Prepare a query image and search the indexed library.</span></div>`;

  return results.map((result: any, index: number) => `
    <div class="sc-result-row">
      <div class="sc-rank">${index + 1}</div>
      <div class="sc-thumb">${result.previewPath ? `<img src="${esc(fileUrl(result.previewPath))}" alt="${esc(result.modelName)} preview" />` : '<div class="sc-thumb-empty">◇</div>'}</div>
      <div class="sc-result-copy">
        <div class="sc-result-title"><strong>${esc(result.modelName || "Unnamed part")}</strong><span class="sc-score">${number(result.score).toFixed(1)}</span></div>
        <div class="sc-result-path" title="${esc(result.sourcePath)}">${esc(shortPath(result.sourcePath || ""))}</div>
        <div class="sc-score-help">Best view ${esc(result.bestViewName || "-")} · shape ${number(result.shapeScore).toFixed(0)} · edge ${number(result.edgeScore).toFixed(0)} · proportion ${number(result.proportionScore).toFixed(0)}</div>
      </div>
      <div class="sc-result-actions">
        <button class="btn small" data-sc-inspect="${esc(result.sourcePath)}">Views</button>
        <button class="btn small primary" data-sc-open="${esc(result.sourcePath)}" ${disabled(scState.busy)}>Open</button>
        <button class="btn small" data-sc-locate="${esc(result.sourcePath)}">Locate</button>
      </div>
    </div>`).join("");
}

function searchPage() {
  const canSearch = !scState.busy && !!scDraft.folder && !!scDraft.queryImage && !!scState.query?.ready;
  return `${queryPanel()}
    <div style="height:14px"></div>
    ${librarySetupPanel()}
    ${progressPanel()}
    <div class="panel sc-results-panel">
      <div class="panel-header"><div><h3>Similar parts</h3><span class="muted">Multi-view silhouette + edge + proportion reranking</span></div><div class="toolbar-right sc-search-actions"><label class="sc-topk-label">Top <input class="input sc-topk" type="number" min="1" max="100" data-sc-draft="topK" value="${esc(scDraft.topK)}" ${disabled(scState.busy)} /></label><button class="btn primary" data-sc-action="search" ${disabled(!canSearch)}>Search</button></div></div>
      <div class="sc-results">${resultRows()}</div>
      <div class="summary-strip"><strong>${Array.isArray(scState.results) ? scState.results.length : 0} matches</strong><span>${esc(scState.progress?.message || "Ready")}</span><span>Scores are relative similarity, not probability</span></div>
    </div>`;
}

function libraryCards() {
  const library = scState.library || {};
  const items = Array.isArray(library.items) ? library.items : [];
  if (!items.length) return `<div class="sc-empty-results sc-library-empty"><strong>No indexed parts to show</strong><span>${scDraft.folder ? "Build the index or change the library filter." : "Choose an indexed library folder first."}</span></div>`;
  return `<div class="sc-library-grid">${items.map((item: any) => `
    <div class="sc-library-card">
      <button class="sc-library-thumb" data-sc-inspect="${esc(item.sourcePath)}">${item.previewPath ? `<img src="${esc(fileUrl(item.previewPath))}" alt="${esc(item.modelName)}" />` : '<span>◇</span>'}</button>
      <div class="sc-library-copy"><strong title="${esc(item.modelName)}">${esc(item.modelName)}</strong><span title="${esc(item.sourcePath)}">${esc(shortPath(item.sourcePath || ""))}</span><small>${number(item.viewCount)} captured views</small></div>
      <div class="sc-library-actions"><button class="btn small" data-sc-inspect="${esc(item.sourcePath)}">Inspect</button><button class="btn small" data-sc-use-query="${esc(item.previewPath || "")}">Use as query</button><button class="btn small" data-sc-open="${esc(item.sourcePath)}">Open</button><button class="btn small" data-sc-locate="${esc(item.sourcePath)}">Locate</button></div>
    </div>`).join("")}</div>`;
}

function inspectedViews() {
  const inspected = scState.library?.inspected;
  if (!inspected) return "";
  const views = Array.isArray(inspected.views) ? inspected.views : [];
  return `<div class="panel sc-inspector-panel">
    <div class="panel-header"><div><h3>${esc(inspected.modelName)}</h3><span class="muted" title="${esc(inspected.sourcePath)}">${esc(shortPath(inspected.sourcePath || ""))}</span></div><div class="toolbar-right"><button class="btn small" data-sc-open="${esc(inspected.sourcePath)}">Open in Creo</button><button class="btn small" data-sc-locate="${esc(inspected.sourcePath)}">Locate</button><button class="btn small" data-sc-action="closeInspect">Close</button></div></div>
    <div class="panel-body"><div class="sc-view-grid">${views.map((view: any) => `<div class="sc-view-card"><div class="sc-view-name">${esc(view.name)}</div><img src="${esc(fileUrl(view.path))}" alt="${esc(view.name)}" /><div class="sc-view-meta">Aspect ${number(view.aspectRatio, 1).toFixed(2)} · fill ${(number(view.fillRatio) * 100).toFixed(0)}%</div><button class="btn small" data-sc-use-query="${esc(view.path)}">Use this view as query</button></div>`).join("")}</div></div>
  </div>`;
}

function libraryPage() {
  const library = scState.library || {};
  const total = number(library.total);
  const pageSize = Math.max(1, number(library.pageSize, 24));
  const page = Math.max(1, number(library.page, 1));
  const pageCount = Math.max(1, Math.ceil(total / pageSize));
  return `${librarySetupPanel()}
    ${progressPanel()}
    <div class="panel sc-library-browser">
      <div class="panel-header"><div><h3>Indexed library</h3><span class="muted">Search parts and inspect every captured canonical view</span></div><div class="toolbar-right"><input class="input search sc-library-search" data-sc-library-filter value="${esc(scLibraryFilter)}" placeholder="Part name or path" /><button class="btn" data-sc-action="browseLibrary" ${disabled(!scDraft.folder)}>Search</button></div></div>
      <div class="panel-body sc-library-body">${libraryCards()}</div>
      <div class="sc-library-pager"><span>${total} matching parts · page ${page} / ${pageCount}</span><div><button class="btn small" data-sc-action="libraryPrev" ${disabled(page <= 1)}>Previous</button><button class="btn small" data-sc-action="libraryNext" ${disabled(page >= pageCount)}>Next</button></div></div>
    </div>
    ${inspectedViews()}`;
}

function pageHtml() {
  return `<div class="page-header">
      <div><h1 class="page-title">Similar CAD Search</h1><p class="page-subtitle">Capture deterministic Creo views, inspect the index, and retrieve visually similar parts locally.</p></div>
      <div class="header-actions"><button class="btn ghost small" data-sc-action="refresh" ${disabled(scState.busy)}>Refresh</button><button class="btn small" data-sc-action="maximize">Full screen</button></div>
    </div>
    ${sectionTabs()}
    ${scSection === "library" ? libraryPage() : searchPage()}`;
}

function requestLibrary(page = 1) {
  if (!scDraft.folder) return;
  send("browse", scDraft.folder, scLibraryFilter, Math.max(1, page), number(scState.library?.pageSize, 24) || 24);
}

function useImageAsQuery(path: string) {
  if (!path) return;
  scDraft.queryImage = path;
  scDraft.autoCrop = true;
  scSection = "search";
  send("prepareQuery", scDraft.queryImage, "1");
  render(true);
}

function bindPage() {
  const page = document.querySelector(".main .page") as HTMLElement | null;
  if (!page) return;

  page.querySelectorAll("[data-sc-section]").forEach(element => {
    (element as HTMLButtonElement).onclick = () => {
      scSection = ((element as HTMLElement).dataset.scSection || "search") as "search" | "library";
      render(true);
      if (scSection === "library") requestLibrary(1);
    };
  });

  page.querySelectorAll("[data-sc-draft]").forEach(element => {
    const input = element as HTMLInputElement;
    const key = input.dataset.scDraft || "";
    const update = () => {
      const value: any = input.type === "checkbox" ? input.checked : input.value;
      scDraft[key] = key === "topK" ? Math.max(1, number(value, 20)) : value;
      if (key === "autoCrop" && scDraft.queryImage) send("prepareQuery", scDraft.queryImage, scDraft.autoCrop ? "1" : "0");
    };
    input.addEventListener("input", update);
    input.addEventListener("change", update);
  });

  const libraryFilterInput = page.querySelector("[data-sc-library-filter]") as HTMLInputElement | null;
  libraryFilterInput?.addEventListener("input", () => { scLibraryFilter = libraryFilterInput.value; });
  libraryFilterInput?.addEventListener("keydown", event => { if (event.key === "Enter") requestLibrary(1); });

  page.querySelectorAll("[data-sc-action]").forEach(element => {
    const button = element as HTMLButtonElement;
    button.onclick = async () => {
      const action = button.dataset.scAction;
      if (action === "refresh") send("refresh");
      if (action === "cancel") send("cancel");
      if (action === "maximize") mainBridge?.postMessage?.("toggleMaximize");
      if (action === "chooseImage") {
        const selected = await scBridge?.chooseImage?.();
        if (selected) {
          scDraft.queryImage = selected;
          send("prepareQuery", selected, scDraft.autoCrop ? "1" : "0");
          render(true);
        }
      }
      if (action === "chooseFolder") {
        const selected = await scBridge?.chooseFolder?.(scDraft.folder || "");
        if (selected) {
          scDraft.folder = selected;
          render(true);
          if (scSection === "library") requestLibrary(1);
        }
      }
      if (action === "index") send("index", scDraft.folder, scDraft.recursive ? "1" : "0", scDraft.latest ? "1" : "0");
      if (action === "search") send("search", scDraft.folder, scDraft.queryImage, Math.max(1, number(scDraft.topK, 20)), scDraft.autoCrop ? "1" : "0");
      if (action === "browseLibrary") requestLibrary(1);
      if (action === "libraryPrev") requestLibrary(Math.max(1, number(scState.library?.page, 1) - 1));
      if (action === "libraryNext") requestLibrary(number(scState.library?.page, 1) + 1);
      if (action === "closeInspect") send("inspect", "");
    };
  });

  page.querySelectorAll("[data-sc-open]").forEach(element => element.addEventListener("click", () => send("open", (element as HTMLElement).dataset.scOpen || "")));
  page.querySelectorAll("[data-sc-locate]").forEach(element => element.addEventListener("click", () => scBridge?.locate?.((element as HTMLElement).dataset.scLocate || "")));
  page.querySelectorAll("[data-sc-inspect]").forEach(element => element.addEventListener("click", () => {
    const path = (element as HTMLElement).dataset.scInspect || "";
    scSection = "library";
    send("inspect", path);
    if (!scState.library?.requested) requestLibrary(1);
    render(true);
  }));
  page.querySelectorAll("[data-sc-use-query]").forEach(element => element.addEventListener("click", () => useImageAsQuery((element as HTMLElement).dataset.scUseQuery || "")));
}

function render(force = false) {
  if (!scActive) return;
  ensureNav();
  markNav();
  const page = document.querySelector(".main .page") as HTMLElement | null;
  if (!page || (!force && page.dataset.similarPage === "1")) return;
  page.dataset.similarPage = "1";
  page.innerHTML = pageHtml();
  bindPage();
}

function reconcile() {
  ensureNav();
  if (scActive) render(false);
  else ensureControlPanelCard();
}

document.addEventListener("click", event => {
  const target = event.target as Element | null;
  if (target?.closest("button[data-page]")) scActive = false;
}, true);

const root = document.getElementById("app");
if (root) new MutationObserver(reconcile).observe(root, { childList: true, subtree: true });

if (scBridge) {
  scBridge.addEventListener("message", (event: { data: any }) => {
    if (!event.data || event.data.type !== "similarState") return;
    syncState(event.data);
    if (scActive) render(true);
    else ensureControlPanelCard();
  });
  send("ready");
}

reconcile();
