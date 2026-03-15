// RT Showcase — three complex colored objects side by side.
// Tests: colored primitives, sphere/cube/cylinder combined, nested CSG.
$fn = 24;

// ── Left: hollow sphere with three equatorial holes ──────────────────────────
color("crimson")
translate([-28, 0, 0])
difference() {
  sphere(r = 10);
  sphere(r = 8.5);  // hollow shell
  for (a = [0, 60, 120, 180, 240, 300])
    rotate([0, 0, a]) translate([0, 8, 0]) sphere(r = 3.5);
}

// ── Center: cube with sphere corners and cylinder bore ───────────────────────
color("royalblue")
difference() {
  cube([18, 18, 18], center = true);
  // round off all 8 corners
  for (x = [-7, 7])
    for (y = [-7, 7])
      for (z = [-7, 7])
        translate([x, y, z]) sphere(r = 5);
  // central bore on each axis
  for (rot = [[0,0,0], [90,0,0], [0,90,0]])
    rotate(rot) cylinder(r = 3, h = 22, center = true);
}

// ── Right: cylinder stack with sphere knobs ───────────────────────────────────
color("forestgreen")
translate([28, 0, 0])
union() {
  // base disc
  cylinder(r = 10, h = 2, center = true);
  // shaft
  translate([0, 0, 8])  cylinder(r = 3, h = 14, center = true);
  // top knob
  translate([0, 0, 16]) sphere(r = 5);
  // three side knobs at mid-shaft
  for (a = [0, 120, 240])
    rotate([0, 0, a]) translate([4, 0, 8]) sphere(r = 2.5);
}
