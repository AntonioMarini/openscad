// 07_kd_postdist.scad
//
// Benchmark for KD re-binarisation after distribution.
//
// Structure:
//   INTERSECTION(UNION(A0,B0,C0, A1,B1,C1, ...), flat_slab)
//
// Distribution (rule 1) fires:
//   → UNION(A0∩slab, B0∩slab, C0∩slab, A1∩slab, ...)
//   → 3·N² new branches, ALL with valid (non-empty) OBBs.
//
// Without KD re-binarisation:
//   Union children ordered A,B,C,A,B,C,... → internal OBBs alternate
//   between x≈0 and x≈100 → poor spatial hierarchy.
//
// With KD re-binarisation:
//   Children sorted by X centroid after distribution:
//     A-cluster (x=0..35), C-cluster (x=50..85), B-cluster (x=100..135)
//   → non-overlapping subtree OBBs → rays toward A can skip B and C in one test.
//
// Cluster layout:
//          C  (top-centre, x=50..85, y=70..105)
//
//   A                          B
//  (x=0..35, y=0..35)     (x=100..135, y=0..35)
//
// N=8 per side per cluster → 3×64 = 192 leaves.
// Node count after distribution ≈ 2×192 − 1 + 192 ≈ 574 vs ≈ 383 before.

$fn = 20;

N = 8;    // spheres per side per cluster
s = 5;    // inter-sphere spacing
r = 1.8;  // sphere radius

cA = [  0,  0, 0];
cB = [100,  0, 0];
cC = [ 50, 70, 0];

// Slab: covers all three clusters in XY, thin in Z (clips each sphere to a disk).
// All 192 INTERSECTION(sphere, slab) branches have a valid, tight OBB.
slab_cx = (cB[0] + (N - 1) * s) / 2;
slab_cy = (cC[1] + (N - 1) * s) / 2;
slab_z  = r * 1.4;   // just wide enough to visibly clip each sphere

color("steelblue")
intersection() {
    union() {
        // Interleaved source order: A, B, C per iteration.
        // Naive split mixes all three clusters in each subtree.
        // KD (post-distribution) groups them by X position.
        for (row = [0 : N - 1], col = [0 : N - 1]) {
            translate(cA + [col * s, row * s, 0]) sphere(r);
            translate(cB + [col * s, row * s, 0]) sphere(r);
            translate(cC + [col * s, row * s, 0]) sphere(r);
        }
    }
    translate([slab_cx, slab_cy, 0])
        cube([cB[0] + (N + 1) * s, cC[1] + (N + 1) * s, slab_z], center = true);
}
