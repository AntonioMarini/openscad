// Swiss-cheese cube
// Tests deep difference: 1 cube minus 3×3×3 sphere grid (27 subtractions).
// Also tests MAX_STACK depth from the binarized difference chain.
$fn = 16;

color("goldenrod")
difference() {
  cube([32, 32, 32], center = true);

  for (x = [-10, 0, 10])
    for (y = [-10, 0, 10])
      for (z = [-10, 0, 10])
        translate([x, y, z])
          sphere(r = 5.5);
}
