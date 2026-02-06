import { useEffect, useState } from "react";
import styles from "./App.module.css";
import { createDistanceSocket } from "./lib/ws";
import DistanceVertical from "./components/DistanceVertical/DistanceVertical";

export default function App() {
  const [host, setHost] = useState("192.168.13.212");
  const [status, setStatus] = useState({ state: "idle" });
  const [data, setData] = useState({ ok: 0 });

  useEffect(() => {
    const cleanup = createDistanceSocket({
      host,
      onData: (obj) => setData(obj),
      onStatus: (s) => setStatus(s),
    });
    return cleanup;
  }, [host]);
// console.log("Status:", status, "Data:", data);
  const ok = !!data?.ok;
  const cm = ok ? Number(data.cm) : null;
  const mm = cm != null ? cm * 10 : null;

  // TEMP DEBUG:
  // console.log("Status:", status, "Data:", data);
  return (
    <div className={styles.page}>
      <div className={styles.shell}>
        <div className={styles.top}>
          <h1 className={styles.title}>Distance UI</h1>

          <div className={styles.controls}>
            <label className={styles.inputLabel}>
              ESP32 host (IP sau mDNS)
              <input
                className={styles.input}
                value={host}
                onChange={(e) => setHost(e.target.value)}
                placeholder="esp32-distance.local sau 192.168.x.x"
              />
            </label>

            <div className={styles.status}>
              <span className={styles.dot} data-state={status.state} />
              <span className={styles.state}>{status.state}</span>
              {status.error && (
                <span className={styles.err}>({status.error})</span>
              )}
            </div>
          </div>
        </div>

        <DistanceVertical mm={mm} ok={ok} mmMin={100} mmMax={3000} />

        <div className={styles.hint}>
          WS target: <code>ws://{host}:81</code> · Test HTTP:{" "}
          <code>http://{host}/ping</code>
        </div>
      </div>
    </div>
  );
}
