#!/usr/bin/env python3
import argparse, csv, json, os, re, shutil, subprocess, sys, pathlib, datetime

FIXED_CONFIGS = [
   #{"label": "Baseline",            "binarization": 0, "obb": 0, "tbest": 0, "product_bvh": 0, "cache": 0, "distribution": 1, "shadows": 1, "dnf": 1},
    {"label": "Bounding Box Culling", "binarization": 0, "obb": 1, "tbest": 0, "product_bvh": 0, "cache": 0, "distribution": 1, "shadows": 1, "dnf": 1},
    {"label": "t culling",           "binarization": 0, "obb": 1, "tbest": 1, "product_bvh": 0, "cache": 0, "distribution": 1, "shadows": 1, "dnf": 1},
    {"label": "KD tree",             "binarization": 1, "obb": 1, "tbest": 1, "product_bvh": 1, "cache": 0, "distribution": 1, "shadows": 1, "dnf": 1},
]

BENCH_RE = re.compile(
    r"\[BENCH\].*total_frames=([0-9]+).*avg_fps=([0-9.]+).*min_fps=([0-9.]+).*max_fps=([0-9.]+)"
    r".*node_count=([0-9]+).*tree_depth=([0-9]+).*render_w=([0-9]+).*render_h=([0-9]+)"
    r".*nodes_visited=([0-9]+).*leaves_visited=([0-9]+)"
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


def run_model(model_path, cfg, args, runs_dir, ts, skip_screenshot=False):
    stem = model_path.stem
    safe = cfg["label"].replace(" ", "_").replace(",", "").replace("+", "")
    out = str(pathlib.Path(runs_dir) / f"{safe}_{ts}")
    total_steps = args.steps + args.warmup
    cmd = [
        args.openscad,
        str(model_path),
        "--benchmark",
        f"--bench-steps={total_steps}",
        f"--bench-output={out}",
        f"--rt-binarization={cfg['binarization']}",
        f"--rt-bounds={cfg['obb']}",
        f"--rt-cache={cfg['cache']}",
        f"--rt-shadows={cfg['shadows']}",
        f"--rt-tbest={cfg['tbest']}",
        f"--rt-product-bvh={cfg['product_bvh']}",
        f"--rt-distribution={cfg['distribution']}",
        f"--rt-dnf={cfg.get('dnf', 0)}",
        f"--bench-width={args.width}",
        f"--bench-height={args.height}",
    ]
    if skip_screenshot:
        cmd.append("--bench-no-screenshot")
    print(f"\n[{stem}] [{cfg['label']}] Running: {' '.join(cmd)}")
    if args.warmup > 0:
        print(f"  (warmup: discarding first {args.warmup} frame(s))")
    env = os.environ.copy()
    # XWayland + GLX: best path for NVIDIA compute shaders on Wayland
    env.setdefault("DISPLAY", ":1")
    env.setdefault("QT_QPA_PLATFORM", "xcb")
    env.setdefault("QT_XCB_GL_INTEGRATION", "xcb_glx")
    # Disable vsync at the NVIDIA driver level
    env["__GL_SYNC_TO_VBLANK"] = "0"

    proc = subprocess.Popen(
        cmd, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    node_count = tree_depth = render_w = render_h = None
    nodes_visited = leaves_visited = None
    for line in proc.stdout:
        print(line, end="", flush=True)
        m = BENCH_RE.search(line)
        if m:
            node_count, tree_depth = int(m[5]), int(m[6])
            render_w, render_h = int(m[7]), int(m[8])
            nodes_visited, leaves_visited = int(m[9]), int(m[10])
    proc.wait()

    frames = []
    mpath = pathlib.Path(out) / "measurements.csv"
    if mpath.exists():
        with open(mpath) as f:
            all_frames = list(csv.DictReader(f))
        # Discard warmup frames; renumber steps from 1
        frames = all_frames[args.warmup:]
        for i, row in enumerate(frames):
            row["step"] = str(i + 1)

    # Recompute statistics from the kept frames
    fps_values = [float(r["fps"]) for r in frames] if frames else []
    avg_fps = sum(fps_values) / len(fps_values) if fps_values else None
    min_fps = min(fps_values) if fps_values else None
    max_fps = max(fps_values) if fps_values else None
    total_frames = len(frames)

    screenshot = pathlib.Path(out) / "frame.png"

    if node_count is not None:
        stats_data = {
            "node_count": node_count, "tree_depth": tree_depth,
            "render_w": render_w, "render_h": render_h,
            "nodes_visited": nodes_visited, "leaves_visited": leaves_visited,
        }
        with open(pathlib.Path(out) / "stats.json", "w") as f:
            json.dump(stats_data, f)

    return (proc.returncode, avg_fps, min_fps, max_fps,
            total_frames, node_count, tree_depth, render_w, render_h,
            nodes_visited, leaves_visited, frames, screenshot)



def _per_pixel(count, render_w, render_h, total_frames):
    total = render_w * render_h * total_frames if (render_w and render_h and total_frames) else 0
    return f"{count / total:.2f}" if total else "N/A"


def _tex_escape(s):
    return s.replace("_", "\\_").replace("&", "\\&").replace("%", "\\%")


def write_summary_latex(out_dir, all_model_results, gpu):
    path = pathlib.Path(out_dir) / "summary.tex"

    render_w = render_h = None
    for _stem, results in all_model_results:
        if results:
            render_w = results[0][2]["render_w"]
            render_h = results[0][2]["render_h"]
            break
    res_str = f"${render_w}\\times{render_h}$" if render_w else "N/A"
    gpu_tex = _tex_escape(gpu)

    with open(path, "w") as f:
        f.write("% Generated by benchmark.py\n")
        f.write("% Requires booktabs, multirow in your preamble.\n\n")
        f.write("\\begin{table}[htbp]\n")
        f.write("  \\centering\\small\n")
        f.write("  \\setlength{\\tabcolsep}{4pt}\n")
        f.write(f"  \\caption{{Raytracer benchmark summary --- Resolution: {res_str}\\,px. GPU: {gpu_tex}.}}\n")
        f.write("  \\label{tab:rt-summary}\n")
        f.write("  \\begin{tabular}{@{}llrrrrrr@{}}\n")
        f.write("    \\toprule\n")
        f.write("    \\textbf{Model} & \\textbf{Config} & \\textbf{Nodes} & "
                "\\textbf{Depth} & \\textbf{Visited/px} & \\textbf{Leaves/px} & "
                "\\textbf{FPS avg} \\\\\n")
        f.write("    \\midrule\n")

        for i, (stem, results) in enumerate(all_model_results):
            if not results:
                continue
            if i > 0:
                f.write("    \\midrule\n")
            stem_short = stem.split("_", 1)[1] if "_" in stem else stem
            stem_tex   = _tex_escape(stem_short)
            n_rows     = len(results)
            for j, (cfg_label, _frames, stats, avg, mn, mx) in enumerate(results):
                lbl      = cfg_label.replace("+", "\\texttt{+}")
                vis_str  = _per_pixel(stats["nodes_visited"],  stats["render_w"], stats["render_h"], stats["total_frames"])
                leaf_str = _per_pixel(stats["leaves_visited"], stats["render_w"], stats["render_h"], stats["total_frames"])
                model_cell = (f"\\multirow{{{n_rows}}}{{*}}{{\\texttt{{{stem_tex}}}}}"
                              if j == 0 else "")
                f.write(f"    {model_cell} & {lbl} & {stats['node_count']} & "
                        f"{stats['tree_depth']} & {vis_str} & {leaf_str} & "
                        f"{avg:.2f} \\\\\n")

        f.write("    \\bottomrule\n")
        f.write("  \\end{tabular}\n")
        f.write("\\end{table}\n")

    print(f"  Summary LaTeX: {path}")


def write_scene_overview_latex(out_dir, all_model_results):
    path = pathlib.Path(out_dir) / "scene_overview.tex"
    with open(path, "w") as f:
        f.write("% Generated by benchmark.py\n")
        f.write("% Requires booktabs, graphicx, float in your preamble.\n\n")
        f.write("\\begin{table}[H]\n")
        f.write("  \\centering\\small\n")
        f.write("  \\setlength{\\tabcolsep}{6pt}\n")
        f.write("  \\caption{Models tested: model name, node count (before distribution), and preview.}\n")
        f.write("  \\label{tab:rt-scene-overview}\n")
        f.write("  \\begin{tabular}{@{}lrl@{}}\n")
        f.write("    \\toprule\n")
        f.write("    \\textbf{Model} & \\textbf{Nodes} & \\textbf{Preview} \\\\\n")
        f.write("    \\midrule\n")
        for stem, results in all_model_results:
            if not results:
                continue
            stem_tex = _tex_escape(stem)
            # Use node count from the first non-distribution config
            nodes = next(
                (s["node_count"] for lbl, _, s, _, _, _ in results
                 if "Dist" not in lbl),
                results[0][2]["node_count"]
            )
            img_path = f"{out_dir}/{stem}/frame.png"
            f.write(f"    \\texttt{{{stem_tex}}} & {nodes} & "
                    f"\\includegraphics[height=2.8cm]{{{img_path}}} \\\\\n")
        f.write("    \\bottomrule\n")
        f.write("  \\end{tabular}\n")
        f.write("\\end{table}\n")
    print(f"  Scene overview LaTeX: {path}")


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

        # ── Config comparison table ──────────────────────────────────────────
        f.write("\\begin{table}[htbp]\n")
        f.write("  \\centering\\small\n")
        f.write("  \\setlength{\\tabcolsep}{6pt}\n")
        f.write("  \\begin{tabular}{@{}lrrrrrr@{}}\n")
        f.write("    \\toprule\n")
        f.write("    \\textbf{Config} & \\textbf{Nodes} & \\textbf{Depth} & "
                "\\textbf{Visited/px} & \\textbf{Leaves/px} & "
                "\\textbf{FPS avg} \\\\\n")
        f.write("    \\midrule\n")
        for cfg_label, _frames, stats, avg, mn, mx in results:
            lbl      = cfg_label.replace("+", "\\texttt{+}")
            vis_str  = _per_pixel(stats["nodes_visited"],  stats["render_w"], stats["render_h"], stats["total_frames"])
            leaf_str = _per_pixel(stats["leaves_visited"], stats["render_w"], stats["render_h"], stats["total_frames"])
            f.write(f"    {lbl} & {stats['node_count']} & {stats['tree_depth']} & "
                    f"{vis_str} & {leaf_str} & {avg:.2f} \\\\\n")
        f.write("    \\bottomrule\n")
        f.write("  \\end{tabular}\n")
        f.write(f"  \\caption{{Configuration comparison for \\texttt{{{stem_tex}}}."
                f" Resolution: {res_str}\\,px. GPU: {gpu_tex}.}}\n")
        f.write(f"  \\label{{tab:rt-{stem_label}-summary}}\n")
        f.write("\\end{table}\n")

    print(f"  LaTeX: {path}")


def main():
    p = argparse.ArgumentParser(
        description="Benchmark OpenSCAD GPU raytracer — outputs one Overleaf-ready folder per model.",
    )
    p.add_argument("models", nargs="+", help=".scad file(s) or folder(s)")
    p.add_argument("--steps",     type=int,   default=6)
    p.add_argument("--warmup",    type=int,   default=2,
                   help="Frames to render but discard before recording (default: 2)")
    p.add_argument("--width",     type=int,   default=1920, help="Viewport width in pixels")
    p.add_argument("--height",    type=int,   default=1080, help="Viewport height in pixels")
    p.add_argument("--output",    default=None,
                   help="Top-level output folder (default: bench_<timestamp>)")
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
    if args.output is None:
        args.output = f"bench_{ts}"
    failed = []
    all_model_results = []

    for model in models:
        stem = model.stem
        # Clean output folder for this model (the Overleaf-ready one)
        model_dir = pathlib.Path(args.output) / stem
        model_dir.mkdir(parents=True, exist_ok=True)
        # Per-config run outputs go directly under model_dir
        runs_dir = model_dir

        results = []
        screenshot_dst = None

        for cfg_idx, cfg in enumerate(FIXED_CONFIGS):
            rc, avg, mn, mx, nframes, nodes, depth, render_w, render_h, \
                nodes_visited, leaves_visited, frames, shot = \
                run_model(model, cfg, args, runs_dir, ts, skip_screenshot=cfg_idx > 0)

            if rc != 0 or avg is None:
                failed.append(f"{stem} [{cfg['label']}]")
                continue

            stats = {
                "node_count": nodes, "tree_depth": depth,
                "render_w": render_w, "render_h": render_h,
                "nodes_visited": nodes_visited, "leaves_visited": leaves_visited,
                "total_frames": args.steps + args.warmup,
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
        all_model_results.append((stem, results))

    if all_model_results:
        write_summary_latex(args.output, all_model_results, gpu)
        write_scene_overview_latex(args.output, all_model_results)

    succeeded = total - len(failed)
    print(f"\nDone. {succeeded}/{total} succeeded.")
    if failed:
        print("Failed:", ", ".join(failed))

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
