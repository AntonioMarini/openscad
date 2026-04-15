// RT Distribution Showcase — all 5 rewrite rules plus combinations.
//
//   Rule 1: (A ∪ B) ∩ C        →  (A ∩ C) ∪ (B ∩ C)
//   Rule 2: A ∩ (B ∪ C)        →  (A ∩ B) ∪ (A ∩ C)
//   Rule 3: (A ∪ B) \ C        →  (A \ C) ∪ (B \ C)
//   Rule 4: A \ (B ∩ C)        →  (A \ B) ∪ (A \ C)
//   Rule 5: A \ (B \ C)        →  (A \ B) ∪ (A ∩ C)
//
// Row 1: single rules 1–3 (original).
// Row 2: single rules 4–5 + combo 1+2 (original).
// Row 3: two-rule combo chains (original).
// Row 4: dense single-rule — large unions (12–16 children) for more distribution.
// Row 5: deep chains — complex subtrees duplicated many times → cache stress test.
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
// Two spheres minus (cylinder minus inner sphere).
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
// parent becomes A ∩ ((B\C) ∪ (B\D)) and Rule 2 fires → 2 copies of A.
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

// ── Row 4 (y = -22): Dense single-rule — large unions ────────────────────────

// Rule 1 (dense) — full ring of 12 satellites + centre ∩ cube
// After distribution: 13 independent (sphere ∩ cube) pairs → cube duplicated 13×.
color("gold")
translate([-9, -22, 0])
intersection() {
  union() {
    sphere(r = 3.5);
    for (a = [0 : 30 : 350])
      rotate([0, 0, a]) translate([4.5, 0, 0]) sphere(r = 1.3);
  }
  cube([9, 9, 9], center = true);
}

// Rule 2 (dense) — sphere ∩ (union of 8 bars at 45° steps)
// After distribution: sphere duplicated 8×.
color("steelblue")
translate([0, -22, 0])
intersection() {
  sphere(r = 5);
  union() {
    for (a = [0 : 45 : 135])
      rotate([0, 0, a]) cube([2, 13, 2], center = true);
    for (a = [0 : 45 : 135])
      rotate([a, 0, 0]) cube([2, 13, 2], center = true);
  }
}

// Rule 3 (dense) — ring of 6 spheres \ cylinder
// After distribution: 6 independent (sphere \ cyl) pairs → cyl duplicated 6×.
color("tomato")
translate([9, -22, 0])
difference() {
  union() {
    for (a = [0, 60, 120, 180, 240, 300])
      rotate([0, 0, a]) translate([4, 0, 0]) sphere(r = 2.5);
  }
  cylinder(r = 1.4, h = 12, center = true);
}

// ── Row 5 (y = -33): Deep chains — complex subtrees duplicated ────────────────

// Combo Rules 2 + 4:  ComplexA ∩ ((B ∪ C ∪ D ∪ E) \ (F ∩ G))
// Step 1 (Rule 4): inner diff → (B∪C∪D∪E)\F ∪ (B∪C∪D∪E)\G
// Step 2 (Rule 3 on each): 4 terms\F ∪ 4 terms\G = 8 leaf differences
// Step 3 (Rule 2): ComplexA ∩ (8-term union) → ComplexA duplicated 8×.
// ComplexA = bored sphere (sphere \ cyl) = 2 primitives → cache stores 2-node result.
color("darkorange")
translate([-9, -33, 0])
intersection() {
  // ComplexA: sphere with axial bore — 2 primitives, cached 8×
  difference() {
    sphere(r = 4.5);
    cylinder(r = 1.2, h = 11, center = true);
  }
  difference() {
    union() {
      translate([-3,  0, 0]) sphere(r = 1.8);
      translate([ 3,  0, 0]) sphere(r = 1.8);
      translate([ 0, -3, 0]) sphere(r = 1.8);
      translate([ 0,  3, 0]) sphere(r = 1.8);
    }
    intersection() {
      cylinder(r = 1.5, h = 10, center = true);
      rotate([90, 0, 0]) cylinder(r = 1.5, h = 10, center = true);
    }
  }
}

// Combo Rules 1 + 4:  (A ∪ B ∪ C ∪ D ∪ E ∪ F) ∩ (G \ (H ∩ I))
// Step 1 (Rule 4): G\H ∪ G\I
// Step 2 (Rule 1 on each): 6 copies of (G\H) + 6 copies of (G\I) = 12 union terms.
// G = cube, H/I = cylinders → cube duplicated 12×.
color("orchid")
translate([0, -33, 0])
intersection() {
  union() {
    sphere(r = 3.5);
    for (a = [0, 60, 120, 180, 240, 300])
      rotate([0, 0, a]) translate([4, 0, 0]) sphere(r = 1.5);
  }
  difference() {
    cube([8, 8, 8], center = true);
    intersection() {
      cylinder(r = 1.6, h = 11, center = true);
      rotate([90, 0, 0]) cylinder(r = 1.6, h = 11, center = true);
    }
  }
}

// Triple combo Rules 3 + 5 + 2:  A ∩ ((B ∪ C ∪ D) \ (E \ F))
// Step 1 (Rule 5): (B∪C∪D)\E  ∪  (B∪C∪D)∩F
// Step 2 (Rule 3 on first, Rule 1 on second):
//   (B\E ∪ C\E ∪ D\E)  ∪  (B∩F ∪ C∩F ∪ D∩F) = 6 union terms
// Step 3 (Rule 2): A ∩ (6-term union) → A duplicated 6×.
// A = Steinmetz sphere (sphere \ (cyl_Z ∩ cyl_X)) — itself expands via Rule 4 → 2 copies.
// Final: 12 leaf operations.
color("deepskyblue")
translate([9, -33, 0])
intersection() {
  // A: sphere with Steinmetz bore — complex subtree, duplicated 6×
  difference() {
    sphere(r = 4.5);
    intersection() {
      cylinder(r = 1.4, h = 11, center = true);
      rotate([90, 0, 0]) cylinder(r = 1.4, h = 11, center = true);
    }
  }
  difference() {
    union() {
      translate([-3, 0, 0]) sphere(r = 2.2);
      translate([ 0, 0, 0]) sphere(r = 2.2);
      translate([ 3, 0, 0]) sphere(r = 2.2);
    }
    difference() {
      cylinder(r = 1.8, h = 9, center = true);
      sphere(r = 1.3);
    }
  }
}
