// RT Distribution Showcase — all 5 rewrite rules plus combinations.
//
//   Rule 1: (A ∪ B) ∩ C        →  (A ∩ C) ∪ (B ∩ C)
//   Rule 2: A ∩ (B ∪ C)        →  (A ∩ B) ∪ (A ∩ C)
//   Rule 3: (A ∪ B) \ C        →  (A \ C) ∪ (B \ C)
//   Rule 4: A \ (B ∩ C)        →  (A \ B) ∪ (A \ C)
//   Rule 5: A \ (B \ C)        →  (A \ B) ∪ (A ∩ C)
//
// Middle row: rules 4 & 5 plus a two-rule combo (1+2).
// Bottom row: three combos where two rules fire in sequence.
$fn = 24;

// ── Row 1 (y = +11): Single rules 1–3 ────────────────────────────────────────

// Rule 1 — (A ∪ B) ∩ C
// Central sphere + six radial satellites, all clipped by a cube.
// After distribution: 7 independent (primitive ∩ cube) pairs under a union.
color("gold")
translate([-9, 11, 0])
intersection() {
  union() {
    sphere(r = 3.5);
    for (a = [0, 60, 120, 180, 240, 300])
      rotate([0, 0, a]) translate([4.5, 0, 0]) sphere(r = 1.6);
  }
  cube([9, 9, 9], center = true);
}

// Rule 2 — A ∩ (B ∪ C)
// Three orthogonal bars (union) clipped by a sphere.
// After distribution: (sphere ∩ bar_Z) ∪ (sphere ∩ bar_X) ∪ (sphere ∩ bar_Y).
color("steelblue")
translate([0, 11, 0])
intersection() {
  sphere(r = 4.5);
  union() {
    cube([ 2.5,  2.5, 11], center = true);
    cube([11,  2.5,  2.5], center = true);
    cube([ 2.5, 11,  2.5], center = true);
  }
}

// Rule 3 — (A ∪ B) \ C
// Two overlapping spheres (peanut) with a cylindrical bore.
// After distribution: (sphere_L \ cyl) ∪ (sphere_R \ cyl).
color("tomato")
translate([9, 11, 0])
difference() {
  union() {
    translate([-2.5, 0, 0]) sphere(r = 3.5);
    translate([ 2.5, 0, 0]) sphere(r = 3.5);
  }
  cylinder(r = 1.5, h = 11, center = true);
}

// ── Row 2 (y = 0): Single rules 4–5 + combo 1+2 ──────────────────────────────

// Rule 4 — A \ (B ∩ C)
// Sphere with a Steinmetz solid (intersection of two crossed cylinders) removed.
// After distribution: (sphere \ cyl_Z) ∪ (sphere \ cyl_X).
color("orchid")
translate([-9, 0, 0])
difference() {
  sphere(r = 4);
  intersection() {
    cylinder(r = 1.75, h = 10, center = true);
    rotate([90, 0, 0]) cylinder(r = 1.75, h = 10, center = true);
  }
}

// Rule 5 — A \ (B \ C)
// Cube minus a spherical shell (outer sphere minus inner sphere).
// After distribution: (cube \ outer_sphere) ∪ (cube ∩ inner_sphere)
// — the shell scooped out of the cube, but the inner core is retained.
color("mediumseagreen")
translate([0, 0, 0])
difference() {
  cube([8, 8, 8], center = true);
  difference() {
    sphere(r = 5);
    sphere(r = 4);
  }
}

// Combo: Rules 1 + 2 — (A ∪ B) ∩ (C ∪ D)
// Two spheres intersected with a cross of two slabs.
// Rule 1 fires first → A∩(C∪D) ∪ B∩(C∪D);
// Rule 2 fires on each half → 4 union terms total.
color("darkorange")
translate([9, 0, 0])
intersection() {
  union() {
    translate([-2.5, 0, 0]) sphere(r = 3);
    translate([ 2.5, 0, 0]) sphere(r = 3);
  }
  union() {
    cube([10,  2.5,  2.5], center = true);
    cube([ 2.5,  2.5, 10], center = true);
  }
}

// ── Row 3 (y = -11): Two-rule combination chains ─────────────────────────────

// Combo: Rules 3 + 4 — (A ∪ B) \ (C ∩ D)
// Two spheres minus the Steinmetz solid of two crossed cylinders.
// Rule 3: (A\(C∩D)) ∪ (B\(C∩D));  Rule 4 on each half → 4 union terms.
color("coral")
translate([-9, -11, 0])
difference() {
  union() {
    translate([-2.5, 0, 0]) sphere(r = 3);
    translate([ 2.5, 0, 0]) sphere(r = 3);
  }
  intersection() {
    cylinder(r = 1.75, h = 10, center = true);
    rotate([90, 0, 0]) cylinder(r = 1.75, h = 10, center = true);
  }
}

// Combo: Rules 3 + 5 — (A ∪ B) \ (C \ D)
// Two spheres minus (cylinder minus inner sphere): bore drilled through
// the peanut, but a spherical core is preserved inside the bore.
// Rule 3: (A\(C\D)) ∪ (B\(C\D));  Rule 5 on each: (A\C ∪ A∩D) ∪ (B\C ∪ B∩D)
// → 4 union terms.
color("mediumpurple")
translate([0, -11, 0])
difference() {
  union() {
    translate([-2, 0, 0]) sphere(r = 3);
    translate([ 2, 0, 0]) sphere(r = 3);
  }
  difference() {
    cylinder(r = 2, h = 9, center = true);
    sphere(r = 1.5);
  }
}

// Combo: Rules 4 + 2 — A ∩ (B \ (C ∩ D))
// Rule 4 fires on the subtree first: B\(C∩D) → (B\C) ∪ (B\D);
// the parent then becomes A ∩ ((B\C) ∪ (B\D)) and Rule 2 fires → 2 union terms.
// Visually: sphere clipping a cube that has a Steinmetz notch cut in.
color("deepskyblue")
translate([9, -11, 0])
intersection() {
  sphere(r = 4.5);
  difference() {
    cube([7, 7, 7], center = true);
    intersection() {
      cylinder(r = 2, h = 10, center = true);
      rotate([90, 0, 0]) cylinder(r = 2, h = 10, center = true);
    }
  }
}
