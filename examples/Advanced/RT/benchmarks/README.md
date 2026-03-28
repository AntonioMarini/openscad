# RT Raytracer Benchmarks

Each scene targets one or more specific performance axes of the raytracer.
FPS is shown in the window title. Rotate the model to expose different
view-dependent workloads.

## Parameters to toggle (all in `src/glview/raytracer/RTGLView.cpp`)

| Parameter | Code location | Default |
|-----------|--------------|---------|
| OBB culling | `u_use_obb` uniform | 1 (on) |
| Span cache | `u_use_cache` uniform | 1 (on) |
| Shadows | `u_use_shadows` uniform | 1 (on) |

## Compile-time defines (in `shaders/raytracer/raytracer.glsl`)

| Define | Effect |
|--------|--------|
| `MAX_SPANS` | Max solid CSG regions stored per ray (default 2) |
| `MAX_STACK` | Max CSG operation nesting depth (default 16) |

## Binarization strategy (CPU side, `src/raytracer/RTCSGNode.h`)

The KD-tree vs naive binarization is selected at build time. Changing it
requires rebuilding after editing the binarizer call.

---

## Scene index

| File | Primary axis | Parameter to compare |
|------|-------------|----------------------|
| `bench_01_baseline_trivial.scad` | FPS ceiling | — |
| `bench_02_baseline_medium.scad` | Mid-complexity baseline | — |
| `bench_03_nodes_flat_large.scad` | Many flat nodes | OBB on/off |
| `bench_04_obb_spread_3d.scad` | OBB culling (high benefit) | `u_use_obb` |
| `bench_05_obb_dense_cluster.scad` | OBB culling (low benefit) | `u_use_obb` |
| `bench_06_deep_tree_ops.scad` | Stack depth / MAX_STACK | binarization strategy |
| `bench_07_span_shells.scad` | Span count / MAX_SPANS | `MAX_SPANS` define |
| `bench_08_cache_shared.scad` | Cache hit rate (high) | `u_use_cache` |
| `bench_09_cache_unique.scad` | Cache miss baseline | `u_use_cache` |
| `bench_10_grid_complex.scad` | Combined: nodes + ops + OBB | all |
