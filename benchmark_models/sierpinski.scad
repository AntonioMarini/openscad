// Sierpinski tetrahedron fractal — additive (union of 4 corner sub-tetrahedra)
// Uses only cube intersections so the raytracer can handle it as CSG primitives.
// Union-of-intersections is already in sum-of-products form, so F5 preview
// normalizes trivially (no combinatorial explosion).

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

// Recursively place 4 half-scale tetrahedra at the corners.
module sierpinski(edge, lvl) {
  if (lvl == 0) {
    tetrahedron(edge);
  } else {
    d = edge * sqrt(6) / 8;
    for (n = [[1,1,1], [1,-1,-1], [-1,1,-1], [-1,-1,1]])
      translate(d * n / sqrt(3))
        sierpinski(edge / 2, lvl - 1);
  }
}

edge = 40 * sqrt(2);

rotate([45,-35,0])
color("goldenrod")
sierpinski(edge, level);
