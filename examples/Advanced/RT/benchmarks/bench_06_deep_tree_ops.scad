// BENCH 06 — Deep operation tree (MAX_STACK stress + binarization comparison)
//
// Two sub-scenes side by side:
//   LEFT  — a wide difference(): 1 base + 14 subtractions = 15-child node.
//           The binarizer turns this into a 14-level binary chain.
//           → Hits near the MAX_STACK = 16 limit.
//   RIGHT — a wide union():  15 children.
//           KD-tree  → balanced binary tree, depth ≈ log2(15) ≈ 4.
//           Naive    → left-deep chain, depth 14.
//
// PRIMARY AXIS: tree depth / binarization strategy
// COMPARE:
//   KD-tree binarization  vs  naive linear binarization
//   → observe FPS difference and whether MAX_STACK is exceeded (artifacts)
//
// Raise MAX_STACK in the shader to eliminate overflow artifacts,
// then re-measure to isolate pure traversal cost.

$fn = 10;

// LEFT: deep difference — one sphere with 14 holes
translate([-40, 0, 0])
color("steelblue")
difference() {
  sphere(r = 22);
  // 14 subtracted spheres placed in a spiral so OBBs don't trivially cull
  for (i = [0:13]) {
    a = i * 360 / 14;
    h = (i - 6.5) * 2.5;
    translate([15 * cos(a), 15 * sin(a), h])
      sphere(r = 4.5);
  }
}

// RIGHT: wide union of 15 distinct objects
translate([40, 0, 0])
union() {
  for (i = [0:14]) {
    a = i * 360 / 15;
    color([i/14, 1-i/14, 0.5])
    translate([18 * cos(a), 18 * sin(a), 0])
      sphere(r = 4);
  }
}
