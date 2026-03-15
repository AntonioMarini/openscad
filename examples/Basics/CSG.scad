// 6 sfere
$fn = 30;

color("crimson")
difference() {
  cube([10, 10, 10], center=true);
  union() {
    translate([4, 0, 0]) sphere(r=2);
    translate([-4, 0, 0]) sphere(r=2);
    translate([0, 4, 0]) sphere(r=2);
    translate([0, -4, 0]) sphere(r=2);
    translate([0, 0, 4]) sphere(r=2);
    translate([0, 0, -4]) sphere(r=2);
  }
}