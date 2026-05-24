
spacing = 2.5;

module primitives (spacing){
color("red")
translate([0,0,0])
cube(2, center=true);

color("green")
translate([spacing,0,0])
sphere(1);
translate([spacing*2,0,0])

color("blue")
cylinder(2,1,1,center=true);

translate([spacing*3,0,0])
cylinder(2,1,0, center=true);
}

primitives(spacing);
