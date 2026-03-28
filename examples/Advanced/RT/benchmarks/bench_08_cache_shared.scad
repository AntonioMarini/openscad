// BENCH 08 — Cache: high hit-rate scene
//
// A complex module (8 primitives, 6 CSG ops internally) is instantiated
// 16 times.  The RTCSGNode duplicate-detection marks repeated subtree
// structures with a duplicate_id; the shader span-cache then reuses the
// computed interval list across rays that share the same subtree.
//
// PRIMARY AXIS: span cache hit rate
// COMPARE:  u_use_cache = 1  vs  0
//
// With cache ON  → subtree spans computed once per frame per unique subtree.
// With cache OFF → same subtree recomputed for every ray.
// The speedup factor should grow with instance count and subtree complexity.
//
// Also toggle u_use_obb to separate caching gain from culling gain.

$fn = 16;

module complex_unit(col) {
  color(col)
  difference() {
    union() {
      sphere(r = 5);
      translate([0, 0, 5]) cylinder(r = 2, h = 4, center = true);
      translate([0, 0, -5]) cylinder(r = 2, h = 4, center = true);
    }
    // three orthogonal bores
    for (ax = [[0,0,0],[90,0,0],[0,90,0]])
      rotate(ax) cylinder(r = 1.5, h = 14, center = true);
    // top notch
    translate([0, 0, 6]) cube([3, 3, 3], center = true);
  }
}

// 4×4 grid of the same complex unit
for (xi = [0:3])
  for (yi = [0:3])
    translate([(xi - 1.5) * 22, (yi - 1.5) * 22, 0])
      complex_unit([xi/3, yi/3, 0.6]);
