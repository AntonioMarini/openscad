#!/usr/bin/env python3
import argparse, csv, os, re, shutil, subprocess, sys, pathlib, datetime

FIXED_CONFIGS = [
    {"label": "Naive, no OBB",       "binarization": 0, "obb": 0, "cache": 0, "distribution": 0, "shadows": 1, "samples": 1},
    {"label": "Naive, OBB",          "binarization": 0, "obb": 1, "cache": 0, "distribution": 0, "shadows": 1, "samples": 1},
    {"label": "KD, OBB",             "binarization": 1, "obb": 1, "cache": 0, "distribution": 0, "shadows": 1, "samples": 1},
    {"label": "KD, OBB, Dist+Cache", "binarization": 1, "obb": 1, "cache": 1, "distribution": 1, "shadows": 1, "samples": 1},
]

BENCH_RE = re.compile(
    r"\[BENCH\].*total_frames=([0-9]+).*avg_fps=([0-9.]+).*min_fps=([0-9.]+).*max_fps=([0-9.]+)"
    r".*node_count=([0-9]+).*tree_depth=([0-9]+).*render_w=([0-9]+).*render_h=([0-9]+)"
    r".*obb_skipped=([0-9]+).*cache_hits=([0-9]+).*cache_misses=([0-9]+)"
)


def collect_models(inputs):
    models = []
    for item in inputs:
        p = pathlib.Path(item)
        if p.is_dir():
            models.extend(sorted(p.glob("*.scad")))
        elif p.suffix == ".scad":
            models.append(p)
        else:
            print(f"Warning: skipping {item} (not a .scad file or directory)")
    return models


def detect_gpu():
    for cmd in (["glxinfo", "-B"], ["lspci", "-mm"]):
        try:
            out = subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True)
            if cmd[0] == "glxinfo":
                for line in out.splitlines():
                    if "OpenGL renderer" in line:
                        return line.split(":", 1)[1].strip()
            else:
                for line in out.splitlines():
                    if "VGA" in line or "Display" in line or "3D" in line:
                        parts = re.findall(r'"([^"]*)"', line)
                        if parts:
                            return parts[-1]
        except (FileNotFoundError, subprocess.CalledProcessError):
            pass
    return "Unknown"


def run_model(model_path, cfg, args, runs_dir, ts):
    stem = model_path.stem
    safe = cfg["label"].replace(" ", "_").replace(",", "").replace("+", "")
    out = str(pathlib.Path(runs_dir) / f"{safe}_{ts}")
    cmd = [
        args.openscad,
        str(model_path),
        "--benchmark",
        f"--bench-steps={args.steps}",
        f"--bench-elevation={args.elevation}",
        f"--bench-distance={args.distance}",
        f"--bench-output={out}",
        f"--rt-binarization={cfg['binarization']}",
        f"--rt-obb={cfg['obb']}",
        f"--rt-cache={cfg['cache']}",
        f"--rt-shadows={cfg['shadows']}",
        f"--rt-samples={cfg['samples']}",
        f"--rt-distribution={cfg['distribution']}",
        f"--bench-width={args.width}",
        f"--bench-height={args.height}",
    ]
    print(f"\n[{stem}] [{cfg['label']}] Running: {' '.join(cmd)}")
    env = os.environ.copy()
    env.setdefault("QT_XCB_GL_INTEGRATION", "xcb_glx")

    proc = subprocess.Popen(
        cmd, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    avg_fps = min_fps = max_fps = None
    total_frames = node_count = tree_depth = render_w = render_h = None
    obb_skipped = cache_hits = cache_misses = None
    for line in proc.stdout:
        print(line, end="", flush=True)
        m = BENCH_RE.search(line)
        if m:
            total_frames = int(m[1])
            avg_fps, min_fps, max_fps = float(m[2]), float(m[3]), float(m[4])
            node_count, tree_depth = int(m[5]), int(m[6])
            render_w, render_h = int(m[7]), int(m[8])
            obb_skipped, cache_hits, cache_misses = int(m[9]), int(m[10]), int(m[11])
    proc.wait()

    frames = []
    mpath = pathlib.Path(out) / "measurements.csv"
    if mpath.exists():
        with open(mpath) as f:
            frames = list(csv.DictReader(f))

    screenshot = pathlib.Path(out) / "frame.png"

    return (proc.returncode, avg_fps, min_fps, max_fps,
            total_frames, node_count, tree_depth, render_w, render_h,
            obb_skipped, cache_hits, cache_misses, frames, screenshot)



def _cache_hit_rate(hits, misses):
    total = hits + misses
    return f"{100.0 * hits / total:.1f}\\%" if total > 0 else "N/A"


def _obb_skip_nodes(obb_skipped, render_w, render_h):
    px = render_w * render_h if render_w and render_h else 0
    return f"{obb_skipped / px:.2f}" if px else "N/A"


def _tex_escape(s):
    return s.replace("_", "\\_").replace("&", "\\&").replace("%", "\\%")


def write_model_csv(out_dir, results):
    path = out_dir / "results.csv"
    with open(path, "w") as f:
        for cfg_label, frames, _stats, avg, mn, mx in results:
            f.write(f"# {cfg_label}\n")
            f.write("Frame,Frame (ms),FPS\n")
            for row in frames:
                f.write(f"{row['step']},{float(row['frame_ms']):.2f},{float(row['fps']):.2f}\n")
            f.write("\n")
    print(f"  CSV:   {path}")


def write_model_latex(out_dir, stem, results, has_screenshot, gpu):
    path = out_dir / "benchmark.tex"

    first_stats = results[0][2]
    render_w    = first_stats["render_w"]
    render_h    = first_stats["render_h"]
    node_count  = first_stats["node_count"]
    res_str     = f"${render_w}\\times{render_h}$"
    stem_tex    = _tex_escape(stem)
    gpu_tex     = _tex_escape(gpu)
    stem_label  = stem.replace("_", "-")

    with open(path, "w") as f:
        f.write("% Generated by benchmark.py\n")
        f.write("% Requires booktabs, graphicx in your preamble.\n\n")

        # ── Screenshot figure ────────────────────────────────────────────────
        f.write("\\begin{figure}[htbp]\n")
        f.write("  \\centering\n")
        if has_screenshot:
            f.write("  \\includegraphics[width=0.65\\linewidth]{frame}\n")
        else:
            f.write("  % screenshot not available\n")
        f.write(f"  \\caption{{Raytracer preview: \\texttt{{{stem_tex}}}.}}\n")
        f.write(f"  \\label{{fig:rt-{stem_label}}}\n")
        f.write("\\end{figure}\n\n")

        # ── Scene parameter recap ────────────────────────────────────────────
        f.write("\\begin{table}[htbp]\n")
        f.write("  \\centering\\small\n")
        f.write("  \\begin{tabular}{@{}ll@{}}\n")
        f.write("    \\toprule\n")
        f.write("    \\multicolumn{2}{@{}l}{\\textbf{Scene parameters}} \\\\\n")
        f.write("    \\midrule\n")
        f.write(f"    Model      & \\texttt{{{stem_tex}}} \\\\\n")
        f.write(f"    Resolution & {res_str}\\,px \\\\\n")
        f.write(f"    GPU        & {gpu_tex} \\\\\n")
        f.write(f"    Nodes      & {node_count} \\\\\n")
        f.write("    \\bottomrule\n")
        f.write("  \\end{tabular}\n")
        f.write(f"  \\caption{{Benchmark parameters for \\texttt{{{stem_tex}}}.}}\n")
        f.write(f"  \\label{{tab:rt-{stem_label}-params}}\n")
        f.write("\\end{table}\n\n")

        # ── Config comparison summary table ──────────────────────────────────
        f.write("\\begin{table}[htbp]\n")
        f.write("  \\centering\\small\n")
        f.write("  \\setlength{\\tabcolsep}{6pt}\n")
        f.write("  \\begin{tabular}{@{}lcccccc@{}}\n")
        f.write("    \\toprule\n")
        f.write("    \\textbf{Config} & \\textbf{Tree depth} & "
                "\\textbf{OBB skip/px} & \\textbf{Cache hit} & "
                "\\textbf{FPS min} & \\textbf{FPS avg} & \\textbf{FPS max} \\\\\n")
        f.write("    \\midrule\n")
        for cfg_label, _frames, stats, avg, mn, mx in results:
            lbl = cfg_label.replace("+", "\\texttt{+}")
            obb_str = _obb_skip_nodes(stats["obb_skipped"], stats["render_w"], stats["render_h"])
            hit_str = _cache_hit_rate(stats["cache_hits"], stats["cache_misses"])
            f.write(f"    {lbl} & {stats['tree_depth']} & {obb_str} & {hit_str}"
                    f" & {mn:.2f} & {avg:.2f} & {mx:.2f} \\\\\n")
        f.write("    \\bottomrule\n")
        f.write("  \\end{tabular}\n")
        f.write(f"  \\caption{{Configuration comparison for \\texttt{{{stem_tex}}}.}}\n")
        f.write(f"  \\label{{tab:rt-{stem_label}-summary}}\n")
        f.write("\\end{table}\n\n")

        # ── Per-config frame tables ──────────────────────────────────────────
        for cfg_label, frames, stats, avg, mn, mx in results:
            lbl_display = cfg_label.replace("+", "\\texttt{+}")
            safe        = cfg_label.replace(" ", "_").replace(",", "").replace("+", "")
            label_tex   = f"{stem_label}-{safe}".replace("_", "-")

            f.write("\\begin{table}[htbp]\n")
            f.write("  \\centering\\small\n")
            f.write("  \\setlength{\\tabcolsep}{8pt}\n")
            f.write("  \\begin{tabular}{@{}rcc@{}}\n")
            f.write("    \\toprule\n")
            f.write("    \\textbf{Frame} & \\textbf{Time (ms)} & \\textbf{FPS} \\\\\n")
            f.write("    \\midrule\n")
            for row in frames:
                f.write(f"    {row['step']} & {float(row['frame_ms']):.2f} & {float(row['fps']):.2f} \\\\\n")
            f.write("    \\bottomrule\n")
            f.write("  \\end{tabular}\n")
            f.write(f"  \\caption{{\\textbf{{{lbl_display}}} --- \\texttt{{{stem_tex}}}.}}\n")
            f.write(f"  \\label{{tab:rt-{label_tex}}}\n")
            f.write("\\end{table}\n\n")

    print(f"  LaTeX: {path}")


def main():
    p = argparse.ArgumentParser(
        description="Benchmark OpenSCAD GPU raytracer — outputs one Overleaf-ready folder per model.",
    )
    p.add_argument("models", nargs="+", help=".scad file(s) or folder(s)")
    p.add_argument("--steps",     type=int,   default=6)
    p.add_argument("--elevation", type=float, default=25.0)
    p.add_argument("--distance",  type=float, default=-1.0)
    p.add_argument("--width",     type=int,   default=1920, help="Viewport width in pixels")
    p.add_argument("--height",    type=int,   default=1080, help="Viewport height in pixels")
    p.add_argument("--output",    default="benchmark_results",
                   help="Top-level output folder")
    p.add_argument("--openscad",  default="./build/openscad")
    args = p.parse_args()

    models = collect_models(args.models)
    if not models:
        print("No .scad models found.")
        sys.exit(1)

    gpu = detect_gpu()
    print(f"GPU: {gpu}")

    total = len(models) * len(FIXED_CONFIGS)
    print(f"Found {len(models)} model(s) x {len(FIXED_CONFIGS)} config(s) = {total} run(s).")
    print(f"Output: {args.output}/")

    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    failed = []

    for model in models:
        stem = model.stem
        # Clean output folder for this model (the Overleaf-ready one)
        model_dir = pathlib.Path(args.output) / stem
        model_dir.mkdir(parents=True, exist_ok=True)
        # Raw per-config run outputs go here (not needed by Overleaf)
        runs_dir = model_dir / "_runs"
        runs_dir.mkdir(parents=True, exist_ok=True)

        results = []
        screenshot_dst = None

        for cfg in FIXED_CONFIGS:
            rc, avg, mn, mx, nframes, nodes, depth, render_w, render_h, \
                obb_skipped, cache_hits, cache_misses, frames, shot = \
                run_model(model, cfg, args, runs_dir, ts)

            if rc != 0 or avg is None:
                failed.append(f"{stem} [{cfg['label']}]")
                continue

            stats = {
                "node_count": nodes, "tree_depth": depth,
                "render_w": render_w, "render_h": render_h,
                "obb_skipped": obb_skipped,
                "cache_hits": cache_hits, "cache_misses": cache_misses,
            }
            results.append((cfg["label"], frames, stats, avg, mn, mx))

            # Copy the first available screenshot to model_dir/frame.png
            if screenshot_dst is None and shot.exists():
                screenshot_dst = model_dir / "frame.png"
                shutil.copy2(shot, screenshot_dst)

        if not results:
            continue

        print(f"\n[{stem}] Writing Overleaf folder: {model_dir}/")
        write_model_csv(model_dir, results)
        write_model_latex(model_dir, stem, results, screenshot_dst is not None, gpu)

    succeeded = total - len(failed)
    print(f"\nDone. {succeeded}/{total} succeeded.")
    if failed:
        print("Failed:", ", ".join(failed))

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
