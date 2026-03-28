// BENCH 07 — Many concentric shells (MAX_SPANS stress)
//
// 8 concentric spherical shells in a union.
// A ray through the centre pierces all 8 shells → 8 separate CSG spans.
// With MAX_SPANS = 2, only the 2 outermost shells are visible; inner shells
// are silently clipped.  Increase MAX_SPANS (recompile shader) and observe:
//   - visual: more inner shells appear
//   - performance: each extra span costs one more merge_spans call
//
// PRIMARY AXIS: MAX_SPANS define in raytracer.glsl
// COMPARE:
//   MAX_SPANS = 2  (default)
//   MAX_SPANS = 4
//   MAX_SPANS = 8  ← should show all shells, maximum cost
//
// Also useful for measuring span-merge overhead independently of node count.

$fn = 32;

radii  = [28, 24, 20, 16, 12, 8, 5, 3];
colors = [
  "steelblue", "tomato", "goldenrod", "mediumseagreen",
  "mediumpurple", "coral", "deepskyblue", "lightsalmon"
];

gap = 1.5;   // gap between shell inner and next outer radius

for (i = [0 : len(radii)-1]) {
  r_out = radii[i];
  r_in  = (i < len(radii)-1) ? radii[i+1] + gap : r_out * 0.3;
  color(colors[i])
  difference() {
    sphere(r = r_out);
    sphere(r = r_in);
    // window so inner shells are visible from outside
    for (ax = [[0,0,0],[90,0,0],[0,90,0]])
      rotate(ax) cylinder(r = r_out * 0.35, h = r_out * 2.5, center = true);
  }
}
