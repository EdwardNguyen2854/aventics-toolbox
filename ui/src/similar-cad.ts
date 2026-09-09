const similarBridge = (window as any).aventicsSimilarCad;

const similarDefaultState = {
  type: "similarState",
  busy: false,
  progress: { done: 0, total: 0, indexed: 0, skipped: 0, failed: 0, message: "Similar CAD Search ready." },
  settings: { folder: "", queryImage: "", recursive: false, latest: true, topK: 20 },
  index: { models: 0, views: 0, cachePath: "", engine: "prototype-signature-v1" },
  results: [] as any[]
};

let similarState: any = structuredClone(similarDefaultState);
let similarActive = false;
let similarInitialized = false;
let similarDraft = structuredClone(similarDefaultState.settings);

function scEncode(value: unknown) {
  return encodeURIComponent(String(value == null ? "" : value));
}

function scSend(action: string, ...args: unknown[]) {
  if (!similarBridge) return;
  similarBridge.postMessage([action, ...args.map(scEncode)].join("\t"));
}

function scEsc(value: unknown) {
  return String(value == null ? "" : value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function scChecked(value: unknown) { return value ? "checked" : ""; }
function scDisabled(value: unknown) { return value ? "disabled" : ""; }

function scFileUrl(filePath: string) {
  if (!filePath) return "";
  const normalized = filePath.replaceAll("\\", "/");
  return encodeURI(normalized.match(/^[a-zA-Z]:\//) ? `file:///${normalized}` : `file://${normalized}`);
}

function scShortPath(filePath: string) {
  if (!filePath) return "";
  const parts = filePath.replaceAll("/", "\\").split("\\");
  if (parts.length <= 4) return filePath;
  return `…\\${parts.slice(-4).join("\\")}`;
}

function scPercent() {
  const total = Number(similarState.progress?.total || 0);
  const done = Number(similarState.progress?.done || 0);
  return total ? Math.max(0, Math.min(100, Math.round((done / total) * 100))) : 0;
}

function scSyncState(next: any) {
  if (!similarInitialized) {
    Object.assign(similarDraft, next.settings || {});
    similarInitialized = true;
  } else {
    const settings = next.settings || {};
    if (settings.folder && settings.folder !== similarState.settings?.folder) similarDraft.folder = settings.folder;
    if (settings.queryImage && settings.queryImage !== similarState.settings?.queryImage) similarDraft.queryImage = settings.queryImage;
    if (typeof settings.recursive === "boolean") similarDraft.recursive = settings.recursive;
    if (typeof settings.latest === "boolean") similarDraft.latest = settings.latest;
    if (Number(settings.topK) > 0) similarDraft.topK = Number(settings.topK);
  }
  similarState = next;
}

function scEnsureNav() {
  const nav = document.querySelector(".nav");
  if (!nav || nav.querySelector("[data-similar-nav]")) return;
  const button = document.createElement("button");
  button.dataset.similarNav = "1";
  button.innerHTML = '<span class="nav-icon">◇</span><span>Similar CAD Search</span>';
  button.addEventListener("click", event => {
    event.preventDefault();
    similarActive = true;
    scRenderPage(true);
  });
  nav.appendChild(button);
}

function scEnsureOverviewCard() {
  if (similarActive) return;
  const title = document.querySelector(".page-title")?.textContent || "";
  const cards = document.querySelector(".cards");
  if (title !== "Control Panel" || !cards || cards.querySelector("[data-similar-card]")) return;

  const card = document.createElement("div");
  card.className = "card";
  card.dataset.similarCard = "1";
  card.innerHTML = `
    <div class="card-icon">◇</div>
    <div>
      <h3>Similar CAD Search</h3>
      <p>Find Creo parts that visually resemble a screenshot, render, or product image.</p>
      <div class="card-meta">Local index · parts only · no cloud upload</div>
    </div>
    <button class="btn small" data-open-similar>Open</button>`;
  card.querySelector("[data-open-similar]")?.addEventListener("click", () => {
    similarActive = true;
    scRenderPage(true);
  });
  cards.appendChild(card);
}

function scMarkNavActive() {
  if (!similarActive) return;
  document.querySelectorAll(".nav button").forEach(button => button.classList.remove("active"));
  document.querySelector("[data-similar-nav]")?.classList.add("active");
}

function scQueryPreview() {
  const path = String(similarDraft.queryImage || "");
  if (!path) {
    return `<div class="sc-image-empty"><span>◇</span><strong>No query image</strong><small>PNG, JPG, JPEG, or BMP</small></div>`;
  }
  return `<img class="sc-query-image" src="${scEsc(scFileUrl(path))}" alt="Query image" />`;
}

function scResultRows() {
  const results = Array.isArray(similarState.results) ? similarState.results : [];
  if (!results.length) {
    return `<div class="sc-empty-results"><strong>No matches yet</strong><span>Build an index, choose an image, then run Search.</span></div>`;
  }

  return results.map((result: any, index: number) => {
    const preview = result.previewPath
      ? `<img src="${scEsc(scFileUrl(String(result.previewPath)))}" alt="${scEsc(result.modelName)} preview" />`
      : `<div class="sc-thumb-empty">◇</div>`;
    const score = Number(result.score || 0).toFixed(1);
    return `<div class="sc-result-row">
      <div class="sc-rank">${index + 1}</div>
      <div class="sc-thumb">${preview}</div>
      <div class="sc-result-copy">
        <div class="sc-result-title"><strong>${scEsc(result.modelName || "Unnamed part")}</strong><span class="sc-score">${score}</span></div>
        <div class="sc-result-path" title="${scEsc(result.sourcePath)}">${scEsc(scShortPath(String(result.sourcePath || "")))}</div>
        <div class="sc-score-help">Visual similarity score · higher is closer</div>
      </div>
      <div class="sc-result-actions">
        <button class="btn small primary" data-sc-open="${scEsc(result.sourcePath)}" ${scDisabled(similarState.busy)}>Open in Creo</button>
        <button class="btn small" data-sc-locate="${scEsc(result.sourcePath)}">Locate</button>
      </div>
    </div>`;
  }).join("");
}

function scPageHtml() {
  const progress = similarState.progress || {};
  const index = similarState.index || {};
  const percent = scPercent();
  const queryPath = String(similarDraft.queryImage || "");
  const folder = String(similarDraft.folder || "");
  const canSearch = !similarState.busy && !!queryPath && !!folder && Number(index.models || 0) > 0;

  return `
    <div class="page-header">
      <div>
        <h1 class="page-title">Similar CAD Search</h1>
        <p class="page-subtitle">Search a local Creo part library using a screenshot, render, or product image.</p>
      </div>
      <div class="header-actions">
        <button class="btn ghost small" data-sc-action="refresh" ${scDisabled(similarState.busy)}>Refresh</button>
        <button class="btn small" data-action="maximize">Full screen</button>
      </div>
    </div>

    <div class="sc-grid">
      <div class="panel sc-query-panel">
        <div class="panel-header"><h3>Query image</h3><span class="muted">2D image → Creo parts</span></div>
        <div class="panel-body">
          <div class="sc-query-layout">
            <div class="sc-query-preview">${scQueryPreview()}</div>
            <div class="sc-query-controls">
              <label>Image</label>
              <div class="input-row">
                <input class="input" value="${scEsc(queryPath)}" readonly placeholder="Choose an image" />
                <button class="btn" data-sc-action="chooseImage" ${scDisabled(similarState.busy)}>Choose image</button>
              </div>
              <div class="field-help">Best results come from clean part views with little background clutter.</div>
            </div>
          </div>
        </div>
      </div>

      <div class="panel sc-library-panel">
        <div class="panel-header"><h3>Creo library</h3><span class="muted">Parts only in v1</span></div>
        <div class="panel-body">
          <div class="form-grid">
            <div class="field span-12">
              <label>Source folder</label>
              <div class="input-row">
                <input class="input" data-sc-draft="folder" value="${scEsc(folder)}" placeholder="Folder containing .prt files" ${scDisabled(similarState.busy)} />
                <button class="btn" data-sc-action="chooseFolder" ${scDisabled(similarState.busy)}>Browse</button>
              </div>
            </div>
            <div class="field span-8">
              <span class="field-label">Folder options</span>
              <div class="checks">
                <label class="check"><input type="checkbox" data-sc-draft="recursive" ${scChecked(similarDraft.recursive)} ${scDisabled(similarState.busy)} />Subfolders</label>
                <label class="check"><input type="checkbox" data-sc-draft="latest" ${scChecked(similarDraft.latest)} ${scDisabled(similarState.busy)} />Latest Creo version only</label>
              </div>
            </div>
            <div class="field span-4 sc-index-action">
              <button class="btn" data-sc-action="index" ${scDisabled(similarState.busy || !folder)}>Build / refresh index</button>
            </div>
          </div>
          <div class="sc-index-meta">
            <span><strong>${Number(index.models || 0)}</strong> indexed parts</span>
            <span><strong>${Number(index.views || 0)}</strong> cached views</span>
            <span>8 views / part</span>
            <span class="sc-engine">Local visual signature v1</span>
          </div>
        </div>
      </div>
    </div>

    ${similarState.busy ? `<div class="panel sc-progress-panel">
      <div class="panel-header"><h3>Indexing</h3><button class="btn small danger" data-sc-action="cancel">Cancel</button></div>
      <div class="panel-body">
        <div class="sc-progress-copy"><strong>${scEsc(progress.message || "Indexing...")}</strong><span>${Number(progress.done || 0)} / ${Number(progress.total || 0)}</span></div>
        <div class="progress-track sc-progress-track"><div class="progress-fill" style="width:${percent}%"></div></div>
        <div class="sc-index-meta"><span>${Number(progress.indexed || 0)} rebuilt</span><span>${Number(progress.skipped || 0)} unchanged</span><span>${Number(progress.failed || 0)} failed</span></div>
      </div>
    </div>` : ""}

    <div class="panel sc-results-panel">
      <div class="panel-header">
        <div><h3>Similar parts</h3><span class="muted">Best matching indexed view determines the part score</span></div>
        <div class="toolbar-right sc-search-actions">
          <label class="sc-topk-label">Top <input class="input sc-topk" type="number" min="1" max="100" data-sc-draft="topK" value="${scEsc(similarDraft.topK)}" ${scDisabled(similarState.busy)} /></label>
          <button class="btn primary" data-sc-action="search" ${scDisabled(!canSearch)}>Search</button>
        </div>
      </div>
      <div class="sc-results">${scResultRows()}</div>
      <div class="summary-strip">
        <strong>${Array.isArray(similarState.results) ? similarState.results.length : 0} matches</strong>
        <span>${scEsc(progress.message || "Ready")}</span>
        <span title="${scEsc(index.cachePath || "")}">Local cache${index.cachePath ? ` · ${scEsc(scShortPath(String(index.cachePath)))}` : ""}</span>
      </div>
    </div>

    <div class="notice sc-prototype-note">
      <strong>First-version retrieval engine:</strong> local 8-view image signatures. The indexing contract is versioned so the scorer can be replaced by an ONNX vision embedding model without changing the Creo/library workflow.
    </div>`;
}

function scBindPage() {
  const page = document.querySelector(".main .page") as HTMLElement | null;
  if (!page) return;

  page.querySelectorAll("[data-sc-draft]").forEach(element => {
    const input = element as HTMLInputElement;
    const key = String(input.dataset.scDraft || "");
    const update = () => {
      const value: any = input.type === "checkbox" ? input.checked : input.value;
      (similarDraft as any)[key] = key === "topK" ? Number(value || 20) : value;
    };
    input.addEventListener("input", update);
    input.addEventListener("change", update);
  });

  page.querySelectorAll("[data-sc-action]").forEach(element => {
    const button = element as HTMLButtonElement;
    button.addEventListener("click", async () => {
      const action = button.dataset.scAction;
      if (action === "refresh") scSend("refresh");
      else if (action === "cancel") scSend("cancel");
      else if (action === "chooseImage") {
        const selected = await similarBridge?.chooseImage?.();
        if (selected) {
          similarDraft.queryImage = selected;
          scRenderPage(true);
        }
      } else if (action === "chooseFolder") {
        const selected = await similarBridge?.chooseFolder?.(similarDraft.folder || "");
        if (selected) {
          similarDraft.folder = selected;
          scRenderPage(true);
        }
      } else if (action === "index") {
        scSend("index", similarDraft.folder, similarDraft.recursive ? "1" : "0", similarDraft.latest ? "1" : "0");
      } else if (action === "search") {
        scSend("search", similarDraft.folder, similarDraft.queryImage, Math.max(1, Number(similarDraft.topK || 20)));
      }
    });
  });

  page.querySelectorAll("[data-sc-open]").forEach(element => {
    element.addEventListener("click", () => scSend("open", (element as HTMLElement).dataset.scOpen || ""));
  });
  page.querySelectorAll("[data-sc-locate]").forEach(element => {
    element.addEventListener("click", () => similarBridge?.locate?.((element as HTMLElement).dataset.scLocate || ""));
  });
}

function scRenderPage(force = false) {
  if (!similarActive) return;
  scEnsureNav();
  scMarkNavActive();
  const page = document.querySelector(".main .page") as HTMLElement | null;
  if (!page) return;
  if (!force && page.dataset.similarPage === "1") return;
  page.dataset.similarPage = "1";
  page.innerHTML = scPageHtml();
  scBindPage();
}

function scReconcile() {
  scEnsureNav();
  if (similarActive) scRenderPage(false);
  else scEnsureOverviewCard();
}

document.addEventListener("click", event => {
  const target = event.target as Element | null;
  if (!target) return;
  const standardNav = target.closest("button[data-page]");
  if (standardNav) similarActive = false;
}, true);

const similarObserver = new MutationObserver(() => scReconcile());
const similarRoot = document.getElementById("app");
if (similarRoot) similarObserver.observe(similarRoot, { childList: true, subtree: true });

if (similarBridge) {
  similarBridge.addEventListener("message", (event: { data: any }) => {
    const next = event.data;
    if (!next || next.type !== "similarState") return;
    scSyncState(next);
    if (similarActive) scRenderPage(true);
    else scEnsureOverviewCard();
  });
  scSend("ready");
}

scReconcile();
