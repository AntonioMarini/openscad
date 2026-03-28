// BENCH 10 — Combined stress: grid of complex CSG objects
//
// 5×5 = 25 instances of a moderately complex object (sphere shell with
// 6 holes + 2 cylinder bores), spread across a 3-D plane.
// This combines: many nodes, moderate operation depth, shared subtrees,
// and enough spacing for OBB culling to be meaningful.
//
// PRIMARY AXIS: all parameters simultaneously
// COMPARE (record FPS for each combination):
//   OBB  on/off  ×  Cache  on/off  →  2×2 = 4 measurements
//   Then swap binarization strategy and repeat → 8 total data points.
//
// This is the recommended "headline" benchmark for the thesis because it
// resembles a real scene and exercises every optimisation at once.

$fn = 14;

module unit_cell(col) {
  color(col)
  difference() {
    sphere(r = 8);
    sphere(r = 6);                          // hollow
    for (a = [0, 60, 120, 180, 240, 300])   // 6 radial holes
      rotate([90, 0, a]) cylinder(r = 2, h = 20, center = true);
    for (ax = [[0,0,0],[90,0,0],[0,90,0]])  // 3 axial bores
      rotate(ax) cylinder(r = 1.5, h = 20, center = true);
  }
}

N       = 5;
spacing = 28;

for (xi = [0:N-1])
  for (yi = [0:N-1])
    translate([(xi - (N-1)/2) * spacing,
               (yi - (N-1)/2) * spacing,
               0])
      unit_cell([xi/(N-1), yi/(N-1), 0.5]);
