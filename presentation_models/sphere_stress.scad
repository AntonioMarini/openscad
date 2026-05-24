$fn = 10;

N       = 10;
spacing = 6;
r       = 1.2;

for (xi = [0:N-1])
  for (yi = [0:N-1])
    for (zi = [0:N-1])
      color([xi / (N-1), yi / (N-1), zi / (N-1)])
        translate([(xi - (N-1)/2) * spacing,
                   (yi - (N-1)/2) * spacing,
                   (zi - (N-1)/2) * spacing])
          //difference(){         
          sphere(r);
            //translate([0.0,0.0,0-r])
            //cylinder(r*2, r/2,r/2);
          //}23
