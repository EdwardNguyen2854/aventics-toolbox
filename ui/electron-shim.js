(() => {
  if (!window.aventicsBridge) return;
  window.chrome = window.chrome || {};
  window.chrome.webview = window.aventicsBridge;
})();
