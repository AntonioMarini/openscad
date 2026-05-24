// CSG.scad - Basic example of CSG usage

csg(3);

module csg(s){
scale([s,s,s])
    difference(){
intersection() {
    color("magenta")
    cube(17, center=true);
    color("crimson")
    sphere(12);
}
color("blue")
sphere(11);
}
};