// BENCH 04 — OBB culling: high-benefit case (objects spread in 3-D)
//
// 6×6×6 = 216 objects placed on a very wide grid (spacing = 50).
// Most rays cross only 1-2 objects' bounding volumes.
//
// PRIMARY AXIS: OBB culling effectiveness
// COMPARE:  u_use_obb = 1  vs  0
//
// With OBB ON  → shader culls ~214/216 objects per ray → fast.
// With OBB OFF → shader intersects all 216 primitives per ray → slow.
//
// This is the MAXIMUM benefit scenario for OBB culling.
// Also compare KD-tree vs naive binarization:
//   KD-tree builds tight spatial OBBs → even fewer false positives.
//   Naive linear chain → root OBB covers entire scene → less early culling.

$fn = 4;

N       = 6;
spacing = 50;   // very wide — objects are far apart

for (xi = [0:N-1])
  for (yi = [0:N-1])
    for (zi = [0:N-1]) {
      t = (xi + yi * N + zi * N * N) / (N*N*N - 1);
      color([t, 1-t, 0.5])
        translate([(xi - (N-1)/2) * spacing,
                   (yi - (N-1)/2) * spacing,
                   (zi - (N-1)/2) * spacing])
          sphere(r = 8);
    }
