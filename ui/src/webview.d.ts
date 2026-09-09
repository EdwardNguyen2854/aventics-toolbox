interface AventicsRendererBridge {
  postMessage(message: string): void;
  addEventListener(type: "message", listener: (event: MessageEvent) => void): void;
}

interface Window {
  aventicsBridge?: AventicsRendererBridge;
  chrome?: {
    webview?: AventicsRendererBridge;
  };
}
