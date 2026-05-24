// Menger sponge fractal — additive (union of 20 sub-cubes per level)
// Union-of-cubes is already in sum-of-products form, so F5 preview
// normalizes trivially.

level = 1;

module menger(s, lvl) {
  if (lvl == 0) {
    cube(s, center=true);
  } else {
    t = s / 3;
    for (x = [-1, 0, 1])
      for (y = [-1, 0, 1])
        for (z = [-1, 0, 1])
          if ((x == 0 ? 0 : 1) + (y == 0 ? 0 : 1) + (z == 0 ? 0 : 1) >= 2)
            translate([x*t, y*t, z*t])
              menger(t, lvl - 1);
  }
}

color("goldenrod")
menger(30, level);
