import { contextBridge, ipcRenderer } from "electron";

// Compatibility adapter for the existing renderer. The UI still sees the tiny
// chrome.webview surface it used in the WebView2 prototype, but all transport is
// now Electron IPC -> named pipe -> native Creo TOOLKIT DLL.
contextBridge.exposeInMainWorld("chrome", {
  webview: {
    postMessage(message: string) {
      ipcRenderer.send("aventics:send", String(message));
    },
    addEventListener(type: string, listener: (event: { data: unknown }) => void) {
      if (type !== "message" || typeof listener !== "function") return;
      ipcRenderer.on("aventics:state", (_event, data) => listener({ data }));
    }
  }
});
