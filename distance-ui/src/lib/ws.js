export function createDistanceSocket({ host, onData, onStatus }) {
  const url = `ws://${host}:81`;
  let ws;
  let alive = true;
  let retryTimer = null;

  // console.log("Creating WS socket to", url);

  function connect() {
    if (!alive) return;
    onStatus?.({ state: "connecting", url });
// console.log("Connecting WS to", url);
    try {
      ws = new WebSocket(url);
    } catch (err) {
      onStatus?.({ state: "error", url, error: String(err) });
      retryTimer = setTimeout(connect, 1000);
      return;
    }
// console.log("WS socket created, waiting for open...");
    ws.onopen = () => onStatus?.({ state: "open", url });


// TEMP DEBUG:
// console.log("WS socket open, waiting for messages...");
    ws.onmessage = (evt) => {
      // TEMP DEBUG:
      // console.log("WS:", evt.data);

      try {
        onData?.(JSON.parse(evt.data));
      } catch {}
    };

    ws.onmessage = (evt) => {
      try {
        onData?.(JSON.parse(evt.data));
      } catch {}
    };

    ws.onerror = () => {
      onStatus?.({ state: "error", url, error: "ws error (see console)" });
    };

    ws.onclose = (e) => {
      onStatus?.({ state: "closed", url, code: e.code, reason: e.reason });
      retryTimer = setTimeout(connect, 800);
    };
  }
// END connect
  connect();

  return () => {
    alive = false;
    if (retryTimer) clearTimeout(retryTimer);
    try {
      ws?.close();
    } catch {}
  };
}



