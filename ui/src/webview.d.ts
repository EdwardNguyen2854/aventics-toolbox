interface Window {
  chrome?: {
    webview?: {
      postMessage(message: string): void;
      addEventListener(type: "message", listener: (event: MessageEvent) => void): void;
    };
  };
}
