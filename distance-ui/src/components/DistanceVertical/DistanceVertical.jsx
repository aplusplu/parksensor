import { useEffect, useMemo, useRef, useState } from "react";
import styles from "./DistanceVertical.module.css";
// Clamp value v between min and max
function clamp(v, min, max) {
  return Math.min(Math.max(v, min), max);
}
// DistanceVertical component shows a vertical distance meter
export default function DistanceVertical({
  mm,
  ok,
  mmMin = 100,
  mmMax = 3000,
}) {
  const lastMmRef = useRef(null);
  const [pulse, setPulse] = useState(0);
// safeMm is the clamped mm value within mmMin and mmMax range
  const safeMm = useMemo(() => {
    if (!ok || typeof mm !== "number" || Number.isNaN(mm)) return null;
    return clamp(mm, mmMin, mmMax);
  }, [mm, ok, mmMin, mmMax]);

  // top = close, bottom = far => pct = (safeMm - mmMin) / (mmMax - mmMin)
  const pct = useMemo(() => {
    if (safeMm == null) return 0;
    const t = (safeMm - mmMin) / (mmMax - mmMin);
    return clamp(t, 0, 1);
  }, [safeMm, mmMin, mmMax]);

  useEffect(() => {
    if (safeMm == null) return;

    const last = lastMmRef.current;
    lastMmRef.current = safeMm;

    if (last == null) {
      setPulse(1);
      const id = setTimeout(() => setPulse(0), 350);
      return () => clearTimeout(id);
    }
// Determine pulse intensity based on change in distance
    const delta = Math.abs(safeMm - last);
    const intensity = delta > 120 ? 2 : delta > 30 ? 1 : 0;
// Trigger pulse effect if intensity is greater than 0
    if (intensity > 0) {
      setPulse(intensity);
      const id = setTimeout(() => setPulse(0), 350);
      return () => clearTimeout(id);
    }
  }, [safeMm]);
// Render the distance meter UI
  return (
    <div className={styles.card}>
      <div className={styles.topRow}>
        <div className={styles.title}>DISTANCE</div>

        <div
          className={`${styles.number} ${pulse ? styles.pulsing : ""}`}
          data-intensity={pulse}
        >
          {ok && safeMm != null ? (
            <>
              <span className={styles.mm}>{safeMm} mm</span>
              <span className={styles.cm}>{(safeMm / 10).toFixed(1)} cm</span>
            </>
          ) : (
            <span className={styles.na}>N/A</span>
          )}
        </div>
      </div>

      <div className={styles.meterWrap}>
        <div className={styles.labels}>
          <span>{mmMin}mm</span>
          <span>{mmMax}mm</span>
        </div>

        <div className={styles.meter}>
          <div className={styles.track} />

          {ok && safeMm != null && (
            <div
              className={styles.marker}
              style={{ top: `${pct * 100}%` }}
              data-intensity={pulse}
            >
              <div className={styles.dot} />
              <div className={styles.line} />
            </div>
          )}

          <div className={styles.scan} />
        </div>
      </div>

      <div className={styles.footer}>
        <span className={`${styles.badge} ${ok ? styles.live : styles.down}`}>
          {ok ? "LIVE" : "NO SIGNAL"}
        </span>
      </div>
    </div>
  );
}
