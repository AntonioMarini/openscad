// BENCH 09 — Cache: zero hit-rate baseline
//
// 16 objects each with a UNIQUE size/shape — no two subtrees are structurally
// identical, so duplicate_id is never set and the cache is never populated.
//
// PRIMARY AXIS: cache miss baseline
// COMPARE:  u_use_cache = 1  vs  0
//
// Expected: FPS identical with cache ON or OFF (no hits to exploit).
// The difference between bench_08 (cache ON) and this scene measures the
// maximum benefit the cache can provide for a fully-shared workload.

$fn = 16;

// Parameterised by index so every instance is geometrically unique
for (i = [0:15]) {
  xi = i % 4;
  yi = floor(i / 4);
  r       = 3.5 + i * 0.3;          // unique radius
  bore_r  = 1.0 + i * 0.1;          // unique bore
  notch   = 1.5 + i * 0.15;         // unique notch size

  color([xi/3, yi/3, i/15])
  translate([(xi - 1.5) * 22, (yi - 1.5) * 22, 0])
  difference() {
    union() {
      sphere(r = r);
      translate([0, 0, r])  cylinder(r = bore_r * 1.4, h = 3 + i*0.2, center=true);
      translate([0, 0, -r]) cylinder(r = bore_r * 1.4, h = 3 + i*0.2, center=true);
    }
    for (ax = [[0,0,0],[90,0,0],[0,90,0]])
      rotate(ax) cylinder(r = bore_r, h = r*3, center = true);
    translate([0, 0, r+1]) cube([notch, notch, notch], center = true);
  }
}
