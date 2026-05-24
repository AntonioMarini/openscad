// Menger sponge fractal — subtractive: cube minus cross-holes at each level
// NOTE: F5 preview may fail at higher levels due to CSG normalization explosion.
// Use the additive version (menger_sponge.scad) for standard OpenSCAD preview.

level = 2;

module menger_holes(s, lvl) {
  if (lvl > 0) {
    t = s / 3;
    // Cross-shaped holes: 3 axis-aligned beams through center
    cube([s + 0.1, t, t], center=true);
    cube([t, s + 0.1, t], center=true);
    cube([t, t, s + 0.1], center=true);

    // Recurse into 20 surviving sub-cube positions
    for (x = [-1, 0, 1])
      for (y = [-1, 0, 1])
        for (z = [-1, 0, 1])
          if ((x == 0 ? 0 : 1) + (y == 0 ? 0 : 1) + (z == 0 ? 0 : 1) >= 2)
            translate([x*t, y*t, z*t])
              menger_holes(t, lvl - 1);
  }
}

color("goldenrod")
difference() {
  cube(30, center=true);
  menger_holes(30, level);
}
