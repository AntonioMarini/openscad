// Sierpinski tetrahedron fractal
// 4 sub-copies per level → 4^level leaf tetrahedra (each = intersection of 4 cubes)

level = 5;

// Tetrahedron via intersection of 4 rotated+translated cubes.
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

// Sierpinski recursion using midpoint vertices
module sierpinski(verts, lvl) {
  if (lvl == 0) {
    center = (verts[0] + verts[1] + verts[2] + verts[3]) / 4;
    edge = norm(verts[0] - verts[1]);
    translate(center)
      tetrahedron(edge);
  } else {
    for (i = [0:3])
      sierpinski([for (j = [0:3]) (verts[i] + verts[j]) / 2], lvl - 1);
  }
}

v0 = [1, 1, 1];
v1 = [1, -1, -1];
v2 = [-1, 1, -1];
v3 = [-1, -1, 1];

color("goldenrod")
sierpinski([v0 * 20, v1 * 20, v2 * 20, v3 * 20], level);
