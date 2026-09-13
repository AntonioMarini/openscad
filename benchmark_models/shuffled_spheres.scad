// kd_shuffled_clusters.scad

N       = 12;    // spheres per side per cluster
r       = 2;
spacing = 6;    

cA = [  0,  0, 0];
cB = [200,  0, 0];    
cC = [ 100, 200, 0];   

union() {
    for (row = [0 : N-1], col = [0 : N-1], depth= [0:N-1]) {
        // one sphere from each cluster per iteration.
        color("red")
        translate(cA + [col * spacing, row * spacing, depth * spacing]) sphere(r);
        color("green")
        translate(cB + [col * spacing, row * spacing, depth * spacing]) sphere(r);
        color("blue")
        translate(cC + [col * spacing, row * spacing, depth * spacing]) sphere(r);
    }
}
