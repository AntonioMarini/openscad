// Shadow + transparency demonstration scene.
//
// Transparency works when a semi-transparent object sits in front of another
// object with no spatial overlap (so the CSG union yields two separate spans).
//
// Light: upper-left-front (shader light0).
// Camera: look roughly from (+X, -Y, +Z) so the transparent sphere is between
//         you and the red sphere behind it.

$fn = 32;

// ── Opaque backdrop sphere (red) ──────────────────────────────────────────
color("tomato")
  sphere(r = 10);

// ── Semi-transparent glass sphere in front (alpha 0.3 → 70% see-through) ─
// Offset in +Y (toward the default camera) so it sits in front of the red one.
color([0.4, 0.8, 1.0, 0.3])
  translate([0, -28, 0])
    sphere(r = 9);

// ── Floor below both spheres – catches shadows and shows through the glass ─
color("lightgray")
  translate([0, 0, -12])
    cube([80, 80, 2], center = true);

// ── Opaque reference objects off to the side ─────────────────────────────
// Tall cylinder (left) – hard shadow on floor
color("steelblue")
  translate([-25, 0, -11])
    cylinder(r = 4, h = 26);

// Cube (right) – angled shadow
color("goldenrod")
  translate([20, 0, -11])
    rotate([0, 0, 20])
      cube([10, 10, 20]);

// ── Second transparency test: stacked translucent slabs ───────────────────
// A ray through both slabs should tint twice.
color([1.0, 0.3, 0.0, 0.5])   // orange, 50 % opaque
  translate([0, -12, 8])
    cube([16, 2, 16], center = true);

color([0.2, 0.9, 0.2, 0.5])   // green, 50 % opaque (sits behind the orange slab)
  translate([0, -15, 8])
    cube([16, 2, 16], center = true);
