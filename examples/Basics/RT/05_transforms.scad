// non-uniform scale
scale([2, 1, 0.5]) sphere(r=5);

// rotated cylinder
translate([14, 0, 0]) rotate([90, 0, 0]) cylinder(h=10, r=3, center=true);

// non-centered cube at origin
translate([-14, 0, 0]) cube([6, 6, 6]);
