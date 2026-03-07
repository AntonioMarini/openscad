#version 450 core

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout (rgba32f, binding = 0) uniform image2D imgOutput;

// depth texture: used for drawing things on top/behind later
layout (rgba32f, binding = 1) uniform image2D depthOutput;

uniform mat4 u_view;
uniform mat4 u_proj;

#define MAX_SPANS 16
#define MAX_STACK 8
#define MAX_COMMANDS 1024
#define MASK_WORDS (MAX_COMMANDS / 32)
#define STACK_MASK_WORDS (MAX_STACK / 32 + 1)

// CONSTANTS
const uint PRIMITIVE_TYPE_SPHERE = 1;
const uint PRIMITIVE_TYPE_CUBE = 2;
const uint PRIMITIVE_TYPE_CYLINDER = 4;

const uint OP_TYPE_OPUNION = 1;
const uint OP_TYPE_OPINTERSECTION = 2;
const uint OP_TYPE_OPDIFFERENCE = 4;

const uint ID_OP_TYPE_PRIMITIVE = 0;
const uint ID_OP_TYPE_OPERATION = 1;

const vec2 NO_HIT_SPAN = vec2(1.0/0.0, -1.0/0.0); // (inf, -inf)

// UNIFORMS
uniform int u_samples;
uniform int u_rendering_mode;
uniform vec3 u_camera_pos;
uniform mat4 u_inv_view;
uniform float fov;
uniform float aspectRatio;
uniform vec3 u_light_dir;
uniform vec3 u_background;
uniform vec3 u_default_mat_color;

uniform int u_use_obb;

// STRUCTS
struct Primitive {
    int type;
    int _pad[3];
    vec4 color;
    mat4 inv_transform;
    mat4 normal_mat;
};

struct Operation {
    int type;
    uint left_id;
    uint right_id;
    int _pad;
};

struct OBB {
    mat4 inv_transform;
    vec4 half_extents; // xyz half extents, w unused
};

struct CSGCommand {
    uint type;
    uint id;
    uint skip_children;
    int _pad;
    mat4 obb_inv_transform;
    uint obb_skip;
    uint obb_pad[3];
};

// SSBO
layout(std430, binding = 1) readonly buffer PrimitivesBuffer { Primitive primitives[]; };
layout(std430, binding = 2) readonly buffer OperationsBuffer { Operation operations[]; };
layout(std430, binding = 3) readonly buffer CommandsBuffer { CSGCommand commands[]; };

// RAY STRUCT
struct ray { vec3 origin; vec3 dir; };

float wireframe_box(vec3 ray_orig, vec3 ray_dir, vec3 b_min, vec3 b_max) {
    vec3 t0 = (b_min - ray_orig) / ray_dir;
    vec3 t1 = (b_max - ray_orig) / ray_dir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    
    float t_near = max(max(tmin.x, tmin.y), tmin.z);
    float t_far = min(min(tmax.x, tmax.y), tmax.z);

    if (t_near < t_far && t_near > 0.0) {
        vec3 p = ray_orig + ray_dir * t_near;
        vec3 d1 = abs(p - b_min);
        vec3 d2 = abs(p - b_max);
        vec3 d = min(d1, d2);         
        
        int borders = 0;
        float thickness = 0.02;         if (d.x < thickness) borders++;
        if (d.y < thickness) borders++;
        if (d.z < thickness) borders++;
        
        return (borders >= 2) ? 1.0 : 0.0;
    }
    return 0.0;
}

bool intersect_obb(vec3 ray_orig, vec3 ray_dir, mat4 inv_transform) {
    vec3 local_orig = (inv_transform * vec4(ray_orig, 1.0)).xyz;
    vec3 local_dir  = (inv_transform * vec4(ray_dir, 0.0)).xyz;
    vec3 inv_dir = 1.0 / (local_dir + vec3(1e-6));

    float margin = 1.01; // 1% margin
    vec3 t0 = (vec3(-margin) - local_orig) * inv_dir;
    vec3 t1 = (vec3( margin) - local_orig) * inv_dir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);

    float t_near = max(max(tmin.x, tmin.y), tmin.z);
    float t_far  = min(min(tmax.x, tmax.y), tmax.z);

    return t_near <= t_far && t_far > 0.0;
}

vec2 intersect_unit_sphere(ray r) {
    vec3 ro = r.origin;
    vec3 rd = r.dir;

    float a = dot(rd, rd);
    float b = 2.0 * dot(ro, rd);
    float c = dot(ro, ro) - 1.0;
    float delta = b*b - 4.0*a*c;

    if (delta < 0.0) {
        return NO_HIT_SPAN;
    }
    float sqrt_delta = sqrt(delta);
    return vec2(-b - sqrt_delta, -b + sqrt_delta) / (2.0 * a);
}

vec2 intersect_box_AABB(ray r) {
  vec2 span = NO_HIT_SPAN; // initiaal value is no hit.

  vec3 box_min = vec3(-.5);
  vec3 box_max = vec3(.5);

  vec3 ro = r.origin;
  vec3 rd = r.dir;
  
  vec3 t0 = (box_min - ro) / rd; 
  vec3 t1 = (box_max - ro) / rd;

  vec3 tmin = min(t0,t1);
  vec3 tmax = max(t0,t1);

  // find the largest near point:
  float t_enter = max(max(tmin.x, tmin.y), tmin.z);
  float t_exit = min(min(tmax.x, tmax.y), tmax.z);

  if(t_exit < t_enter){
    return span; // NO HIT
  }

  return vec2(t_enter, t_exit);
}

vec2 intersect_cylinder(ray r) {
    vec2 ro = r.origin.xz;
    vec2 rd = r.dir.xz;

    float a = dot(rd, rd);
    float b = 2.0 * dot(ro, rd);
    float c = dot(ro, ro) - 1.0; // Radius is 1.0

    float disc = b * b - 4.0 * a * c;

    // If discriminant is negative, ray misses the infinite tube entirely
    if (disc < 0.0) return vec2(1.0, -1.0); 

    float sqrtDisc = sqrt(disc);
    float t_tube_enter = (-b - sqrtDisc) / (2.0 * a);
    float t_tube_exit  = (-b + sqrtDisc) / (2.0 * a);

    float t_cap_bottom = (-0.5- r.origin.y) / r.dir.y;
    float t_cap_top    = ( 0.5 - r.origin.y) / r.dir.y;

    float t_cap_enter = min(t_cap_bottom, t_cap_top);
    float t_cap_exit  = max(t_cap_bottom, t_cap_top);

    float t_enter = max(t_tube_enter, t_cap_enter);
    float t_exit  = min(t_tube_exit,  t_cap_exit);

    return vec2(t_enter, t_exit);
}

vec3 get_local_normal(uint type, vec3 p) {
    if (type == PRIMITIVE_TYPE_SPHERE) return normalize(p);
    if (type == PRIMITIVE_TYPE_CUBE) {
        vec3 abs_dist = abs(p);
        float max_axis = max(max(abs_dist.x, abs_dist.y), abs_dist.z);
        return normalize(step(vec3(max_axis - 0.0001), abs_dist) * sign(p));
    }
    if (type == PRIMITIVE_TYPE_CYLINDER) {
        if (abs(p.y) > 0.499) return vec3(0.0, sign(p.y), 0.0);
        return normalize(vec3(p.x, 0.0, p.z));
    }
    return vec3(0,1,0);
}

vec3 get_final_color(vec3 world_pos, vec3 light_dir, Primitive prim, bool invert_normal){
    mat4 inv_mat = prim.inv_transform;
    vec3 local_pos = (inv_mat * vec4(world_pos, 1.0)).xyz;
    vec3 local_normal = get_local_normal(prim.type, local_pos);
    mat3 normal_matrix = mat3(prim.normal_mat);
    vec3 world_normal = normalize(normal_matrix * local_normal);
    if (invert_normal) world_normal = -world_normal;

    float ambient = 0.2;
    float diffuse = max(0.0, dot(world_normal, light_dir));
    return prim.color.rgb * (ambient + diffuse);
}

struct span { vec2 interval; uint primitive_id; bool invert_normal; };
struct interval_list { span spans[MAX_SPANS]; int count; };

bool is_inside(uint op, bool in_a, bool in_b) {
    if (op == OP_TYPE_OPUNION)        return in_a || in_b;
    if (op == OP_TYPE_OPINTERSECTION) return in_a && in_b;
    if (op == OP_TYPE_OPDIFFERENCE)   return in_a && !in_b;
    return false;
}

interval_list merge_spans(interval_list l_a, interval_list l_b, uint op){
    
    // early exit conditions
    // UNION: if one is empty, return the other
    // INTERSECTION: if one is empty, return empty
    // DIFFERENCE: if l_a is empty, return empty; if l_b is empty, return l_a
    switch(op) {
        case OP_TYPE_OPUNION:
            if (l_a.count == 0) return l_b;
            if (l_b.count == 0) return l_a;
            break;
        case OP_TYPE_OPINTERSECTION:
            if (l_a.count == 0 || l_b.count == 0) {
                interval_list empty; empty.count = 0; return empty;
            }
            break;
        case OP_TYPE_OPDIFFERENCE:
            if (l_a.count == 0) {
                interval_list empty; empty.count = 0; return empty;
            }
            if (l_b.count == 0) return l_a;
            break;
    }

    interval_list result; result.count = 0;
    int i = 0; int j = 0;
    bool in_a = false; bool in_b = false;
    bool last_in_result = false;
    float t_start = 0.0; uint start_prim_id = 0; bool start_inverted = false;

    while((i < l_a.count || j < l_b.count) && result.count < MAX_SPANS) {
        float t_a = (i < l_a.count) ? (in_a ? l_a.spans[i].interval.y : l_a.spans[i].interval.x) : 1.0/0.0;
        float t_b = (j < l_b.count) ? (in_b ? l_b.spans[j].interval.y : l_b.spans[j].interval.x) : 1.0/0.0;
        
        float current_t; uint current_prim; bool current_invert;

        if (t_a < t_b) {
            current_t = t_a;
            current_prim = l_a.spans[i].primitive_id;
            current_invert = l_a.spans[i].invert_normal;
            in_a = !in_a;
            if (!in_a) i++;
        } else {
            current_t = t_b;
            current_prim = l_b.spans[j].primitive_id;
            bool is_diff = (op == OP_TYPE_OPDIFFERENCE);
            current_invert = is_diff ? !l_b.spans[j].invert_normal : l_b.spans[j].invert_normal;
            in_b = !in_b;
            if (!in_b) j++;
        }

        bool in_result = is_inside(op, in_a, in_b);
        if (in_result != last_in_result) {
            if (in_result) {
                t_start = current_t; start_prim_id = current_prim; start_inverted = current_invert;
            } else {
                if (current_t > t_start + 0.001) {
                    int idx = result.count;
                    result.spans[idx].interval = vec2(t_start, current_t);
                    result.spans[idx].primitive_id = start_prim_id;
                    result.spans[idx].invert_normal = start_inverted;
                    result.count++;
                }
            }
            last_in_result = in_result;
        }
    }
    return result;
}

interval_list make_primitive_interval(vec2 span, uint id) {
    interval_list list;
    if (span.x >= span.y) { list.count = 0; } 
    else {
        list.count = 1;
        list.spans[0].interval = span;
        list.spans[0].primitive_id = id;
        list.spans[0].invert_normal = false;
    }
    return list;
}

void set_bit(inout uint mask[MASK_WORDS], int bit) {
    mask[bit / 32] |= (1u << (bit % 32));
}

bool get_bit(uint mask[MASK_WORDS], int bit) {
    return (mask[bit / 32] & (1u << (bit % 32))) != 0u;
}

void set_stack_bit(inout uint mask[STACK_MASK_WORDS], int bit) {
    mask[bit / 32] |= (1u << (bit % 32));
}

void clear_stack_bit(inout uint mask[STACK_MASK_WORDS], int bit) {
    mask[bit / 32] &= ~(1u << (bit % 32));
}

bool get_stack_bit(uint mask[STACK_MASK_WORDS], int bit) {
    return (mask[bit / 32] & (1u << (bit % 32))) != 0u;
}

vec3 csg_span(ray r, ivec2 pixel_coords) {
    vec3 inv_ray_dir = 1.0 / (r.dir + vec3(1e-6));
    uint num_ops = commands.length();
    uint root_id = num_ops - 1;
    CSGCommand root_cmd = commands[root_id];

    if (u_use_obb == 1) {
        // Early out: if ray misses root OBB, return background
        if (root_cmd.obb_skip == 1u || !intersect_obb(r.origin, r.dir, root_cmd.obb_inv_transform)) {
            imageStore(depthOutput, pixel_coords, vec4(1.0, 0.0, 0.0, 0.0));
            return u_background;
        }
    }

    // old skip mask
    //bool skip_mask[MAX_COMMANDS];
    //for (uint i = 0; i < num_ops; i++) skip_mask[i] = false;

   uint skip_mask[MASK_WORDS];
   for (int i = 0; i < MASK_WORDS; i++) skip_mask[i] = 0u;

    // First pass: determine which commands to skip based on AABB intersection
    // iterate inverse order: root -> leaves
    if(u_use_obb == 1){
        for (int i = int(num_ops) -1; i>=0; i--) {
          if (get_bit(skip_mask, i)) continue;

            CSGCommand cmd = commands[i];
            if (cmd.obb_skip == 1u || !intersect_obb(r.origin, r.dir, cmd.obb_inv_transform)) {
                // Mark children to skip
                uint skip_count = cmd.skip_children;
                for (int j = 0; j < int(skip_count); j++) {
                    if (i - j >= 0) set_bit(skip_mask, i - j);
                }
            }
        }
    }

    // Second pass: process commands, skipping those marked
    // iterate normal order: leaves -> root
    interval_list stack[MAX_STACK];
    uint stack_has_data[STACK_MASK_WORDS];
    for (int i = 0; i < STACK_MASK_WORDS; i++) stack_has_data[i] = 0u;
    int sp = 0;

    for (uint i = 0; i < num_ops; i++) {
        
        CSGCommand command = commands[i];
        bool is_skipped = get_bit(skip_mask, int(i));

        if (command.type == ID_OP_TYPE_PRIMITIVE) {
             if (is_skipped) {
                clear_stack_bit(stack_has_data, sp);
                sp++;            
             }else{
                Primitive p = primitives[command.id];
                ray local_ray;
				local_ray.origin = (p.inv_transform * vec4(r.origin, 1.0)).xyz;
				local_ray.dir    = (p.inv_transform * vec4(r.dir, 0.0)).xyz;
				
				vec2 hit = NO_HIT_SPAN;
				if (p.type == PRIMITIVE_TYPE_SPHERE) hit = intersect_unit_sphere(local_ray);
				if (p.type == PRIMITIVE_TYPE_CUBE)   hit = intersect_box_AABB(local_ray);
				if (p.type == PRIMITIVE_TYPE_CYLINDER) hit = intersect_cylinder(local_ray);
				
				if (hit.x < hit.y) {
                    stack[sp].count = 1;
                    stack[sp].spans[0].interval = hit;
                    stack[sp].spans[0].primitive_id = command.id;
                    stack[sp].spans[0].invert_normal = false;

                    set_stack_bit(stack_has_data, sp);
                } else {
                    clear_stack_bit(stack_has_data, sp);
                }
                sp++;            
            }
        } else {
            sp--; 
            bool has_op2 = get_stack_bit(stack_has_data, sp);
                        
            sp--;
            bool has_op1 = get_stack_bit(stack_has_data, sp);

             if (is_skipped || (!has_op1  && !has_op2 && command.type != OP_TYPE_OPUNION)) {
                // if both children empty and not an union -> result is empty
                clear_stack_bit(stack_has_data, sp); // update mask for this sp to 0
                sp++;
            }else {
                interval_list op2_val; op2_val.count = 0;
                if (has_op2) op2_val = stack[sp + 1]; 

                interval_list op1_val; op1_val.count = 0;
                if (has_op1) op1_val = stack[sp];    
               
                // merge the teo nodes spans lists.
                stack[sp] = merge_spans(op1_val, op2_val, operations[command.id].type);
                
                //finally update the stack_has_data bitmask               
                if(stack[sp].count > 0) set_stack_bit(stack_has_data, sp);
                else clear_stack_bit(stack_has_data, sp);
                
                sp++;            }
        }
    }

    if (sp > 0) {
        interval_list final_list = stack[0];
        float t = 1e10; int best_idx = -1;
        for (int k = 0; k < final_list.count; k++) {
            float t_enter = final_list.spans[k].interval.x;
            if (t_enter > 0.01 && t_enter < t) {
                t = t_enter; best_idx = k;
            }
        }
        if (best_idx != -1) {
            span hit = final_list.spans[best_idx];

            vec3 hitPos = r.origin + r.dir * t;

            // Write depth
            vec4 clipPos = u_proj * u_view * vec4(hitPos, 1.0);
            float depth = (clipPos.z / clipPos.w) * 0.5 + 0.5;
            imageStore(depthOutput, pixel_coords, vec4(depth, 0.0, 0.0, 0.0));
            
            return get_final_color(hitPos, normalize(u_light_dir), primitives[hit.primitive_id], hit.invert_normal);
        }
    }

    // No hit → background depth (1.0)
    imageStore(depthOutput, pixel_coords, vec4(1.0, 0.0, 0.0, 0.0));

    return u_background;
}

float hash(vec2 p) { return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); }

void main() {
    ivec2 pixel_coords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dims = imageSize(imgOutput);
    
    if (pixel_coords.x >= dims.x || pixel_coords.y >= dims.y) return;

    vec3 average_color = vec3(0.0);
    int samples = u_samples;    
     for(int s = 0; s < samples; s++) {
        vec2 jitter = vec2(hash(vec2(pixel_coords) + float(s)), hash(vec2(pixel_coords) + float(s)*2.0)) - 0.5;
        vec2 uv = (vec2(pixel_coords) + jitter) / vec2(dims);
        uv = uv * 2.0 - 1.0;
        
        float tanHalfFov = tan(radians(fov) * 0.5);
        vec3 rayDirLocal = normalize(vec3(uv.x * aspectRatio * tanHalfFov, uv.y * tanHalfFov, -1.0));
        vec3 rayDirWorld = normalize(mat3(u_inv_view) * rayDirLocal);

        ray r;
        r.origin = u_camera_pos;
        r.dir = rayDirWorld;

        vec3 sample_color = csg_span(r, pixel_coords);

        if (u_rendering_mode == 1) {
            float wire = 0.0;
            for (uint i = 0; i < commands.length(); i++) {
                CSGCommand cmd = commands[i];
                vec3 local_ro = (cmd.obb_inv_transform * vec4(r.origin, 1.0)).xyz;
                vec3 local_rd = (cmd.obb_inv_transform * vec4(r.dir, 0.0)).xyz;
                wire += wireframe_box(local_ro, local_rd, vec3(-1.0), vec3(1.0));
            }
            if (wire > 0.0) {
                sample_color = mix(sample_color, vec3(0.0, 1.0, 0.2), 0.6);
            }
        }
        
        average_color += sample_color;
    }    
    imageStore(imgOutput, pixel_coords, vec4(average_color / float(samples), 1.0));
}