// Greek column — cylinders, cubes, and sphere decorations.
// Tests: mixed primitive types, union chains, transforms.
$fn = 32;

color("ivory") {
  // Base slab
  cube([16, 16, 3], center = true);

  // Lower torus-like ring (two nested cylinders differenced)
  translate([0, 0, 3])
  difference() {
    cylinder(r = 7, h = 2, center = true);
    cylinder(r = 5, h = 3, center = true);
  }

  // Shaft (slightly tapered)
  translate([0, 0, 12]) cylinder(r1 = 5, r2 = 4, h = 16, center = true);

  // Neck groove
  translate([0, 0, 20.5])
  difference() {
    cylinder(r = 4.2, h = 1.5, center = true);
    cylinder(r = 3.5, h = 2,   center = true);
  }

  // Capital slab
  translate([0, 0, 22]) cube([14, 14, 2.5], center = true);

  // Entablature
  translate([0, 0, 24.5]) cube([18, 18, 2], center = true);

  // Four sphere corner decorations on entablature
  for (x = [-7, 7])
    for (y = [-7, 7])
      translate([x, y, 26]) sphere(r = 1.5);
}
