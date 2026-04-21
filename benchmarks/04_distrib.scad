// 02_distribution_showcase.scad
//
// Benchmark designed to isolate the OBB-culling benefit of CSG distribution.
//
// Core pattern:
//   INTERSECTION(UNION(A0, A1, ..., AN), clipper)
//   → UNION(A0∩clipper, A1∩clipper, ..., AN∩clipper)
//
// After distribution each branch OBB = OBB(Ai) ∩ OBB(clipper).
// Objects entirely outside the clipper produce a degenerate OBB →
// the GPU skips them with a single ray-OBB test.
//
// Scene A (y=0):   25 spheres in a row × narrow slab → ~20/25 branches culled.
// Scene B (y=70):  10×10 sphere grid  × narrow slab → ~90/100 branches culled.
// Scene C (y=160): INTERSECTION(union_4, union_64) — exercises the Rule1/Rule2
//                  overlap case. Left union is small (4 spheres), right union is
//                  large (64 spheres). With full DNF distribution both expand
//                  completely to 4×64=256 leaf intersections; the rule ordering
//                  determines the intermediate tree shape and OBB quality.
//
// Metric to watch: obb_skipped/px in the benchmark output.
// Config "Naive, OBB" vs "KD, OBB" shows the effect of distribution.

$fn = 24;

// ── Scene A: 1-D row ∩ slab ─────────────────────────────────────────────────
//
// 25 spheres spread over x = [0, 192].
// Slab covers only x ≈ [88, 136] → spheres 11..17 visible (7/25 ≈ 28%).
// With distribution: 18 branches have empty OBB → skipped immediately.

N_a  = 25;
sp_a = 8;
r_a  = 2.8;

color("steelblue")
intersection() {
    union() {
        for (i = [0 : N_a - 1])
            translate([i * sp_a, 0, 0]) sphere(r = r_a);
    }
    // Slab: centred at sphere 14, width covers ~6 spheres
    translate([14 * sp_a, 0, 0]) cube([6 * sp_a, 50, 50], center = true);
}

// ── Scene B: 2-D grid ∩ slab ─────────────────────────────────────────────────
//
// 10×10 = 100 spheres on a regular grid.
// Slab covers only column 5 (x ≈ 45..55) → 10/100 spheres visible.
// With distribution: 90 branches have empty OBB → skipped immediately.

N_b  = 10;
sp_b = 10;
r_b  = 3.2;

color("tomato")
translate([0, 70, 0])
intersection() {
    union() {
        for (row = [0 : N_b - 1], col = [0 : N_b - 1])
            translate([col * sp_b, row * sp_b, 0]) sphere(r = r_b);
    }
    // Slab: centred on column 5, wide enough for one column + margin
    translate([5 * sp_b, (N_b - 1) * sp_b / 2, 0])
        cube([sp_b * 1.4, (N_b + 2) * sp_b, (N_b + 2) * sp_b], center = true);
}

// ── Scene C: INTERSECTION(union_small, union_large) — overlap case ────────────
//
// Both children of the top-level INTERSECTION are UNIONs of very different sizes.
// Left:  2×2 grid of spheres (4 leaves, KD binary tree depth 2, 7 nodes).
// Right: 8×8 grid of spheres (64 leaves, KD binary tree depth 6, 127 nodes).
// Full DNF expands to 4×64 = 256 leaf intersections INTERSECTION(sphere_L, sphere_R).
// The rule-ordering fix (duplicate smaller side) determines the intermediate tree
// shape during recursion, which affects OBB tightness after KD rebinarization.

N_cL = 2;     // small left side: N_cL × N_cL = 4 spheres
N_cR = 8;     // large right side: N_cR × N_cR = 64 spheres
sp_c = 8;
r_c  = 2.5;

// The 4 left spheres are placed at the four corners of the 8×8 grid,
// so each left sphere overlaps only ~16 of the 64 right spheres.
// After full distribution, ~75% of the 256 leaf intersections have empty OBBs.
color("mediumseagreen")
translate([0, 160, 0])
intersection() {
    // Small left union: 4 spheres at grid corners, large radius to clip a quadrant each
    union() {
        translate([             0,              0, 0]) sphere(r = r_c * 5);
        translate([(N_cR-1)*sp_c,              0, 0]) sphere(r = r_c * 5);
        translate([             0, (N_cR-1)*sp_c, 0]) sphere(r = r_c * 5);
        translate([(N_cR-1)*sp_c, (N_cR-1)*sp_c, 0]) sphere(r = r_c * 5);
    }
    // Large right union: 8×8 grid
    union() {
        for (row = [0 : N_cR - 1], col = [0 : N_cR - 1])
            translate([col * sp_c, row * sp_c, 0]) sphere(r = r_c);
    }
}
