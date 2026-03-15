// Nested spherical shells with porthole windows
// Tests: deep CSG stack, span overflow guard (many overlapping spheres on a ray).
$fn = 24;

radii  = [20, 16, 12, 8];
colors = ["steelblue", "tomato", "gold", "mediumseagreen"];

for (i = [0 : len(radii)-1]) {
  r_out = radii[i];
  r_in  = (i < len(radii)-1) ? radii[i+1] + 0.5 : r_out * 0.5;

  color(colors[i])
  difference() {
    sphere(r = r_out);
    sphere(r = r_in);         // hollow out the inside

    // six axis-aligned portholes so inner shells are visible
    for (rot = [[0,0,0], [90,0,0], [0,90,0]])
      rotate(rot) cylinder(r = r_out * 0.25, h = r_out * 2.5, center = true);
  }
}
