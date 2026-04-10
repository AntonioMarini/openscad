// Swiss-cheese cube
// Tests deep difference: 1 cube minus 3×3×3 sphere grid (27 subtractions).
// Also tests MAX_STACK depth from the binarized difference chain.
$fn = 16;

cube_l = 25;

color("goldenrod")
difference() {
  cube([cube_l, cube_l, cube_l], center = true);

  for (x = [-10, -5, 0, 5, 10])
    for (y = [-10, -5, 0, 5, 10])
      for (z = [-10, -5, 0, 5, 10])
        translate([x, y, z])
          sphere(r = 3.2);
}
