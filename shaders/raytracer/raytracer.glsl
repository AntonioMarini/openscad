#version 450 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba32f, binding = 0) uniform image2D imgOutput;

// depth texture: used for drawing things on top/behind later
layout(rgba32f, binding = 1) uniform image2D depthOutput;

uniform mat4 u_view;
uniform mat4 u_proj;

#define MAX_SPANS 2
#define MAX_STACK 16
#define CACHE_SIZE 16

// CONSTANTS
const uint PRIMITIVE_TYPE_SPHERE = 1;
const uint PRIMITIVE_TYPE_CUBE = 2;
const uint PRIMITIVE_TYPE_CYLINDER = 4;

const uint OP_TYPE_OPUNION = 1;
const uint OP_TYPE_OPINTERSECTION = 2;
const uint OP_TYPE_OPDIFFERENCE = 4;

const uint ID_OP_TYPE_PRIMITIVE = 0;
const uint ID_OP_TYPE_OPERATION = 1;
const uint ID_OP_TYPE_CACHED_REF = 2; // node type cached

const vec2 NO_HIT_SPAN = vec2(1.0 / 0.0, -1.0 / 0.0); // (inf, -inf)

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
uniform int u_use_cache;
uniform int u_cache_size;

// STRUCTS
struct Primitive {
  int type;
  float r1;
  float r2;
  int _pad;
  vec4 color;
  mat4 inv_transform;
};

struct Operation {
  int type;
  uint left_id;
  uint right_id;
  int _pad;
};

struct CSGCommand {
  uint type;
  uint id;
  uint skip_children;
  uint duplicate_id;
};

struct OBBData {
  mat4 inv_transform;
  uint skip;
  uint _pad[3];
};

// SSBO
layout(std430, binding = 1) readonly buffer PrimitivesBuffer {
  Primitive primitives[];
};
layout(std430, binding = 2) readonly buffer OperationsBuffer {
  Operation operations[];
};
layout(std430, binding = 3) readonly buffer CommandsBuffer {
  CSGCommand commands[];
};
layout(std430, binding = 4) readonly buffer OBBBuffer {
  OBBData obbs[];
};

// bit 31 = invert_normal, bits 0-30 = primitive_id
struct span {
  vec2 interval;
  uint packed_prim;
};

uint span_prim_id(span s) {
  return s.packed_prim & 0x7FFFFFFFu;
}
bool span_invert(span s) {
  return (s.packed_prim & 0x80000000u) != 0u;
}
uint pack_span_prim(uint id, bool invert) {
  return id | (invert ? 0x80000000u : 0u);
}

struct interval_list {
  span spans[MAX_SPANS];
  int count;
};

// --- CACHE ---
struct cache_entry {
  uint duplicate_id;
  interval_list spans;
};
cache_entry span_cache[CACHE_SIZE];
uint cache_current_id = 0;

#define SCACHE(i) span_cache[i]

int find_in_cache(uint dup_id) {
  for (int i = 0; i < u_cache_size; i++) {
    if (SCACHE(i).duplicate_id == dup_id) return i;
  }
  return -1;
}

void save_to_cache(uint dup_id, interval_list il) {
  SCACHE(cache_current_id).duplicate_id = dup_id;
  SCACHE(cache_current_id).spans = il;
  cache_current_id = (cache_current_id + 1) % u_cache_size;
}
// --- CACHE ---

// RAY STRUCT
struct ray {
  vec3 origin;
  vec3 dir;
};

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
    float thickness = 0.02;
    if (d.x < thickness) borders++;
    if (d.y < thickness) borders++;
    if (d.z < thickness) borders++;

    return (borders >= 2) ? 1.0 : 0.0;
  }
  return 0.0;
}

bool intersect_obb(vec3 ray_orig, vec3 ray_dir, mat4 inv_transform) {
  vec3 local_orig = (inv_transform * vec4(ray_orig, 1.0)).xyz;
  vec3 local_dir = (inv_transform * vec4(ray_dir, 0.0)).xyz;
  vec3 inv_dir = 1.0 / (local_dir + vec3(1e-6));

  float margin = 1.01; // 1% margin
  vec3 t0 = (vec3(-margin) - local_orig) * inv_dir;
  vec3 t1 = (vec3(margin) - local_orig) * inv_dir;
  vec3 tmin = min(t0, t1);
  vec3 tmax = max(t0, t1);

  float t_near = max(max(tmin.x, tmin.y), tmin.z);
  float t_far = min(min(tmax.x, tmax.y), tmax.z);

  return t_near <= t_far && t_far > 0.0;
}

vec2 intersect_unit_sphere(ray r) {
  vec3 ro = r.origin;
  vec3 rd = r.dir;

  float a = dot(rd, rd);
  float b = 2.0 * dot(ro, rd);
  float c = dot(ro, ro) - 1.0;
  float delta = b * b - 4.0 * a * c;

  if (delta < 0.0) {
    return NO_HIT_SPAN;
  }
  float sqrt_delta = sqrt(delta);
  return vec2(-b - sqrt_delta, -b + sqrt_delta) / (2.0 * a);
}

vec2 intersect_box_AABB(ray r) {
  vec2 span = NO_HIT_SPAN;

  vec3 box_min = vec3(-.5);
  vec3 box_max = vec3(.5);

  vec3 ro = r.origin;
  vec3 rd = r.dir;

  vec3 t0 = (box_min - ro) / rd;
  vec3 t1 = (box_max - ro) / rd;

  vec3 tmin = min(t0, t1);
  vec3 tmax = max(t0, t1);

  float t_enter = max(max(tmin.x, tmin.y), tmin.z);
  float t_exit = min(min(tmax.x, tmax.y), tmax.z);

  if (t_exit < t_enter) {
    return span;
  }

  return vec2(t_enter, t_exit);
}

vec2 intersect_cylinder(ray r, float r_bottom, float r_top) {
  float slope = r_top - r_bottom;
  float R0 = r_bottom + slope * (r.origin.y + 0.5);

  float a = r.dir.x * r.dir.x + r.dir.z * r.dir.z - slope * slope * r.dir.y * r.dir.y;
  float bh = r.origin.x * r.dir.x + r.origin.z * r.dir.z - R0 * slope * r.dir.y;
  float c = r.origin.x * r.origin.x + r.origin.z * r.origin.z - R0 * R0;

  float t_enter = 1.0 / 0.0;
  float t_exit = -(1.0 / 0.0);

  if (abs(a) > 1e-7) {
    float disc = bh * bh - a * c;
    if (disc >= 0.0) {
      float sq = sqrt(disc);
      float t0 = (-bh - sq) / a;
      float t1 = (-bh + sq) / a;
      float y0 = r.origin.y + t0 * r.dir.y;
      float y1 = r.origin.y + t1 * r.dir.y;
      if (y0 >= -0.5 && y0 <= 0.5) {
        t_enter = min(t_enter, t0);
        t_exit = max(t_exit, t0);
      }
      if (y1 >= -0.5 && y1 <= 0.5) {
        t_enter = min(t_enter, t1);
        t_exit = max(t_exit, t1);
      }
    }
  }

  if (abs(r.dir.y) > 1e-7) {
    float t_b = (-0.5 - r.origin.y) / r.dir.y;
    vec2 pb = r.origin.xz + t_b * r.dir.xz;
    if (dot(pb, pb) <= r_bottom * r_bottom + 1e-6) {
      t_enter = min(t_enter, t_b);
      t_exit = max(t_exit, t_b);
    }
    float t_t = (0.5 - r.origin.y) / r.dir.y;
    vec2 pt = r.origin.xz + t_t * r.dir.xz;
    if (dot(pt, pt) <= r_top * r_top + 1e-6) {
      t_enter = min(t_enter, t_t);
      t_exit = max(t_exit, t_t);
    }
  }

  if (t_enter >= t_exit) return NO_HIT_SPAN;
  return vec2(t_enter, t_exit);
}

vec3 get_local_normal(uint type, vec3 p, float r1, float r2) {
  if (type == PRIMITIVE_TYPE_SPHERE) return normalize(p);
  if (type == PRIMITIVE_TYPE_CUBE) {
    vec3 abs_dist = abs(p);
    float max_axis = max(max(abs_dist.x, abs_dist.y), abs_dist.z);
    return normalize(step(vec3(max_axis - 0.0001), abs_dist) * sign(p));
  }
  if (type == PRIMITIVE_TYPE_CYLINDER) {
    if (abs(p.y) > 0.499) return vec3(0.0, sign(p.y), 0.0);
    float slope = r2 - r1;
    float R = r1 + slope * (p.y + 0.5);
    return normalize(vec3(p.x, -slope * R, p.z));
  }
  return vec3(0, 1, 0);
}

vec4 get_final_color(vec3 world_pos, Primitive prim, bool invert_normal) {
  mat4 inv_mat = prim.inv_transform;
  vec3 local_pos = (inv_mat * vec4(world_pos, 1.0)).xyz;
  vec3 local_normal = get_local_normal(prim.type, local_pos, prim.r1, prim.r2);
  mat3 normal_matrix = transpose(mat3(prim.inv_transform));
  vec3 world_normal = normalize(normal_matrix * local_normal);
  if (invert_normal) world_normal = -world_normal; // for inside faces

  vec3 view_dir = normalize(u_camera_pos - world_pos);

  mat3 eye_to_world = mat3(u_inv_view);
  vec3 light0 = normalize(eye_to_world * normalize(vec3(-1.0, +1.0, +1.0)));
  vec3 light1 = normalize(eye_to_world * normalize(vec3(+1.0, -1.0, -1.0)));

  // OpenSCAD lighting model: global ambient 0.2 + two white diffuse lights
  float ambient = 0.2;
  float diffuse = max(0.0, dot(world_normal, light0))
      + max(0.0, dot(world_normal, light1));

  // Fresnel edge darkening: silhouette edges fade darker, giving clear object borders
  float fresnel = 1.0 - abs(dot(world_normal, view_dir));
  float lighting = (ambient + diffuse) * (1.0 - pow(fresnel, 3.0) * 0.5);

  // Specular: Blinn-Phong highlight on primary light
  vec3 half_dir = normalize(light0 + view_dir);
  float specular = pow(max(0.0, dot(world_normal, half_dir)), 48.0) * 0.25;

  return vec4(clamp(prim.color.rgb * lighting + vec3(specular), 0.0, 1.0), prim.color.a);
}

bool is_inside(uint op, bool in_a, bool in_b) {
  if (op == OP_TYPE_OPUNION) return in_a || in_b;
  if (op == OP_TYPE_OPINTERSECTION) return in_a && in_b;
  if (op == OP_TYPE_OPDIFFERENCE) return in_a && !in_b;
  return false;
}

interval_list merge_spans(interval_list l_a, interval_list l_b, uint op) {
  switch (op) {
    case OP_TYPE_OPUNION:
    if (l_a.count == 0) return l_b;
    if (l_b.count == 0) return l_a;
    break;
    case OP_TYPE_OPINTERSECTION:
    if (l_a.count == 0 || l_b.count == 0) {
      interval_list empty;
      empty.count = 0;
      return empty;
    }
    break;
    case OP_TYPE_OPDIFFERENCE:
    if (l_a.count == 0) {
      interval_list empty;
      empty.count = 0;
      return empty;
    }
    if (l_b.count == 0) return l_a;
    break;
  }

  interval_list result;
  result.count = 0;
  int i = 0;
  int j = 0;
  bool in_a = false;
  bool in_b = false;
  bool last_in_result = false;
  float t_start = 0.0;
  uint start_prim_id = 0;
  bool start_inverted = false;

  while ((i < l_a.count || j < l_b.count) && result.count < MAX_SPANS) {
    float t_a = (i < l_a.count) ? (in_a ? l_a.spans[i].interval.y : l_a.spans[i].interval.x) : 1.0 / 0.0;
    float t_b = (j < l_b.count) ? (in_b ? l_b.spans[j].interval.y : l_b.spans[j].interval.x) : 1.0 / 0.0;

    float current_t;
    uint current_prim;
    bool current_invert;

    if (t_a < t_b) {
      current_t = t_a;
      current_prim = span_prim_id(l_a.spans[i]);
      current_invert = span_invert(l_a.spans[i]);
      in_a = !in_a;
      if (!in_a) i++;
    } else {
      current_t = t_b;
      current_prim = span_prim_id(l_b.spans[j]);
      bool is_diff = (op == OP_TYPE_OPDIFFERENCE);
      current_invert = is_diff ? !span_invert(l_b.spans[j]) : span_invert(l_b.spans[j]);
      in_b = !in_b;
      if (!in_b) j++;
    }

    bool in_result = is_inside(op, in_a, in_b);
    if (in_result != last_in_result) {
      if (in_result) {
        t_start = current_t;
        start_prim_id = current_prim;
        start_inverted = current_invert;
      } else {
        if (current_t > t_start + max(t_start * 1e-5, 1e-6)) {
          int idx = result.count;
          result.spans[idx].interval = vec2(t_start, current_t);
          result.spans[idx].packed_prim = pack_span_prim(start_prim_id, start_inverted);
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
  if (span.x >= span.y) {
    list.count = 0;
  }
  else {
    list.count = 1;
    list.spans[0].interval = span;
    list.spans[0].packed_prim = pack_span_prim(id, false);
  }
  return list;
}

vec4 csg_span(ray r, ivec2 pixel_coords) {
  interval_list res_stack[MAX_STACK]; // holds computed interval list
  uint op_type_stack[MAX_STACK]; // pending operations stack
  int op_received[MAX_STACK]; // for each operation collected in the stack store how many children it has
  uint op_dup_id[MAX_STACK]; // ids of duplicate ops

  int res_sp = 0; // spans stack pointer
  int op_sp = 0; // operations stack pointer

  // clear cache before use
  for (int c = 0; c < u_cache_size; c++) SCACHE(c).duplicate_id = 0u;
  cache_current_id = 0;

  uint num_ops = uint(commands.length());

  // traversal of the csg tree, root -> leaves
  for (uint i = 0; i < num_ops; ) {
    CSGCommand cmd = commands[i];

    bool has_result = false;
    interval_list result;
    result.count = 0;
    uint advance = 1u;

    // CACHE HIT -> return immediately the stored span
    if (u_use_cache == 1 && cmd.duplicate_id != 0u) {
      int cached = find_in_cache(cmd.duplicate_id);
      if (cached != -1) {
        result = SCACHE(cached).spans;
        has_result = true;
        advance = cmd.skip_children;
      }
    }

    if (!has_result) {
      if (cmd.type == ID_OP_TYPE_CACHED_REF) {
        has_result = true;
      } else {

        // obb intersection check
        bool obb_skip = u_use_obb == 1 &&
            (obbs[i].skip == 1u || !intersect_obb(r.origin, r.dir, obbs[i].inv_transform));

        if (obb_skip) { // empty result, skip this + children
          has_result = true;
          advance = cmd.skip_children;
        } else if (cmd.type == ID_OP_TYPE_PRIMITIVE) { // Calculate span hit ray-primitive
          Primitive p = primitives[cmd.id];
          ray local_ray;
          local_ray.origin = (p.inv_transform * vec4(r.origin, 1.0)).xyz;
          local_ray.dir = (p.inv_transform * vec4(r.dir, 0.0)).xyz;

          vec2 hit = NO_HIT_SPAN;
          if (p.type == PRIMITIVE_TYPE_SPHERE) hit = intersect_unit_sphere(local_ray);
          else if (p.type == PRIMITIVE_TYPE_CUBE) hit = intersect_box_AABB(local_ray);
          else if (p.type == PRIMITIVE_TYPE_CYLINDER) hit = intersect_cylinder(local_ray, p.r1, p.r2);

          // actual hit something
          if (hit.x < hit.y) {
            result.count = 1;
            result.spans[0].interval = hit;
            result.spans[0].packed_prim = pack_span_prim(cmd.id, false);
          }

          if (u_use_cache == 1 && cmd.duplicate_id != 0u) save_to_cache(cmd.duplicate_id, result); // if have duplicates cache it
          has_result = true;
        } else { // OPERATION NODES
          // init data for this node in the ops stacks
          op_type_stack[op_sp] = operations[cmd.id].type;
          op_received[op_sp] = 0;
          op_dup_id[op_sp] = cmd.duplicate_id;
          op_sp++;
          i++;
          continue;
        }
      }
    }

    // put this node hit result (empty or not empty) in the stack
    res_stack[res_sp++] = result;
    while (op_sp > 0) {
      op_received[op_sp - 1]++; // update parent node (op_sp - 1) children count
      if (op_received[op_sp - 1] < 2) break; // wait for both left and right nodes to be visited

      // pop the children spans and merge them, then push the result
      interval_list b = res_stack[--res_sp];
      interval_list a = res_stack[--res_sp];
      op_sp--; // go back to parent
      interval_list merged = merge_spans(a, b, op_type_stack[op_sp]);
      if (u_use_cache == 1 && op_dup_id[op_sp] != 0u) save_to_cache(op_dup_id[op_sp], merged); // if have duplicates cache it
      res_stack[res_sp++] = merged;
    }
    i += advance; // skip logic
  }

  if (res_sp > 0 && res_stack[0].count > 0) {
    interval_list final_list = res_stack[0];
    float t = 1e10;
    int best_idx = -1; // the best (with closest t_enter) span index on the final list
    for (int k = 0; k < final_list.count; k++) {
      float t_enter = final_list.spans[k].interval.x;
      if (t_enter > 0.01 && t_enter < t) {
        t = t_enter;
        best_idx = k;
      }
    }
    if (best_idx != -1) {
      span hit = final_list.spans[best_idx];
      vec3 hitPos = r.origin + r.dir * t; // calculate hit point

      vec4 clipPos = u_proj * u_view * vec4(hitPos, 1.0); // hit point in clip space
      float depth = (clipPos.z / clipPos.w) * 0.5 + 0.5;
      imageStore(depthOutput, pixel_coords, vec4(depth, 0.0, 0.0, 0.0));

      return get_final_color(hitPos, primitives[span_prim_id(hit)], span_invert(hit));
    }
  }

  imageStore(depthOutput, pixel_coords, vec4(1.0, 0.0, 0.0, 0.0));
  return vec4(u_background, 1.0);
}

float hash(vec2 p) {
  return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x))));
}

void main() {
  ivec2 pixel_coords = ivec2(gl_GlobalInvocationID.xy);
  ivec2 dims = imageSize(imgOutput);

  if (pixel_coords.x >= dims.x || pixel_coords.y >= dims.y) return;

  vec4 average_color = vec4(0.0);
  int samples = u_samples;
  for (int s = 0; s < samples; s++) {
    vec2 jitter = vec2(hash(vec2(pixel_coords) + float(s)), hash(vec2(pixel_coords) + float(s) * 2.0)) - 0.5;
    vec2 uv = (vec2(pixel_coords) + jitter) / vec2(dims);
    uv = uv * 2.0 - 1.0;

    float tanHalfFov = tan(radians(fov) * 0.5);
    vec3 rayDirLocal = normalize(vec3(uv.x * aspectRatio * tanHalfFov, uv.y * tanHalfFov, -1.0));
    vec3 rayDirWorld = normalize(mat3(u_inv_view) * rayDirLocal);

    ray r;
    r.origin = u_camera_pos;
    r.dir = rayDirWorld;

    vec4 sample_color = csg_span(r, pixel_coords);

    // Render obb outlines for debug
    if (u_rendering_mode == 1) {
      float wire = 0.0;
      for (uint i = 0; i < commands.length(); i++) {
        vec3 local_ro = (obbs[i].inv_transform * vec4(r.origin, 1.0)).xyz;
        vec3 local_rd = (obbs[i].inv_transform * vec4(r.dir, 0.0)).xyz;
        wire += wireframe_box(local_ro, local_rd, vec3(-1.0), vec3(1.0));
      }
      if (wire > 0.0) {
        sample_color = mix(sample_color, vec4(0.0, 1.0, 0.2, 1.0), 0.6);
      }
    }

    average_color += sample_color;
  }
  imageStore(imgOutput, pixel_coords, average_color / float(samples));
}
