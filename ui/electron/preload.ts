import { contextBridge, ipcRenderer } from "electron";

let mainStateReceived = false;
let similarStateReceived = false;
let mainHandshakeStarted = false;
let similarHandshakeStarted = false;

function startMainHandshake() {
  if (mainHandshakeStarted) return;
  mainHandshakeStarted = true;
  for (const delay of [0, 250, 750, 1500, 3000]) {
    setTimeout(() => {
      if (!mainStateReceived) ipcRenderer.send("aventics:send", "refresh");
    }, delay);
  }
}

function startSimilarHandshake() {
  if (similarHandshakeStarted) return;
  similarHandshakeStarted = true;
  for (const delay of [0, 250, 750, 1500, 3000]) {
    setTimeout(() => {
      if (!similarStateReceived) ipcRenderer.send("aventics:similar-send", "refresh");
    }, delay);
  }
}

contextBridge.exposeInMainWorld("aventicsBridge", {
  postMessage(message: string) {
    ipcRenderer.send("aventics:send", String(message));
  },
  addEventListener(type: string, listener: (event: { data: unknown }) => void) {
    if (type !== "message" || typeof listener !== "function") return;
    ipcRenderer.on("aventics:state", (_event, data) => {
      mainStateReceived = true;
      listener({ data });
    });
    startMainHandshake();
  }
});

contextBridge.exposeInMainWorld("aventicsSimilarCad", {
  postMessage(message: string) {
    ipcRenderer.send("aventics:similar-send", String(message));
  },
  addEventListener(type: string, listener: (event: { data: unknown }) => void) {
    if (type !== "message" || typeof listener !== "function") return;
    ipcRenderer.on("aventics:similar-state", (_event, data) => {
      similarStateReceived = true;
      listener({ data });
    });
    startSimilarHandshake();
  },
  chooseImage() {
    return ipcRenderer.invoke("aventics:similar-choose-image");
  },
  chooseFolder(defaultPath = "") {
    return ipcRenderer.invoke("aventics:similar-choose-folder", String(defaultPath || ""));
  },
  locate(filePath: string) {
    return ipcRenderer.invoke("aventics:similar-locate", String(filePath || ""));
  }
});
