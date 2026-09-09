const scBridge = (window as any).aventicsSimilarCad;
const mainBridge = (window as any).aventicsBridge;

const scDefault = {
  type: "similarState",
  busy: false,
  progress: { done: 0, total: 0, indexed: 0, skipped: 0, failed: 0, message: "Similar CAD Search ready." },
  settings: { folder: "", queryImage: "", recursive: false, latest: true, topK: 20 },
  index: { models: 0, views: 0, cachePath: "", engine: "prototype-signature-v1" },
  results: [] as any[]
};

let scState: any = structuredClone(scDefault);
let scDraft: any = structuredClone(scDefault.settings);
let scActive = false;
let scInitialized = false;

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

function syncState(next: any) {
  if (!scInitialized) {
    Object.assign(scDraft, next.settings || {});
    scInitialized = true;
  } else {
    const settings = next.settings || {};
    if (settings.folder && settings.folder !== scState.settings?.folder) scDraft.folder = settings.folder;
    if (settings.queryImage && settings.queryImage !== scState.settings?.queryImage) scDraft.queryImage = settings.queryImage;
    if (typeof settings.recursive === "boolean") scDraft.recursive = settings.recursive;
    if (typeof settings.latest === "boolean") scDraft.latest = settings.latest;
    if (Number(settings.topK) > 0) scDraft.topK = Number(settings.topK);
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
    <div><h3>Similar CAD Search</h3><p>Find Creo parts that visually resemble a screenshot, render, or product image.</p><div class="card-meta">Local index · parts only · no cloud upload</div></div>
    <button class="btn small" data-open-similar>Open</button>`;
  card.querySelector("[data-open-similar]")?.addEventListener("click", () => {
    scActive = true;
    render(true);
  });
  cards.appendChild(card);
}

function markNav() {
  if (!scActive) return;
  document.querySelectorAll(".nav button").forEach(button => button.classList.remove("active"));
  document.querySelector("[data-similar-nav]")?.classList.add("active");
}

function queryPreview() {
  if (!scDraft.queryImage) return `<div class="sc-image-empty"><span>◇</span><strong>No query image</strong><small>PNG, JPG, JPEG, or BMP</small></div>`;
  return `<img class="sc-query-image" src="${esc(fileUrl(scDraft.queryImage))}" alt="Query image" />`;
}

function resultRows() {
  const results = Array.isArray(scState.results) ? scState.results : [];
  if (!results.length) return `<div class="sc-empty-results"><strong>No matches yet</strong><span>Choose an image and library, then run Search.</span></div>`;

  return results.map((result: any, index: number) => `
    <div class="sc-result-row">
      <div class="sc-rank">${index + 1}</div>
      <div class="sc-thumb">${result.previewPath ? `<img src="${esc(fileUrl(result.previewPath))}" alt="${esc(result.modelName)} preview" />` : '<div class="sc-thumb-empty">◇</div>'}</div>
      <div class="sc-result-copy">
        <div class="sc-result-title"><strong>${esc(result.modelName || "Unnamed part")}</strong><span class="sc-score">${Number(result.score || 0).toFixed(1)}</span></div>
        <div class="sc-result-path" title="${esc(result.sourcePath)}">${esc(shortPath(result.sourcePath || ""))}</div>
        <div class="sc-score-help">Visual similarity score · higher is closer</div>
      </div>
      <div class="sc-result-actions">
        <button class="btn small primary" data-sc-open="${esc(result.sourcePath)}" ${disabled(scState.busy)}>Open in Creo</button>
        <button class="btn small" data-sc-locate="${esc(result.sourcePath)}">Locate</button>
      </div>
    </div>`).join("");
}

function pageHtml() {
  const p = scState.progress || {};
  const idx = scState.index || {};
  const total = Number(p.total || 0);
  const percent = total ? Math.max(0, Math.min(100, Math.round(Number(p.done || 0) * 100 / total))) : 0;
  const canSearch = !scState.busy && !!scDraft.folder && !!scDraft.queryImage;

  return `
    <div class="page-header">
      <div><h1 class="page-title">Similar CAD Search</h1><p class="page-subtitle">Search a local Creo part library using a screenshot, render, or product image.</p></div>
      <div class="header-actions"><button class="btn ghost small" data-sc-action="refresh" ${disabled(scState.busy)}>Refresh</button><button class="btn small" data-sc-action="maximize">Full screen</button></div>
    </div>

    <div class="sc-grid">
      <div class="panel sc-query-panel">
        <div class="panel-header"><h3>Query image</h3><span class="muted">2D image → Creo parts</span></div>
        <div class="panel-body"><div class="sc-query-layout">
          <div class="sc-query-preview">${queryPreview()}</div>
          <div class="sc-query-controls"><label>Image</label><div class="input-row"><input class="input" value="${esc(scDraft.queryImage)}" readonly placeholder="Choose an image" /><button class="btn" data-sc-action="chooseImage" ${disabled(scState.busy)}>Choose image</button></div><div class="field-help">Clean part views with little background clutter work best.</div></div>
        </div></div>
      </div>

      <div class="panel sc-library-panel">
        <div class="panel-header"><h3>Creo library</h3><span class="muted">Parts only in v1</span></div>
        <div class="panel-body"><div class="form-grid">
          <div class="field span-12"><label>Source folder</label><div class="input-row"><input class="input" data-sc-draft="folder" value="${esc(scDraft.folder)}" placeholder="Folder containing .prt files" ${disabled(scState.busy)} /><button class="btn" data-sc-action="chooseFolder" ${disabled(scState.busy)}>Browse</button></div></div>
          <div class="field span-8"><span class="field-label">Folder options</span><div class="checks"><label class="check"><input type="checkbox" data-sc-draft="recursive" ${checked(scDraft.recursive)} ${disabled(scState.busy)} />Subfolders</label><label class="check"><input type="checkbox" data-sc-draft="latest" ${checked(scDraft.latest)} ${disabled(scState.busy)} />Latest Creo version only</label></div></div>
          <div class="field span-4 sc-index-action"><button class="btn" data-sc-action="index" ${disabled(scState.busy || !scDraft.folder)}>Build / refresh index</button></div>
        </div>
        <div class="sc-index-meta"><span><strong>${Number(idx.models || 0)}</strong> indexed parts</span><span><strong>${Number(idx.views || 0)}</strong> cached views</span><span>8 views / part</span><span class="sc-engine">Local visual signature v1</span></div>
        </div>
      </div>
    </div>

    ${scState.busy ? `<div class="panel sc-progress-panel"><div class="panel-header"><h3>Indexing</h3><button class="btn small danger" data-sc-action="cancel">Cancel</button></div><div class="panel-body"><div class="sc-progress-copy"><strong>${esc(p.message || "Indexing...")}</strong><span>${Number(p.done || 0)} / ${total}</span></div><div class="progress-track sc-progress-track"><div class="progress-fill" style="width:${percent}%"></div></div><div class="sc-index-meta"><span>${Number(p.indexed || 0)} rebuilt</span><span>${Number(p.skipped || 0)} unchanged</span><span>${Number(p.failed || 0)} failed</span></div></div></div>` : ""}

    <div class="panel sc-results-panel">
      <div class="panel-header"><div><h3>Similar parts</h3><span class="muted">Best matching indexed view determines the part score</span></div><div class="toolbar-right sc-search-actions"><label class="sc-topk-label">Top <input class="input sc-topk" type="number" min="1" max="100" data-sc-draft="topK" value="${esc(scDraft.topK)}" ${disabled(scState.busy)} /></label><button class="btn primary" data-sc-action="search" ${disabled(!canSearch)}>Search</button></div></div>
      <div class="sc-results">${resultRows()}</div>
      <div class="summary-strip"><strong>${Array.isArray(scState.results) ? scState.results.length : 0} matches</strong><span>${esc(p.message || "Ready")}</span><span title="${esc(idx.cachePath || "")}">Local cache${idx.cachePath ? ` · ${esc(shortPath(idx.cachePath))}` : ""}</span></div>
    </div>

    <div class="notice sc-prototype-note"><strong>First-version retrieval engine:</strong> local 8-view image signatures. The versioned index keeps the scorer replaceable so an ONNX vision encoder can be added without changing the Creo/library workflow.</div>`;
}

function bindPage() {
  const page = document.querySelector(".main .page") as HTMLElement | null;
  if (!page) return;

  page.querySelectorAll("[data-sc-draft]").forEach(element => {
    const input = element as HTMLInputElement;
    const key = input.dataset.scDraft || "";
    const update = () => {
      const value: any = input.type === "checkbox" ? input.checked : input.value;
      scDraft[key] = key === "topK" ? Math.max(1, Number(value || 20)) : value;
    };
    input.addEventListener("input", update);
    input.addEventListener("change", update);
  });

  page.querySelectorAll("[data-sc-action]").forEach(element => {
    const button = element as HTMLButtonElement;
    button.onclick = async () => {
      const action = button.dataset.scAction;
      if (action === "refresh") send("refresh");
      if (action === "cancel") send("cancel");
      if (action === "maximize") mainBridge?.postMessage?.("toggleMaximize");
      if (action === "chooseImage") {
        const selected = await scBridge?.chooseImage?.();
        if (selected) { scDraft.queryImage = selected; render(true); }
      }
      if (action === "chooseFolder") {
        const selected = await scBridge?.chooseFolder?.(scDraft.folder || "");
        if (selected) { scDraft.folder = selected; render(true); }
      }
      if (action === "index") send("index", scDraft.folder, scDraft.recursive ? "1" : "0", scDraft.latest ? "1" : "0");
      if (action === "search") send("search", scDraft.folder, scDraft.queryImage, Math.max(1, Number(scDraft.topK || 20)));
    };
  });

  page.querySelectorAll("[data-sc-open]").forEach(element => element.addEventListener("click", () => send("open", (element as HTMLElement).dataset.scOpen || "")));
  page.querySelectorAll("[data-sc-locate]").forEach(element => element.addEventListener("click", () => scBridge?.locate?.((element as HTMLElement).dataset.scLocate || "")));
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
