// BENCH — Emmental wheel wedge
// A quarter-section of a cheese wheel with many small spherical holes
// concentrated near the curved rind surface.
// Tests: many difference ops, OBB culling (most holes miss any given ray).
//
// PRIMARY AXIS: OBB culling
// COMPARE: u_use_obb on/off

$fn = 16;

R  = 26;   // wheel radius
H  = 10;   // wheel height

// ── Spherical holes near the rind ─────────────────────────────────────────────
// Two rings: one just inside the curved surface, one a bit deeper.
N_outer = 48;   // thin shell just inside the rind
N_inner = 32;   // slightly deeper, slightly larger

// Outer ring: r sampled in [R*0.72, R*0.90], small radii
ox  = rands(R * 0.55, R * 0.92, N_outer, 11);
oy  = rands(R * 0.55, R * 0.92, N_outer, 22);
oz  = rands(-H / 2 + 1.2, H / 2 - 1.2, N_outer, 33);
ors = rands(1.2, 2.5, N_outer, 44);

// Inner ring: r sampled in [R*0.45, R*0.72], slightly larger
ix  = rands(R * 0.15, R * 0.55, N_inner, 55);
iy  = rands(R * 0.15, R * 0.55, N_inner, 66);
iz  = rands(-H / 2 + 1.5, H / 2 - 1.5, N_inner, 77);
irs = rands(1.8, 3.2, N_inner, 88);

color([1.0, 0.84, 0.16])
difference() {
  // ── Wedge body: quarter-cylinder (90° sector in +x,+y quadrant) ────────────
  intersection() {
    cylinder(r = R, h = H, center = true, $fn = 64);
    translate([R / 2, R / 2, 0]) cube([R + 2, R + 2, H + 2], center = true);
  }

  // ── Near-surface holes ───────────────────────────────────────────────────────
  for (i = [0 : N_outer - 1])
    translate([ox[i], oy[i], oz[i]])
      sphere(r = ors[i]);

  // ── Interior holes ───────────────────────────────────────────────────────────
  for (i = [0 : N_inner - 1])
    translate([ix[i], iy[i], iz[i]])
      sphere(r = irs[i]);
}
