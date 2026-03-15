// Cube with 3 orthogonal cylinder holes — tests difference with rotated primitives
difference() {
    cube([12, 12, 12], center=true);
    cylinder(h=14, r=3, center=true);
    rotate([90, 0, 0]) cylinder(h=14, r=3, center=true);
    rotate([0, 90, 0]) cylinder(h=14, r=3, center=true);
}
