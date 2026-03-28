// CSG stress test — maximises tree depth, span count, and DAG sharing.
// A sphere shell with 12 icosphere-like holes + 3 orthogonal cylinder bores.
// Rotate the model while watching performance.
$fn = 20;

SHELL_R  = 22;
HOLE_R   =  4;
BORE_R   =  4;

// Icosahedron-ish hole positions (12 vertices approximated with latitude rings)
hole_dirs = [
  // top cap
  [0, 0, 1],
  // upper ring (5 points)
  [sin(0)*sin(63),   cos(0)*sin(63),   cos(63)],
  [sin(72)*sin(63),  cos(72)*sin(63),  cos(63)],
  [sin(144)*sin(63), cos(144)*sin(63), cos(63)],
  [sin(216)*sin(63), cos(216)*sin(63), cos(63)],
  [sin(288)*sin(63), cos(288)*sin(63), cos(63)],
  // lower ring (5 points, offset by 36°)
  [sin(36)*sin(117),  cos(36)*sin(117),  cos(117)],
  [sin(108)*sin(117), cos(108)*sin(117), cos(117)],
  [sin(180)*sin(117), cos(180)*sin(117), cos(117)],
  [sin(252)*sin(117), cos(252)*sin(117), cos(117)],
  [sin(324)*sin(117), cos(324)*sin(117), cos(117)],
  // bottom cap
  [0, 0, -1],
];

color("slategray")
difference() {
  // Outer shell
  sphere(r = SHELL_R);
  sphere(r = SHELL_R - 2.5);   // hollow

  // Icosphere holes
  for (d = hole_dirs)
    translate(d * SHELL_R) sphere(r = HOLE_R);

  // Three orthogonal cylinder bores
  for (rot = [[0,0,0], [90,0,0], [0,90,0]])
    rotate(rot) cylinder(r = BORE_R, h = SHELL_R * 2.5, center = true);
}
