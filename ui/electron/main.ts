import { app, BrowserWindow, dialog, ipcMain, shell } from "electron";
import fs from "node:fs";
import net from "node:net";
import path from "node:path";

let mainWindow: BrowserWindow | null = null;
let pipe: net.Socket | null = null;
let reconnectTimer: NodeJS.Timeout | null = null;
let receiveBuffer = "";
let similarPipe: net.Socket | null = null;
let similarReconnectTimer: NodeJS.Timeout | null = null;
let similarReceiveBuffer = "";
let shuttingDown = false;
let lastState: any = null;
let lastSimilarState: any = null;
let receivedMainState = false;
let receivedSimilarState = false;

function argValue(name: string): string {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length ? process.argv[index + 1] : "";
}

const pipeName = argValue("--pipe");
const creoPid = argValue("--creo-pid");
const similarPipeName = creoPid ? `\\\\.\\pipe\\aventics-similar-cad-${creoPid}` : "";
const mockMode = process.argv.includes("--mock") || !pipeName;
const installRoot = path.resolve(path.dirname(process.execPath), "..", "..");
const logDirectory = path.join(installRoot, "logs");
const bridgeLogPath = path.join(logDirectory, "electron_bridge.log");

function bridgeLog(message: string, details = "") {
  const line = `${new Date().toISOString()}  PID ${process.pid}  ${message}${details ? `  ${details}` : ""}\r\n`;
  try {
    fs.mkdirSync(logDirectory, { recursive: true });
    fs.appendFileSync(bridgeLogPath, line, "utf8");
  } catch {
    // Diagnostics must never prevent the Toolbox UI from starting.
  }
}

bridgeLog(
  "Electron main started",
  `user=${process.env.USERDOMAIN || ""}\\${process.env.USERNAME || ""} creoPid=${creoPid || "-"} pipe=${pipeName || "-"} exec=${process.execPath}`
);

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
      instances: {
        folder: "", codes: "", recursive: false, latest: true,
        rowsAlongX: false, useZ: false, columns: 5, columnGap: 50, rowGap: 50
      }
    },
    weakResults: [],
    accuracyResults: [],
    inspectionResults: [],
    instanceResults: []
  };
}

function mockSimilarState(message = "Similar CAD Search mock mode") {
  return {
    type: "similarState",
    protocolVersion: 2,
    busy: false,
    progress: {
      done: 0, total: 0, indexed: 0, skipped: 0, failed: 0,
      currentModel: "", viewDone: 0, viewTotal: 8, currentView: "", message
    },
    settings: { folder: "", queryImage: "", recursive: false, latest: true, topK: 20, autoCrop: true },
    index: {
      models: 0, views: 0, viewCountPerModel: 8, cachePath: "",
      engine: "hybrid-shape-v2", captureProfile: "ptc-axis-8-v2"
    },
    query: { ready: false, processedPath: "", aspectRatio: 0, fillRatio: 0, autoCrop: true },
    results: [],
    library: { requested: false, filter: "", page: 1, pageSize: 24, total: 0, items: [], inspected: null }
  };
}

function sendStateToRenderer(state: any) {
  lastState = state;
  if (!mainWindow || mainWindow.isDestroyed()) return;
  mainWindow.webContents.send("aventics:state", state);
}

function sendSimilarStateToRenderer(state: any) {
  lastSimilarState = state;
  if (!mainWindow || mainWindow.isDestroyed()) return;
  mainWindow.webContents.send("aventics:similar-state", state);
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

function sendSimilarDisconnected(message: string) {
  const base = lastSimilarState || mockSimilarState(message);
  sendSimilarStateToRenderer({
    ...base,
    busy: false,
    progress: { ...(base.progress || {}), done: 0, total: 0, message }
  });
}

function handleNativeMessage(line: string) {
  if (!line.trim()) return;
  try {
    const message = JSON.parse(line);
    if (message && message.type === "control") {
      const command = String(message.command || "");
      bridgeLog("Creo control received", command);
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
    if (message && message.type === "state") {
      if (!receivedMainState) {
        receivedMainState = true;
        bridgeLog("First Creo state received", `pipe=${pipeName}`);
      }
      sendStateToRenderer(message);
    }
  } catch (error) {
    bridgeLog("Invalid message from Creo bridge", String(error));
    console.error("Invalid message from Creo bridge", error, line);
  }
}

function handleSimilarNativeMessage(line: string) {
  if (!line.trim()) return;
  try {
    const message = JSON.parse(line);
    if (message && message.type === "similarState") {
      if (!receivedSimilarState) {
        receivedSimilarState = true;
        bridgeLog("First Similar CAD state received", `pipe=${similarPipeName}`);
      }
      sendSimilarStateToRenderer(message);
    }
  } catch (error) {
    bridgeLog("Invalid message from Similar CAD bridge", String(error));
    console.error("Invalid message from Similar CAD bridge", error, line);
  }
}

function scheduleReconnect() {
  if (mockMode || shuttingDown || reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connectPipe();
  }, 1000);
}

function scheduleSimilarReconnect() {
  if (mockMode || shuttingDown || similarReconnectTimer || !similarPipeName) return;
  similarReconnectTimer = setTimeout(() => {
    similarReconnectTimer = null;
    connectSimilarPipe();
  }, 1000);
}

function connectPipe() {
  if (mockMode || shuttingDown || !pipeName) return;
  if (pipe && !pipe.destroyed) return;

  bridgeLog("Connecting to Creo pipe", pipeName);
  const socket = net.createConnection(pipeName);
  pipe = socket;
  receiveBuffer = "";
  socket.setEncoding("utf8");

  socket.on("connect", () => {
    bridgeLog("Connected to Creo pipe", pipeName);
    socket.write("ready\n", error => {
      if (error) bridgeLog("Failed to send ready to Creo", `${error.name}: ${error.message}`);
      else bridgeLog("Ready sent to Creo", pipeName);
    });
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
    bridgeLog("Creo pipe error", `code=${error.code || "-"} message=${error.message}`);
    if (!shuttingDown && error.code !== "ENOENT" && error.code !== "ECONNREFUSED") {
      console.error("Aventics named-pipe error", error);
    }
  });

  socket.on("close", hadError => {
    bridgeLog("Creo pipe closed", `hadError=${hadError}`);
    if (pipe === socket) pipe = null;
    if (!shuttingDown) {
      sendDisconnected(`Creo session${creoPid ? ` ${creoPid}` : ""} disconnected — reconnecting...`);
      scheduleReconnect();
    }
  });
}

function connectSimilarPipe() {
  if (mockMode || shuttingDown || !similarPipeName) return;
  if (similarPipe && !similarPipe.destroyed) return;

  bridgeLog("Connecting to Similar CAD pipe", similarPipeName);
  const socket = net.createConnection(similarPipeName);
  similarPipe = socket;
  similarReceiveBuffer = "";
  socket.setEncoding("utf8");

  socket.on("connect", () => {
    bridgeLog("Connected to Similar CAD pipe", similarPipeName);
    socket.write("ready\n", error => {
      if (error) bridgeLog("Failed to send ready to Similar CAD", `${error.name}: ${error.message}`);
      else bridgeLog("Ready sent to Similar CAD", similarPipeName);
    });
  });

  socket.on("data", (chunk: string) => {
    similarReceiveBuffer += chunk;
    for (;;) {
      const newline = similarReceiveBuffer.indexOf("\n");
      if (newline < 0) break;
      const line = similarReceiveBuffer.slice(0, newline).replace(/\r$/, "");
      similarReceiveBuffer = similarReceiveBuffer.slice(newline + 1);
      handleSimilarNativeMessage(line);
    }
  });

  socket.on("error", (error: NodeJS.ErrnoException) => {
    bridgeLog("Similar CAD pipe error", `code=${error.code || "-"} message=${error.message}`);
    if (!shuttingDown && error.code !== "ENOENT" && error.code !== "ECONNREFUSED") {
      console.error("Similar CAD named-pipe error", error);
    }
  });

  socket.on("close", hadError => {
    bridgeLog("Similar CAD pipe closed", `hadError=${hadError}`);
    if (similarPipe === socket) similarPipe = null;
    if (!shuttingDown) {
      sendSimilarDisconnected("Similar CAD service disconnected — reconnecting...");
      scheduleSimilarReconnect();
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
    bridgeLog("Renderer command waiting for Creo pipe", message.split("\t", 1)[0]);
    sendDisconnected("Waiting for Creo connection...");
    scheduleReconnect();
    return;
  }
  pipe.write(`${message}\n`);
}

function sendSimilarToCreo(message: string) {
  if (mockMode) {
    if (message === "ready" || message === "refresh") sendSimilarStateToRenderer(mockSimilarState());
    else sendSimilarStateToRenderer(mockSimilarState(`Mock mode received: ${message.split("\t", 1)[0]}`));
    return;
  }

  if (!similarPipe || similarPipe.destroyed || !similarPipe.writable) {
    bridgeLog("Renderer command waiting for Similar CAD pipe", message.split("\t", 1)[0]);
    sendSimilarDisconnected("Waiting for Similar CAD service...");
    scheduleSimilarReconnect();
    return;
  }
  similarPipe.write(`${message}\n`);
}

async function chooseImage(): Promise<string> {
  if (!mainWindow || mainWindow.isDestroyed()) return "";
  const result = await dialog.showOpenDialog(mainWindow, {
    title: "Choose Similar CAD query image",
    properties: ["openFile"],
    filters: [
      { name: "Images", extensions: ["png", "jpg", "jpeg", "bmp"] },
      { name: "All files", extensions: ["*"] }
    ]
  });
  return result.canceled || !result.filePaths.length ? "" : result.filePaths[0];
}

async function chooseSimilarFolder(defaultPath = ""): Promise<string> {
  if (!mainWindow || mainWindow.isDestroyed()) return "";
  const result = await dialog.showOpenDialog(mainWindow, {
    title: "Choose Creo part library",
    defaultPath: defaultPath || undefined,
    properties: ["openDirectory"]
  });
  return result.canceled || !result.filePaths.length ? "" : result.filePaths[0];
}

function createWindow() {
  bridgeLog("Creating Toolbox window");
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

  mainWindow.webContents.on("did-fail-load", (_event, errorCode, errorDescription) => {
    bridgeLog("Toolbox renderer failed to load", `code=${errorCode} description=${errorDescription}`);
  });
  mainWindow.webContents.on("render-process-gone", (_event, details) => {
    bridgeLog("Toolbox renderer process gone", `reason=${details.reason} exitCode=${details.exitCode}`);
  });

  mainWindow.once("ready-to-show", () => {
    bridgeLog("Toolbox window ready", mockMode ? "mock" : "live");
    mainWindow?.show();
    if (mockMode) {
      sendStateToRenderer(mockState());
      sendSimilarStateToRenderer(mockSimilarState());
    } else {
      connectPipe();
      connectSimilarPipe();
    }
  });

  mainWindow.on("closed", () => {
    bridgeLog("Toolbox window closed");
    mainWindow = null;
  });

  void mainWindow.loadFile(path.join(__dirname, "..", "index.html"));
}

ipcMain.on("aventics:send", (_event, message) => sendToCreo(String(message)));
ipcMain.on("aventics:similar-send", (_event, message) => sendSimilarToCreo(String(message)));
ipcMain.handle("aventics:similar-choose-image", () => chooseImage());
ipcMain.handle("aventics:similar-choose-folder", (_event, defaultPath) => chooseSimilarFolder(String(defaultPath || "")));
ipcMain.handle("aventics:similar-locate", (_event, filePath) => {
  const value = String(filePath || "");
  if (value) shell.showItemInFolder(value);
});

app.whenReady().then(() => {
  bridgeLog("Electron app ready");
  createWindow();
});

app.on("window-all-closed", () => {
  bridgeLog("Electron shutdown requested");
  shuttingDown = true;
  if (reconnectTimer) clearTimeout(reconnectTimer);
  reconnectTimer = null;
  if (similarReconnectTimer) clearTimeout(similarReconnectTimer);
  similarReconnectTimer = null;
  if (pipe) pipe.destroy();
  pipe = null;
  if (similarPipe) similarPipe.destroy();
  similarPipe = null;
  app.quit();
});
