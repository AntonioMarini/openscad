/*
Script Name: tree_fractal.scad
Description: This script generates a 3D model of tree fractal.
Date: 2024-06-27
Author: Chia-Jung, Yang
MakerWorld ID: jeroyang
Email: jeroyang@gmail.com

License: CC BY-SA 4.0

This work is licensed under the Creative Commons Attribution-ShareAlike 4.0 International License.
*/

// Tree fractal parameters

// Length of the initial trunk 
base_length = 15; //[5:30]

// Base angle for branches
angle = 15; 

// Reduction factor for the length of each branch
length_factor = 0.9; 

// Reduction factor for the thickness of each branch
thickness_factor = 0.7; 

// Number of recursive levels
depth = 10; //[2:10]

// Thickness of the initial trunk
initial_thickness = 7; 

// Angle of rotation around the Z-axis for spiraling effect
spiral_angle = 26;

// The diameter of the footplate
foot_diameter = 20;

/* [Hidden] */ 

// Size of the bushes (green blobs)
bush_size = 9;


// Generate the tree fractal
module tree_fractal(length, thickness, depth, current_spiral_angle, draw_bush=false) {
    if (depth > 0) {
        color("#6F4E37")

        // Draw the trunk
        cylinder(h = length, r1 = thickness, r2 = thickness * thickness_factor, center = false);

        // Move to the end of the trunk
        translate([0, 0, length]) {
            // Draw the left branch
            rotate([angle, 0, current_spiral_angle]) {
                tree_fractal(length * length_factor, thickness * thickness_factor, depth - 1, current_spiral_angle + spiral_angle, draw_bush);
            }
            
            // Draw the right branch
            rotate([-angle, 0, current_spiral_angle]) {
                tree_fractal(length * length_factor, thickness * thickness_factor, depth - 1, current_spiral_angle + spiral_angle, draw_bush);
            }
        }
    } else if (draw_bush) {
        // Draw the bush (green blob) at the end of the branch
        translate([0, 0, length]) {
            color("green")
            sphere(bush_size);
        }
    }
}

// Render the tree fractal
translate([0, 0, 0])
    color("yellow")
    cylinder(h = 2, r = foot_diameter, center = false);
    tree_fractal(base_length, initial_thickness, depth, 0, false);
