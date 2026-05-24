$fn = 10;

room_size_x = 80;
room_size_y = 80;
room_size_z = 80;
module room(){
union(){
translate([-room_size_x,0,0])
cube([room_size_x,1,room_size_z]);
translate([0,-room_size_y,0])
cube([1,room_size_y,room_size_z]);
translate([-room_size_x,-room_size_y,0])
cube([room_size_x,room_size_y,1]);
}
}

module booster(h, r) {
// cone + tube + cone
 booster_bottom_h = h/20;
 booster_pointy_h = h/15;
 tube_h = h - booster_bottom_h - booster_pointy_h;
 
 union(){
 color("grey")
 cylinder(booster_bottom_h,r+r/3,r); 
 
 color("white") 
 translate([0,0,booster_bottom_h])
 cylinder(tube_h,r,r);
 
 color("white")
 translate([0,0,booster_bottom_h+tube_h])
 cylinder(booster_pointy_h, r, r*0.2);
 }
};

module core_orion_connector(h, r1, r2){
   color("darkorange")
   cylinder(h, r1, r2);
}

module core_stage(h, r) {
 // motors + fuel cylinder
 motors_h = h/20;
 pre_motors_h = h/10;
 core_tube_h = h - motors_h - pre_motors_h;
 
 translate([0,0,motors_h])
 union(){
 translate([0,0,pre_motors_h]){
 color("orange")
 cylinder(core_tube_h,r,r);
 }
 color("white")
 cylinder(pre_motors_h, r,r);
 }
 
 // 4x engines
 union(){
 translate([r/2,r/2,0])
 rs_25_engine(motors_h, r/5);
  translate([-r/2,r/2,0])
 rs_25_engine(motors_h, r/5);
  translate([r/2,-r/2,0])
 rs_25_engine(motors_h, r/5);
  translate([-r/2,-r/2,0])
 rs_25_engine(motors_h, r/5);
 }
};

module rs_25_engine(h,r) {
   color([0.2,0.2,0.2])
   cylinder(h,r+0.35,r);
};

module orion(h,r1,r2) {
  orion_conn_h = h*0.5;
  orion_base_h = h - orion_conn_h;
  
  color("white")
  union(){
    translate([0,0,orion_base_h+orion_conn_h])
    cylinder(orion_conn_h, r2,r2*0.2);
    translate([0,0,orion_base_h])
    cylinder(orion_conn_h, r1,r2);
    cylinder(orion_base_h,r1,r1);
  }
};

core_h=25;
core_r = 2;

core_orion_h = core_h * 0.1;
core_orion_r1 = core_r;
core_orion_r2 = core_r*0.8;

orion_h = core_h*0.3;
orion_r1 = core_orion_r2;
orion_r2 = core_orion_r2*0.2;

union(){
translate([3.2,0,0])
booster(20,1);

translate([-3.2,0,0])
booster(20,1);

core_stage(core_h,core_r);

translate([0,0,core_h])
core_orion_connector(core_orion_h,core_orion_r1, core_orion_r2);

translate([0,0,core_h + core_orion_h])
orion(orion_h, orion_r1, orion_r2);
}

//color("lightyellow")
//cube([1000,1000,1]);
