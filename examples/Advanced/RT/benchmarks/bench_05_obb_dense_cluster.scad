// BENCH 05 — OBB culling: low-benefit case (all objects clustered together)
//
// Same 6×6×6 = 216 objects as bench_04 but with spacing = 1.5
// (objects nearly touching / overlapping).  Every ray passes through
// the bounding volume of nearly every object.
//
// PRIMARY AXIS: OBB culling in worst case
// COMPARE:  u_use_obb = 1  vs  0
//
// With OBB ON  → almost no culling possible → FPS similar to OBB OFF.
// With OBB OFF → same cost → the two lines should nearly overlap.
//
// The FPS delta (bench_04_OBB_on − bench_04_OBB_off)
//   vs (bench_05_OBB_on − bench_05_OBB_off)
// quantifies how scene sparsity amplifies OBB benefit.

$fn = 4;

N       = 6;
spacing = 1.5;   // very dense — all objects within a ~7-unit cube

for (xi = [0:N-1])
  for (yi = [0:N-1])
    for (zi = [0:N-1]) {
      t = (xi + yi * N + zi * N * N) / (N*N*N - 1);
      color([t, 1-t, 0.5])
        translate([(xi - (N-1)/2) * spacing,
                   (yi - (N-1)/2) * spacing,
                   (zi - (N-1)/2) * spacing])
          sphere(r = 1);
    }
