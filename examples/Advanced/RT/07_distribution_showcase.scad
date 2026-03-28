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

// ── Row 1 (y = +34): Single rules 1–3 ────────────────────────────────────────

// Rule 1 — (A ∪ B) ∩ C
// Central sphere + six radial satellites, all clipped by a cube.
// After distribution: 7 independent (primitive ∩ cube) pairs under a union.
color("gold")
translate([-28, 34, 0])
intersection() {
  union() {
    sphere(r = 7);
    for (a = [0, 60, 120, 180, 240, 300])
      rotate([0, 0, a]) translate([9, 0, 0]) sphere(r = 3.2);
  }
  cube([18, 18, 18], center = true);
}

// Rule 2 — A ∩ (B ∪ C)
// Three orthogonal bars (union) clipped by a sphere.
// After distribution: (sphere ∩ bar_Z) ∪ (sphere ∩ bar_X) ∪ (sphere ∩ bar_Y).
color("steelblue")
translate([0, 34, 0])
intersection() {
  sphere(r = 9);
  union() {
    cube([ 5,  5, 22], center = true);
    cube([22,  5,  5], center = true);
    cube([ 5, 22,  5], center = true);
  }
}

// Rule 3 — (A ∪ B) \ C
// Two overlapping spheres (peanut) with a cylindrical bore.
// After distribution: (sphere_L \ cyl) ∪ (sphere_R \ cyl).
color("tomato")
translate([28, 34, 0])
difference() {
  union() {
    translate([-5, 0, 0]) sphere(r = 7);
    translate([ 5, 0, 0]) sphere(r = 7);
  }
  cylinder(r = 3, h = 22, center = true);
}

// ── Row 2 (y = 0): Single rules 4–5 + combo 1+2 ──────────────────────────────

// Rule 4 — A \ (B ∩ C)
// Sphere with a Steinmetz solid (intersection of two crossed cylinders) removed.
// After distribution: (sphere \ cyl_Z) ∪ (sphere \ cyl_X).
color("orchid")
translate([-28, 0, 0])
difference() {
  sphere(r = 8);
  intersection() {
    cylinder(r = 3.5, h = 20, center = true);
    rotate([90, 0, 0]) cylinder(r = 3.5, h = 20, center = true);
  }
}

// Rule 5 — A \ (B \ C)
// Cube minus a spherical shell (outer sphere minus inner sphere).
// After distribution: (cube \ outer_sphere) ∪ (cube ∩ inner_sphere)
// — the shell scooped out of the cube, but the inner core is retained.
color("mediumseagreen")
translate([0, 0, 0])
difference() {
  cube([16, 16, 16], center = true);
  difference() {
    sphere(r = 10);
    sphere(r = 8);
  }
}

// Combo: Rules 1 + 2 — (A ∪ B) ∩ (C ∪ D)
// Two spheres intersected with a cross of two slabs.
// Rule 1 fires first → A∩(C∪D) ∪ B∩(C∪D);
// Rule 2 fires on each half → 4 union terms total.
color("darkorange")
translate([28, 0, 0])
intersection() {
  union() {
    translate([-5, 0, 0]) sphere(r = 6);
    translate([ 5, 0, 0]) sphere(r = 6);
  }
  union() {
    cube([20,  5,  5], center = true);
    cube([ 5,  5, 20], center = true);
  }
}

// ── Row 3 (y = -34): Two-rule combination chains ─────────────────────────────

// Combo: Rules 3 + 4 — (A ∪ B) \ (C ∩ D)
// Two spheres minus the Steinmetz solid of two crossed cylinders.
// Rule 3: (A\(C∩D)) ∪ (B\(C∩D));  Rule 4 on each half → 4 union terms.
color("coral")
translate([-28, -34, 0])
difference() {
  union() {
    translate([-5, 0, 0]) sphere(r = 6);
    translate([ 5, 0, 0]) sphere(r = 6);
  }
  intersection() {
    cylinder(r = 3.5, h = 20, center = true);
    rotate([90, 0, 0]) cylinder(r = 3.5, h = 20, center = true);
  }
}

// Combo: Rules 3 + 5 — (A ∪ B) \ (C \ D)
// Two spheres minus (cylinder minus inner sphere): bore drilled through
// the peanut, but a spherical core is preserved inside the bore.
// Rule 3: (A\(C\D)) ∪ (B\(C\D));  Rule 5 on each: (A\C ∪ A∩D) ∪ (B\C ∪ B∩D)
// → 4 union terms.
color("mediumpurple")
translate([0, -34, 0])
difference() {
  union() {
    translate([-4, 0, 0]) sphere(r = 6);
    translate([ 4, 0, 0]) sphere(r = 6);
  }
  difference() {
    cylinder(r = 4, h = 18, center = true);
    sphere(r = 3);
  }
}

// Combo: Rules 4 + 2 — A ∩ (B \ (C ∩ D))
// Rule 4 fires on the subtree first: B\(C∩D) → (B\C) ∪ (B\D);
// the parent then becomes A ∩ ((B\C) ∪ (B\D)) and Rule 2 fires → 2 union terms.
// Visually: sphere clipping a cube that has a Steinmetz notch cut in.
color("deepskyblue")
translate([28, -34, 0])
intersection() {
  sphere(r = 9);
  difference() {
    cube([14, 14, 14], center = true);
    intersection() {
      cylinder(r = 4, h = 20, center = true);
      rotate([90, 0, 0]) cylinder(r = 4, h = 20, center = true);
    }
  }
}
