// 4×4×4 gradient sphere grid
// Tests OBB culling (64 objects), span merging, and color interpolation.
$fn = 5;

N       = 5;
spacing = 10;
r       = 2;

for (xi = [0:N-1])
  for (yi = [0:N-1])
    for (zi = [0:N-1])
      color([xi / (N-1), yi / (N-1), zi / (N-1)])
        translate([(xi - (N-1)/2) * spacing,
                   (yi - (N-1)/2) * spacing,
                   (zi - (N-1)/2) * spacing])
          sphere(r);