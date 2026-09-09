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
