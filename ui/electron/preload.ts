import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("aventicsBridge", {
  postMessage(message: string) {
    ipcRenderer.send("aventics:send", String(message));
  },
  addEventListener(type: string, listener: (event: { data: unknown }) => void) {
    if (type !== "message" || typeof listener !== "function") return;
    ipcRenderer.on("aventics:state", (_event, data) => listener({ data }));
  }
});

contextBridge.exposeInMainWorld("aventicsSimilarCad", {
  postMessage(message: string) {
    ipcRenderer.send("aventics:similar-send", String(message));
  },
  addEventListener(type: string, listener: (event: { data: unknown }) => void) {
    if (type !== "message" || typeof listener !== "function") return;
    ipcRenderer.on("aventics:similar-state", (_event, data) => listener({ data }));
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
