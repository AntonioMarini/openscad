// 02_distribution_showcase.scad
//
// Benchmark designed to isolate the OBB-culling benefit of CSG distribution

N_cR = 40;     // large right side: N_cR × N_cR = 64 spheres
sp_c = 20;
r_c  = 4.5;


color("red")
intersection() {
    union() {
        for (row = [0 : N_cR - 1], col = [0 : N_cR - 1])
            translate([col * sp_c, row * sp_c, 0]) sphere(r = r_c);
    }
    cube(N_cR * sp_c);
}
