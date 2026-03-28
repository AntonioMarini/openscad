// Swiss-cheese cube
// Tests deep difference: 1 cube minus 3×3×3 sphere grid (27 subtractions).
// Also tests MAX_STACK depth from the binarized difference chain.
$fn = 16;

cube_l = 50;

color("goldenrod")
difference() {
  cube([cube_l, cube_l, cube_l], center = true);

  for (x = [-20,-10, 0, 10, 20])
    for (y = [-20,-10, 0, 10, 20])
      for (z = [-20,-10, 0, 10, 20])
        translate([x, y, z])
          sphere(r = 6.5);
}
