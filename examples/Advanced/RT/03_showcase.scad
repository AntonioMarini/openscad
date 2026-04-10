// RT Showcase — three complex colored objects side by side.
// Tests: colored primitives, sphere/cube/cylinder combined, nested CSG.
$fn = 24;

// ── Left: hollow sphere with three equatorial holes ──────────────────────────
color("crimson")
translate([-15, 0, 0])
difference() {
  sphere(r = 5.5);
  sphere(r = 4.7);  // hollow shell
  for (a = [0, 60, 120, 180, 240, 300])
    rotate([0, 0, a]) translate([0, 4.4, 0]) sphere(r = 1.9);
}

// ── Center: cube with sphere corners and cylinder bore ───────────────────────
color("royalblue")
difference() {
  cube([10, 10, 10], center = true);
  // round off all 8 corners
  for (x = [-3.8, 3.8])
    for (y = [-3.8, 3.8])
      for (z = [-3.8, 3.8])
        translate([x, y, z]) sphere(r = 2.8);
  // central bore on each axis
  for (rot = [[0,0,0], [90,0,0], [0,90,0]])
    rotate(rot) cylinder(r = 1.7, h = 12, center = true);
}

// ── Right: cylinder stack with sphere knobs ───────────────────────────────────
color("forestgreen")
translate([15, 0, 0])
union() {
  // base disc
  cylinder(r = 5.5, h = 1, center = true);
  // shaft
  translate([0, 0, 4.5])  cylinder(r = 1.7, h = 8, center = true);
  // top knob
  translate([0, 0, 9]) sphere(r = 2.8);
  // three side knobs at mid-shaft
  for (a = [0, 120, 240])
    rotate([0, 0, a]) translate([2.2, 0, 4.5]) sphere(r = 1.4);
}
