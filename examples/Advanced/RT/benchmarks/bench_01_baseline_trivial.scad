// BENCH 01 — Trivial baseline
// 3 primitives, no CSG operations.
// Use this to establish the FPS ceiling: the max the renderer can do
// when the traversal cost is negligible.
//
// Expected: highest FPS of all benchmarks.
// Toggle nothing — this is a fixed reference point.

$fn = 32;

color("steelblue")  sphere(r = 10);
color("tomato")     translate([28, 0, 0]) cube(14, center = true);
color("goldenrod")  translate([-28, 0, 0]) cylinder(r = 7, h = 18, center = true);
