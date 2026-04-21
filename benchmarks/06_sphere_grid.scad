// 8x8x8 gradient sphere grid
$fn = 10;

N       = 8;
spacing = 6;
r       = 1.2;

for (xi = [0:N-1])
  for (yi = [0:N-1])
    for (zi = [0:N-1])
      let( a = (xi == 0 || yi == 0 || zi == 0) ? 0.5 :1.0)
      color([xi / (N-1), yi / (N-1), zi / (N-1), a])
        translate([(xi - (N-1)/2) * spacing,
                   (yi - (N-1)/2) * spacing,
                   (zi - (N-1)/2) * spacing])
          //difference(){         
          sphere(r);
            //translate([0.0,0.0,0-r])
            //cylinder(r*2, r/2,r/2);
          //}
