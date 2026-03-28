// BENCH 03 — Many flat nodes (no deep nesting)
// 8×8×8 = 512 spheres in a 3-D grid.  No CSG operations between them —
// pure union of primitives.  Maximises the number of command-buffer entries
// the shader must walk per ray.
//
// PRIMARY AXIS: raw node count
// COMPARE:
//   u_use_obb = 1  vs  0   — culling should give a large speedup here
//   KD-tree   vs  naive    — binarization affects OBB tightness/depth
//
// Expected: with OBB off, FPS drops proportionally to N.
//           with OBB on,  rays cull most of the 512 nodes immediately.

$fn = 4;   // low subdivision — geometry is not the bottleneck

N       = 8;
spacing = 12;
r       = 3.5;

for (xi = [0:N-1])
  for (yi = [0:N-1])
    for (zi = [0:N-1])
      color([xi/(N-1), yi/(N-1), zi/(N-1)])
        translate([(xi - (N-1)/2) * spacing,
                   (yi - (N-1)/2) * spacing,
                   (zi - (N-1)/2) * spacing])
          sphere(r = r);
