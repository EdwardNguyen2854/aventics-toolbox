import { app, BrowserWindow, ipcMain } from "electron";
import net from "node:net";
import path from "node:path";

let mainWindow: BrowserWindow | null = null;
let pipe: net.Socket | null = null;
let reconnectTimer: NodeJS.Timeout | null = null;
let receiveBuffer = "";
let shuttingDown = false;
let lastState: any = null;

function argValue(name: string): string {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length ? process.argv[index + 1] : "";
}

const pipeName = argValue("--pipe");
const creoPid = argValue("--creo-pid");
const mockMode = process.argv.includes("--mock") || !pipeName;

function mockState(message = "Mock Creo session — Electron UI preview") {
  return {
    type: "state",
    busy: false,
    operation: "",
    progress: { done: 0, total: 0, message },
    activeModel: { name: "MOCK_ASSEMBLY.ASM", isAssembly: true },
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
}

function sendStateToRenderer(state: any) {
  lastState = state;
  if (!mainWindow || mainWindow.isDestroyed()) return;
  mainWindow.webContents.send("aventics:state", state);
}

function sendDisconnected(message: string) {
  const base = lastState || mockState(message);
  sendStateToRenderer({
    ...base,
    busy: false,
    operation: "",
    progress: { ...(base.progress || {}), done: 0, total: 0, message }
  });
}

function handleNativeMessage(line: string) {
  if (!line.trim()) return;
  try {
    const message = JSON.parse(line);
    if (message && message.type === "control") {
      const command = String(message.command || "");
      if (!mainWindow || mainWindow.isDestroyed()) return;
      if (command === "focus") {
        if (mainWindow.isMinimized()) mainWindow.restore();
        mainWindow.show();
        mainWindow.focus();
      } else if (command === "hide") {
        mainWindow.hide();
      } else if (command === "show") {
        mainWindow.show();
        mainWindow.focus();
      } else if (command === "close") {
        shuttingDown = true;
        mainWindow.close();
      }
      return;
    }
    if (message && message.type === "state") sendStateToRenderer(message);
  } catch (error) {
    console.error("Invalid message from Creo bridge", error, line);
  }
}

function scheduleReconnect() {
  if (mockMode || shuttingDown || reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connectPipe();
  }, 1000);
}

function connectPipe() {
  if (mockMode || shuttingDown || !pipeName) return;
  if (pipe && !pipe.destroyed) return;

  const socket = net.createConnection(pipeName);
  pipe = socket;
  receiveBuffer = "";
  socket.setEncoding("utf8");

  socket.on("connect", () => {
    socket.write("ready\n");
  });

  socket.on("data", (chunk: string) => {
    receiveBuffer += chunk;
    for (;;) {
      const newline = receiveBuffer.indexOf("\n");
      if (newline < 0) break;
      const line = receiveBuffer.slice(0, newline).replace(/\r$/, "");
      receiveBuffer = receiveBuffer.slice(newline + 1);
      handleNativeMessage(line);
    }
  });

  socket.on("error", (error: NodeJS.ErrnoException) => {
    if (!shuttingDown && error.code !== "ENOENT" && error.code !== "ECONNREFUSED") {
      console.error("Aventics named-pipe error", error);
    }
  });

  socket.on("close", () => {
    if (pipe === socket) pipe = null;
    if (!shuttingDown) {
      sendDisconnected(`Creo session${creoPid ? ` ${creoPid}` : ""} disconnected — reconnecting...`);
      scheduleReconnect();
    }
  });
}

function sendToCreo(message: string) {
  if (message === "toggleMaximize") {
    if (!mainWindow || mainWindow.isDestroyed()) return;
    if (mainWindow.isMaximized()) mainWindow.unmaximize();
    else mainWindow.maximize();
    return;
  }

  if (mockMode) {
    if (message === "ready" || message === "refresh") sendStateToRenderer(mockState());
    else sendStateToRenderer(mockState(`Mock mode received: ${message.split("\t", 1)[0]}`));
    return;
  }

  if (!pipe || pipe.destroyed || !pipe.writable) {
    sendDisconnected("Waiting for Creo connection...");
    scheduleReconnect();
    return;
  }
  pipe.write(`${message}\n`);
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1240,
    height: 800,
    minWidth: 860,
    minHeight: 580,
    show: false,
    title: "Aventics Toolbox — Electron",
    backgroundColor: "#f6f7f9",
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false
    }
  });

  mainWindow.once("ready-to-show", () => {
    mainWindow?.show();
    if (mockMode) sendStateToRenderer(mockState());
    else connectPipe();
  });

  mainWindow.on("closed", () => {
    mainWindow = null;
  });

  void mainWindow.loadFile(path.join(__dirname, "..", "index.html"));
}

ipcMain.on("aventics:send", (_event, message) => sendToCreo(String(message)));

app.whenReady().then(createWindow);

app.on("window-all-closed", () => {
  shuttingDown = true;
  if (reconnectTimer) clearTimeout(reconnectTimer);
  reconnectTimer = null;
  if (pipe) pipe.destroy();
  pipe = null;
  app.quit();
});
