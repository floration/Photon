"""Compare two device histories (Photon *_devc.csv or FDS *_devc.csv).

    python tools/compare_cases.py A.csv B.csv --labels realtime reference
           --window 60 120 --out report_prefix

Writes <out>.txt (per-device time-averaged values and differences) and
<out>.svg (temperature and doorway velocity profiles, layer history).
Standard library only.
"""
import argparse
import csv
import math


def read_devc(path):
    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    # FDS writes a units row first ("s","C",...), then the IDs
    if rows and rows[0] and rows[0][0].strip().strip('"') == "s":
        rows = rows[1:]
    head = [h.strip().strip('"') for h in rows[0]]
    data = []
    for r in rows[1:]:
        if not r:
            continue
        try:
            data.append([float(x) for x in r])
        except ValueError:
            continue
    cols = {h: [row[i] for row in data] for i, h in enumerate(head)}
    return head, cols


def window_mean(t, y, t0, t1):
    v = [b for a, b in zip(t, y) if t0 <= a <= t1 and not math.isnan(b)]
    return sum(v) / len(v) if v else float("nan")


def series_at(cols, prefix):
    """Devices named <prefix>_<cm>, sorted by height."""
    out = []
    for k in cols:
        if k.startswith(prefix + "_"):
            tail = k[len(prefix) + 1:]
            if tail.isdigit():
                out.append((int(tail) / 100.0, k))
    return sorted(out)


def svg_report(path, labels, A, B, t0, t1):
    W, H = 1100, 420
    colors = ["#2563eb", "#dc2626"]
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" font-family="Segoe UI, Arial" font-size="12">',
             '<rect width="100%" height="100%" fill="white"/>']

    def panel(x0, title, xlabel, curves, xr, yr, ylabel, swap=False):
        pw, ph, px, py = 300, 300, x0 + 50, 60
        parts.append(f'<text x="{x0 + 50}" y="30" font-size="14" font-weight="600">{title}</text>')
        parts.append(f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" fill="none" stroke="#999"/>')
        (xa, xb), (ya, yb) = xr, yr
        sx = lambda v: px + (v - xa) / (xb - xa) * pw
        sy = lambda v: py + ph - (v - ya) / (yb - ya) * ph
        for q in range(5):
            xv = xa + (xb - xa) * q / 4
            yv = ya + (yb - ya) * q / 4
            parts.append(f'<text x="{sx(xv):.0f}" y="{py + ph + 16}" text-anchor="middle" fill="#555">{xv:.3g}</text>')
            parts.append(f'<text x="{px - 6}" y="{sy(yv) + 4:.0f}" text-anchor="end" fill="#555">{yv:.3g}</text>')
            parts.append(f'<line x1="{px}" x2="{px + pw}" y1="{sy(yv):.1f}" y2="{sy(yv):.1f}" stroke="#eee"/>')
        parts.append(f'<text x="{px + pw / 2}" y="{py + ph + 34}" text-anchor="middle">{xlabel}</text>')
        parts.append(f'<text x="{x0 + 12}" y="{py + ph / 2}" transform="rotate(-90 {x0 + 12} {py + ph / 2})" text-anchor="middle">{ylabel}</text>')
        for ci, pts in enumerate(curves):
            pts = [p for p in pts if not math.isnan(p[0]) and not math.isnan(p[1])]
            if not pts:
                continue
            d = " ".join(f"{sx(a):.1f},{sy(b):.1f}" for a, b in pts)
            parts.append(f'<polyline points="{d}" fill="none" stroke="{colors[ci]}" stroke-width="2"/>')
            for a, b in pts:
                parts.append(f'<circle cx="{sx(a):.1f}" cy="{sy(b):.1f}" r="2.5" fill="{colors[ci]}"/>')

    def prof(cols, prefix):
        return [(window_mean(cols["Time"], cols[k], t0, t1), z) for z, k in series_at(cols, prefix)]

    tA, tB = prof(A, "TC_center"), prof(B, "TC_center")
    allx = [p[0] for p in tA + tB if not math.isnan(p[0])] or [0, 1]
    panel(0, f"Room center temperature, mean {t0:g}-{t1:g} s", "Temperature (C)", [tA, tB],
          (0, max(allx) * 1.1), (0, 2.4), "Height (m)")
    vA, vB = prof(A, "V_door"), prof(B, "V_door")
    allv = [abs(p[0]) for p in vA + vB if not math.isnan(p[0])] or [1]
    m = max(allv) * 1.15
    panel(370, "Doorway velocity (+ into room)", "Velocity (m/s)", [vA, vB], (-m, m), (0, 2.0), "Height (m)")
    lA = list(zip(A["Time"], A.get("LAYER_height", [])))
    lB = list(zip(B["Time"], B.get("LAYER_height", [])))
    tmax = max([p[0] for p in lA + lB] or [1])
    panel(740, "Layer interface height", "Time (s)", [lA, lB], (0, tmax), (0, 2.4), "Height (m)")
    for i, lab in enumerate(labels):
        parts.append(f'<rect x="{60 + i * 160}" y="{H - 28}" width="14" height="4" fill="{colors[i]}"/>')
        parts.append(f'<text x="{80 + i * 160}" y="{H - 22}">{lab}</text>')
    parts.append("</svg>")
    with open(path, "w") as f:
        f.write("\n".join(parts))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--labels", nargs=2, default=["A", "B"])
    ap.add_argument("--window", nargs=2, type=float, default=[60, 120])
    ap.add_argument("--out", default="compare")
    args = ap.parse_args()
    _, A = read_devc(args.a)
    _, B = read_devc(args.b)
    t0, t1 = args.window
    lines = [f"{args.labels[0]}: {args.a}", f"{args.labels[1]}: {args.b}", f"time window {t0:g} to {t1:g} s (means)", ""]
    lines.append(f"{'device':18s} {args.labels[0]:>12s} {args.labels[1]:>12s} {'diff':>10s} {'rel':>8s}")
    rels = []
    # scale for temperature differences: the largest rise above ambient in B
    rise = max([abs(window_mean(B["Time"], B[k], t0, t1) - 20.0) for k in B if k.startswith("TC")] or [1.0])
    for k in A:
        if k == "Time" or k not in B:
            continue
        a = window_mean(A["Time"], A[k], t0, t1)
        b = window_mean(B["Time"], B[k], t0, t1)
        d = a - b
        # relative to the rise above ambient for temperatures, to the reference otherwise
        is_t = "TC" in k or k.endswith("_upper") or k.startswith("T_")
        base = max(abs(b - 20.0), 0.25 * rise) if is_t else abs(b)
        rel = abs(d) / base if base > 1e-6 else float("nan")
        if not math.isnan(rel) and (is_t or base > 0.05):
            rels.append((rel, k))
        lines.append(f"{k:18s} {a:12.3f} {b:12.3f} {d:10.3f} {rel * 100 if not math.isnan(rel) else float('nan'):7.1f}%")
    if rels:
        rels.sort()
        mean = sum(r for r, _ in rels) / len(rels)
        lines += ["", f"mean relative difference over {len(rels)} significant devices: {mean * 100:.1f}%",
                  f"largest: {rels[-1][1]} {rels[-1][0] * 100:.1f}%"]
    with open(args.out + ".txt", "w") as f:
        f.write("\n".join(lines) + "\n")
    svg_report(args.out + ".svg", args.labels, A, B, t0, t1)
    print("\n".join(lines[-3:]))


if __name__ == "__main__":
    main()
