// BENCH 02 — Medium baseline
// ~25 primitives with light CSG (differences and unions), mixed types.
// Representative of a typical real model.
//
// Expected: moderate FPS. Use as the "normal use" reference.
// No specific parameter to toggle — this is a calibration point.

$fn = 20;

// Central hollowed sphere
color("slategray")
difference() {
  sphere(r = 14);
  sphere(r = 11);
  for (a = [0, 60, 120, 180, 240, 300])
    rotate([0, 90, a]) cylinder(r = 3, h = 30, center = true);
}

// Ring of 8 cylinders around it
for (i = [0:7]) {
  a = i * 45;
  color([i/7, 1 - i/7, 0.5])
  translate([30 * cos(a), 30 * sin(a), 0])
    difference() {
      cylinder(r = 4, h = 12, center = true);
      cylinder(r = 2.5, h = 14, center = true);
    }
}
