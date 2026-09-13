// Sierpinski tetrahedron fractal — subtractive
// Starts with a solid tetrahedron, recursively subtracts inverted half-scale
// tetrahedra at the center of each surviving sub-region.
// NOTE: F5 preview fails at level ≥3 due to CSG normalization explosion.
// Use the additive version (sierpinski.scad) for standard OpenSCAD preview.

level = 3;

// Tetrahedron via intersection of 4 rotated+translated cubes, centered at origin.
module tetrahedron(edge) {
  big = edge * 2;
  r = edge * sqrt(6) / 12;
  off = big / 2 - r;

  a1 = acos(1 / sqrt(3));
  a2 = acos(-1 / sqrt(3));

  n0 = [1, 1, 1] / sqrt(3);
  n1 = [1, -1, -1] / sqrt(3);
  n2 = [-1, 1, -1] / sqrt(3);
  n3 = [-1, -1, 1] / sqrt(3);

  intersection() {
    translate(off * n0) rotate(a=a1, v=[-1, 1, 0])  cube(big, center=true);
    translate(off * n1) rotate(a=a2, v=[1, 1, 0])   cube(big, center=true);
    translate(off * n2) rotate(a=a2, v=[-1, -1, 0]) cube(big, center=true);
    translate(off * n3) rotate(a=a1, v=[1, -1, 0])  cube(big, center=true);
  }
}

// Collect all voids: at each level, an inverted half-scale tetrahedron at center,
// then recurse into the 4 corner sub-tetrahedra.
// rotate([0,0,90]) maps the tetrahedron to its dual (inverted) orientation.
module sierpinski_holes(edge, lvl) {
  if (lvl > 0) {
    rotate([0, 0, 90]) tetrahedron(edge / 2);

    // Corner sub-tetrahedra centers: halfway from origin to each vertex
    // Circumradius R = edge * sqrt(6) / 4, offset = R / 2
    d = edge * sqrt(6) / 8;
    for (n = [[1,1,1], [1,-1,-1], [-1,1,-1], [-1,-1,1]])
      translate(d * n / sqrt(3))
        sierpinski_holes(edge / 2, lvl - 1);
  }
}

edge = 40 * sqrt(2);

color("goldenrod")
difference() {
  tetrahedron(edge);
  sierpinski_holes(edge, level);
}
