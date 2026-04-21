#version 450 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba32f, binding = 0) uniform image2D imgOutput;

// depth texture: used for drawing things on top/behind later
layout(rgba32f, binding = 1) uniform image2D depthOutput;

uniform mat4 u_view;
uniform mat4 u_proj;

#ifndef MAX_STACK
#define MAX_STACK 8
#endif
#define MAX_SPANS        3   // opaque surface + 1 transparent layer
#define MAX_SHADOW_SPANS 2   // shadow: rarely needs more than 2 intervals
#define CACHE_SIZE       16  // per-thread cache slots (in workgroup shared memory)
#define WG_SIZE          64  // 8×8; must match local_size_x * local_size_y
#define SHADOW_T_MIN     0.01 // minimum t for shadow hits; filters self-intersection

// CONSTANTS
const uint PRIMITIVE_TYPE_SPHERE = 1u;
const uint PRIMITIVE_TYPE_CUBE = 2u;
const uint PRIMITIVE_TYPE_CYLINDER = 4u;

const uint OP_TYPE_OPUNION = 1u;
const uint OP_TYPE_OPINTERSECTION = 2u;
const uint OP_TYPE_OPDIFFERENCE = 4u;

const uint ID_OP_TYPE_PRIMITIVE = 0u;
const uint ID_OP_TYPE_OPERATION = 1u;
const uint ID_OP_TYPE_CACHED_OPERATION = 2u;
const uint ID_OP_TYPE_CACHED_PRIMITIVE = 3u;

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
uniform int u_use_bounds;
uniform int u_use_cache;
uniform int u_use_shadows;

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

struct BoundsData {
  mat4 inv_transform;  // OBB inv-transform, OR col(0).xyz=min/col(1).xyz=max for AABB
  uint skip;
  uint bounds_type;    // 0 = AABB (slab test), 1 = OBB (matrix test)
  uint _pad[2];
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
layout(std430, binding = 4) readonly buffer BoundsBuffer {
  BoundsData bounds[];
};

layout(std430, binding = 5) buffer StatsBuffer {
  uint bounds_skipped;
  uint cache_hits;
  uint cache_misses;
  uint nodes_visited;
  uint leaves_visited;
};

struct span {
  float t_enter;
  float t_exit;
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

// Shadow path: no prim ID needed — only interval extents matter.
// fp16 packing: t_enter=lo, t_exit=hi — halves shadow_span from 8 to 4 bytes.
// Precision is sufficient: shadow only needs correct ordering and t > SHADOW_T_MIN test.
struct shadow_span {
  uint t_packed;
};
struct shadow_il {
  shadow_span spans[MAX_SHADOW_SPANS];
  int count;
};

shadow_span pack_shadow_span(float t_enter, float t_exit) {
  shadow_span s;
  s.t_packed = packHalf2x16(vec2(t_enter, t_exit));
  return s;
}
float shadow_span_enter(shadow_span s) {
  return unpackHalf2x16(s.t_packed).x;
}
float shadow_span_exit(shadow_span s) {
  return unpackHalf2x16(s.t_packed).y;
}

// Op stack packing — fits op_type + op_received + dup_id into one uint per slot:
//   bits 31-4: dup_id
//   bits  3-2: op_type  (0=UNION, 1=INTERSECTION, 2=DIFFERENCE)
//   bits  1-0: received (0, 1, or 2 — incremented with op_stack[i]++)
uint encode_op_type(uint t) {
  return t == OP_TYPE_OPUNION ? 0u : t == OP_TYPE_OPINTERSECTION ? 1u : 2u;
}
uint decode_op_type(uint e) {
  return e == 0u ? OP_TYPE_OPUNION : e == 1u ? OP_TYPE_OPINTERSECTION : OP_TYPE_OPDIFFERENCE;
}
uint make_op_entry(uint dup_id, uint op_type) {
  return ((dup_id & 0x0FFFFFFFu) << 4) | (encode_op_type(op_type) << 2);
}
uint get_op_dup_id(uint e) {
  return e >> 4;
}
uint get_op_type(uint e) {
  return decode_op_type((e >> 2) & 3u);
}
uint get_op_received(uint e) {
  return e & 3u;
}

// --- CACHE (workgroup shared memory, one column per thread) ---
//
// Layout: [CACHE_SIZE][WG_SIZE]  (thread index is the fast dimension)
// When all 64 threads access slot i, thread t is at offset:
//   i * WG_SIZE * sizeof(cache_entry) + t * sizeof(cache_entry)
// sizeof(cache_entry) = 4 (key) + MAX_SPANS*12 (span[2]) = 28 bytes = 7 words
// gcd(7, 32 banks) = 1  →  zero bank conflicts.
//
// Direct-mapped: slot = (dup_id - 1) % CACHE_SIZE  →  O(1) lookup/store.
//
// encoding: bits 31-30 = count (0..MAX_SPANS), bits 29-0 = duplicate_id
struct cache_entry {
  uint key;
  span spans[MAX_SPANS];
};
shared cache_entry wg_span_cache[CACHE_SIZE][WG_SIZE];

// utility packed ds getters
uint cache_make_key(uint dup_id, int count) {
  return (uint(count) << 30) | (dup_id & 0x3FFFFFFFu);
}
uint cache_dup_id(uint key) {
  return key & 0x3FFFFFFFu;
}
int cache_count(uint key) {
  return int(key >> 30);
}

// O(1): direct slot from dup_id, single read + comparison.
int find_in_cache(uint dup_id) {
  uint tid = gl_LocalInvocationIndex;
  int slot = int((dup_id - 1u) % uint(CACHE_SIZE));
  if (cache_dup_id(wg_span_cache[slot][tid].key) == dup_id) return slot;
  return -1;
}

void save_to_cache(uint dup_id, interval_list il) {
  uint tid = gl_LocalInvocationIndex;
  int slot = int((dup_id - 1u) % uint(CACHE_SIZE));
  wg_span_cache[slot][tid].key = cache_make_key(dup_id, il.count);
  for (int s = 0; s < il.count; s++)
    wg_span_cache[slot][tid].spans[s] = il.spans[s];
}

interval_list load_from_cache(int slot) {
  uint tid = gl_LocalInvocationIndex;
  interval_list il;
  il.count = cache_count(wg_span_cache[slot][tid].key);
  for (int s = 0; s < il.count; s++)
    il.spans[s] = wg_span_cache[slot][tid].spans[s];
  return il;
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

bool intersect_oriented(vec3 ray_orig, vec3 ray_dir, mat4 inv_transform) {
  vec3 local_orig = (inv_transform * vec4(ray_orig, 1.0)).xyz;
  vec3 local_dir  = (inv_transform * vec4(ray_dir,  0.0)).xyz;
  vec3 inv_dir    = 1.0 / (local_dir + vec3(1e-6));
  float m = 1.01;
  vec3 t0 = (vec3(-m) - local_orig) * inv_dir;
  vec3 t1 = (vec3( m) - local_orig) * inv_dir;
  vec3 tmin = min(t0, t1);
  vec3 tmax = max(t0, t1);
  float t_near = max(max(tmin.x, tmin.y), tmin.z);
  float t_far  = min(min(tmax.x, tmax.y), tmax.z);
  return t_near <= t_far && t_far > 0.0;
}

bool intersect_aabb(vec3 ro, vec3 rd, vec3 mn, vec3 mx) {
  vec3 inv_d = 1.0 / (rd + vec3(1e-30));
  vec3 t0 = (mn - ro) * inv_d;
  vec3 t1 = (mx - ro) * inv_d;
  float t_near = max(max(min(t0.x,t1.x), min(t0.y,t1.y)), min(t0.z,t1.z));
  float t_far  = min(min(max(t0.x,t1.x), max(t0.y,t1.y)), max(t0.z,t1.z));
  return t_near <= t_far && t_far > 0.0;
}

bool bounds_hit(vec3 ro, vec3 rd, BoundsData b) {
  if (b.bounds_type == 0u)
    return intersect_aabb(ro, rd, b.inv_transform[0].xyz, b.inv_transform[1].xyz);
  return intersect_oriented(ro, rd, b.inv_transform);
}

vec2 intersect_unit_sphere(ray r) {
  vec3 ro = r.origin;
  vec3 rd = r.dir;

  float a = dot(rd, rd);
  float b = 2.0 * dot(ro, rd);
  float c = dot(ro, ro) - 1.0;
  float delta = b * b - 4.0 * a * c;

  if (delta < 0.0) return NO_HIT_SPAN;
  float sqrt_delta = sqrt(delta);
  return vec2(-b - sqrt_delta, -b + sqrt_delta) / (2.0 * a);
}

vec2 intersect_box_AABB(ray r) {
  vec3 box_min = vec3(-0.5);
  vec3 box_max = vec3(0.5);
  vec3 ro = r.origin;
  vec3 rd = r.dir;

  vec3 t0 = (box_min - ro) / rd;
  vec3 t1 = (box_max - ro) / rd;

  vec3 tmin = min(t0, t1);
  vec3 tmax = max(t0, t1);

  float t_enter = max(max(tmin.x, tmin.y), tmin.z);
  float t_exit = min(min(tmax.x, tmax.y), tmax.z);

  if (t_exit < t_enter) return NO_HIT_SPAN;
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

vec4 get_final_color(vec3 world_pos, Primitive prim, bool invert_normal, float shadow_factor) {
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
  float diffuse = max(0.0, dot(world_normal, light0)) * shadow_factor
      + max(0.0, dot(world_normal, light1));
  float lighting = ambient + diffuse;

  // Specular: Blinn-Phong highlight on primary light
  vec3 half_dir = normalize(light0 + view_dir);
  float specular = pow(max(0.0, dot(world_normal, half_dir)), 48.0) * 0.25 * shadow_factor;

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
  int i = 0, j = 0;
  bool in_a = false, in_b = false, last_in_result = false;
  float t_start = 0.0;
  uint start_prim_id = 0u;
  bool start_inverted = false;

  while ((i < l_a.count || j < l_b.count) && result.count < MAX_SPANS) {
    float t_a = (i < l_a.count) ? (in_a ? l_a.spans[i].t_exit : l_a.spans[i].t_enter) : 1.0 / 0.0;
    float t_b = (j < l_b.count) ? (in_b ? l_b.spans[j].t_exit : l_b.spans[j].t_enter) : 1.0 / 0.0;

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
      } else if (current_t > t_start + max(t_start * 1e-5, 1e-6)) {
        int idx = result.count;
        result.spans[idx].t_enter = t_start;
        result.spans[idx].t_exit = current_t;
        result.spans[idx].packed_prim = pack_span_prim(start_prim_id, start_inverted);
        result.count++;
      }
      last_in_result = in_result;
    }
  }
  return result;
}

// Shadow merge: same traversal algorithm with early exit, no prim-ID tracking
shadow_il merge_shadow_spans(shadow_il l_a, shadow_il l_b, uint op) {
  switch (op) {
    case OP_TYPE_OPUNION:
    if (l_a.count == 0) return l_b;
    if (l_b.count == 0) return l_a;
    break;
    case OP_TYPE_OPINTERSECTION:
    if (l_a.count == 0 || l_b.count == 0) {
      shadow_il empty;
      empty.count = 0;
      return empty;
    }
    break;
    case OP_TYPE_OPDIFFERENCE:
    if (l_a.count == 0) {
      shadow_il empty;
      empty.count = 0;
      return empty;
    }
    if (l_b.count == 0) return l_a;
    break;
  }

  shadow_il result;
  result.count = 0;
  int i = 0, j = 0;
  bool in_a = false, in_b = false, last_in_result = false;
  float t_start = 0.0;

  while ((i < l_a.count || j < l_b.count) && result.count < MAX_SHADOW_SPANS) {
    float t_a = (i < l_a.count) ? (in_a ? shadow_span_exit(l_a.spans[i]) : shadow_span_enter(l_a.spans[i])) : 1.0 / 0.0;
    float t_b = (j < l_b.count) ? (in_b ? shadow_span_exit(l_b.spans[j]) : shadow_span_enter(l_b.spans[j])) : 1.0 / 0.0;

    float current_t;
    if (t_a < t_b) {
      current_t = t_a;
      in_a = !in_a;
      if (!in_a) i++;
    } else {
      current_t = t_b;
      in_b = !in_b;
      if (!in_b) j++;
    }

    bool in_result = is_inside(op, in_a, in_b);
    if (in_result != last_in_result) {
      if (in_result) {
        t_start = current_t;
      } else if (current_t > t_start + max(t_start * 1e-5, 1e-6)) {
        result.spans[result.count] = pack_shadow_span(t_start, current_t);
        result.count++;
      }
      last_in_result = in_result;
    }
  }
  return result;
}

interval_list make_primitive_interval(vec2 hit, uint id) {
  interval_list list;
  if (hit.x >= hit.y) {
    list.count = 0;
  } else {
    list.count = 1;
    list.spans[0].t_enter = hit.x;
    list.spans[0].t_exit = hit.y;
    list.spans[0].packed_prim = pack_span_prim(id, false);
  }
  return list;
}

shadow_il make_shadow_interval(vec2 hit) {
  shadow_il list;
  if (hit.x >= hit.y) {
    list.count = 0;
  } else {
    list.count = 1;
    list.spans[0] = pack_shadow_span(hit.x, hit.y);
  }
  return list;
}

bool csg_shadow_test(ray r) {
  shadow_il res_stack[MAX_STACK];
  int res_sp = 0;
  uint op_stack[MAX_STACK];
  int op_sp = 0;

  uint num_ops = uint(commands.length());
  for (uint i = 0; i < num_ops; ) {
    CSGCommand cmd = commands[i];

    bool has_result = false;
    shadow_il result;
    result.count = 0;
    uint advance = 1u;

    bool bounds_skip = u_use_bounds == 1 &&
        (bounds[i].skip == 1u || !bounds_hit(r.origin, r.dir, bounds[i]));

    if (bounds_skip) {
      has_result = true;
      advance = cmd.skip_children;
    } else if (cmd.type == ID_OP_TYPE_PRIMITIVE ||
        cmd.type == ID_OP_TYPE_CACHED_PRIMITIVE) {
      Primitive p = primitives[cmd.id];
      ray local_ray;
      local_ray.origin = (p.inv_transform * vec4(r.origin, 1.0)).xyz;
      local_ray.dir = (p.inv_transform * vec4(r.dir, 0.0)).xyz;

      vec2 hit = NO_HIT_SPAN;
      if (p.type == PRIMITIVE_TYPE_SPHERE) hit = intersect_unit_sphere(local_ray);
      else if (p.type == PRIMITIVE_TYPE_CUBE) hit = intersect_box_AABB(local_ray);
      else if (p.type == PRIMITIVE_TYPE_CYLINDER) hit = intersect_cylinder(local_ray, p.r1, p.r2);

      result = make_shadow_interval(hit);
      has_result = true;
    } else {
      op_stack[op_sp] = make_op_entry(0u, operations[cmd.id].type);
      op_sp++;
      i++;
      continue;
    }

    res_stack[res_sp++] = result;
    while (op_sp > 0) {
      op_stack[op_sp - 1]++;
      if (get_op_received(op_stack[op_sp - 1]) < 2u) break;
      shadow_il b = res_stack[--res_sp];
      shadow_il a = res_stack[--res_sp];
      op_sp--;
      shadow_il merged = merge_shadow_spans(a, b, get_op_type(op_stack[op_sp]));
      for (int k = 0; k < merged.count; k++) {
        if (shadow_span_enter(merged.spans[k]) > SHADOW_T_MIN) return true;
      }
      res_stack[res_sp++] = merged;
    }
    i += advance;
  }

  if (res_sp > 0) {
    for (int k = 0; k < res_stack[0].count; k++) {
      if (shadow_span_enter(res_stack[0].spans[k]) > SHADOW_T_MIN) return true;
    }
  }
  return false;
}

// CSG traversal — computes the interval list for ray r.
interval_list csg_traverse(ray r, inout uint s_bounds, inout uint s_hits, inout uint s_misses,
                            inout uint s_nodes, inout uint s_leaves) {
  interval_list res_stack[MAX_STACK];
  int res_sp = 0;

  uint op_stack[MAX_STACK]; // packed: bits 31-4=dup_id, 3-2=type, 1-0=received
  int op_sp = 0;

  uint tid = gl_LocalInvocationIndex;
  if (u_use_cache == 1) {
    for (int c = 0; c < CACHE_SIZE; c++) wg_span_cache[c][tid].key = 0u;
  }

  uint num_ops = uint(commands.length());
//simplified traversal of the CSG tree returning a boolean occlusion result .
// Traversal of the CSG tree, root → leaves
for ( uint i = 0; i < num_ops; ) {
CSGCommand cmd = commands[i];
s_nodes++;

bool has_result = false;
interval_list result;
result . count = 0 ;
uint advance = 1u;

// CACHE HIT → return stored span immediately
if ( u_use_cache == 1 && cmd . duplicate_id != 0u ) {
int cached = find_in_cache(cmd.duplicate_id);
if ( cached != - 1 ) {
result = load_from_cache(cached);
has_result = true ;
advance = cmd . skip_children;
s_hits ++ ;
} else {
s_misses ++ ;
}
}

if ( ! has_result ) {
if ( cmd . type == ID_OP_TYPE_CACHED_OPERATION ) {
// Cache miss: fall back to regular operation traversal
op_stack[op_sp] = make_op_entry(cmd.duplicate_id, operations[cmd.id].type);
op_sp ++ ;
i ++ ;
continue ;
} else if ( cmd . type == ID_OP_TYPE_CACHED_PRIMITIVE ) {
Primitive p = primitives[cmd.id];
ray local_ray;
local_ray . origin = ( p . inv_transform * vec4(r.origin, 1.0)) . xyz;
local_ray . dir = ( p . inv_transform * vec4(r.dir, 0.0)) . xyz;

vec2 hit = NO_HIT_SPAN;
if ( p . type == PRIMITIVE_TYPE_SPHERE ) hit = intersect_unit_sphere(local_ray);
else if ( p . type == PRIMITIVE_TYPE_CUBE ) hit = intersect_box_AABB(local_ray);
else if ( p . type == PRIMITIVE_TYPE_CYLINDER ) hit = intersect_cylinder(local_ray, p.r1, p.r2);

result = make_primitive_interval(hit, cmd.id);
if ( u_use_cache == 1 && cmd . duplicate_id != 0u ) save_to_cache(cmd.duplicate_id, result);
has_result = true ;
s_leaves++;
} else {
// OBB intersection check
bool bounds_skip = u_use_bounds == 1 &&
    (bounds[i].skip == 1u || !bounds_hit(r.origin, r.dir, bounds[i]));

if ( bounds_skip ) { // empty result, skip subtree
has_result = true ;
advance = cmd . skip_children;
s_bounds += cmd . skip_children;
} else if ( cmd . type == ID_OP_TYPE_PRIMITIVE ) { // ray–primitive intersection
Primitive p = primitives[cmd.id];
ray local_ray;
local_ray . origin = ( p . inv_transform * vec4(r.origin, 1.0)) . xyz;
local_ray . dir = ( p . inv_transform * vec4(r.dir, 0.0)) . xyz;

vec2 hit = NO_HIT_SPAN;
if ( p . type == PRIMITIVE_TYPE_SPHERE ) hit = intersect_unit_sphere(local_ray);
else if ( p . type == PRIMITIVE_TYPE_CUBE ) hit = intersect_box_AABB(local_ray);
else if ( p . type == PRIMITIVE_TYPE_CYLINDER ) hit = intersect_cylinder(local_ray, p.r1, p.r2);

result = make_primitive_interval(hit, cmd.id);
if ( u_use_cache == 1 && cmd . duplicate_id != 0u ) save_to_cache(cmd.duplicate_id, result);
has_result = true ;
s_leaves++;
} else { // OPERATION NODE
op_stack[op_sp] = make_op_entry(cmd.duplicate_id, operations[cmd.id].type);
op_sp ++ ;
i ++ ;
continue ;
}
}
}

// Push result and merge completed operations when both children are ready
res_stack[res_sp++] = result;
while ( op_sp > 0 ) {
op_stack[op_sp - 1] ++ ;
if ( get_op_received(op_stack[op_sp-1])< 2u ) break ;

interval_list b = res_stack[--res_sp];
interval_list a = res_stack[--res_sp];
op_sp -- ;
interval_list merged = merge_spans(a, b, get_op_type(op_stack[op_sp]));
uint dup = get_op_dup_id(op_stack[op_sp]);
if ( u_use_cache == 1 && dup != 0u ) save_to_cache(dup, merged);
res_stack[res_sp++] = merged;
}
i += advance;
}

interval_list empty;
empty . count = 0 ;
return ( res_sp > 0 ) ? res_stack[0] : empty;
}

// Shade a single span — both paths use the same primitives[] at binding 1.
vec4 shade_span(span hit, vec3 ray_origin, vec3 ray_dir, float shadow_factor) {
  vec3 hitPos = ray_origin + ray_dir * hit.t_enter;
  return get_final_color(hitPos, primitives[span_prim_id(hit)], span_invert(hit), shadow_factor);
}

// Composite: depth write + per-span shadow test + alpha blending → final pixel color.
vec4 composite(interval_list final_list, ray r, ivec2 pixel_coords) {
  vec3 accumulated = vec3(0.0);
  float remaining = 1.0; // remaining opacity budget
  bool depth_written = false;

  for (int k = 0; k < final_list.count; k++) {
    if (remaining < 0.01) break;

    float t_enter = final_list.spans[k].t_enter;
    if (t_enter <= 0.01) continue;

    span hit = final_list.spans[k];
    vec3 hitPos = r.origin + r.dir * t_enter;

    // Depth from first hit only
    if (!depth_written) {
      vec4 clipPos = u_proj * u_view * vec4(hitPos, 1.0);
      float depth = (clipPos.z / clipPos.w) * 0.5 + 0.5;
      imageStore(depthOutput, pixel_coords, vec4(depth, 0.0, 0.0, 0.0));
      depth_written = true;
    }

    float shadow_factor = 1.0;
    if (u_use_shadows == 1) {
      Primitive hit_prim = primitives[span_prim_id(hit)];
      vec3 local_pos = (hit_prim.inv_transform * vec4(hitPos, 1.0)).xyz;
      vec3 local_n = get_local_normal(hit_prim.type, local_pos, hit_prim.r1, hit_prim.r2);
      mat3 normal_mat = transpose(mat3(hit_prim.inv_transform));
      vec3 world_n = normalize(normal_mat * local_n);
      if (span_invert(hit)) world_n = -world_n;

      mat3 eye_to_world = mat3(u_inv_view);
      vec3 light0 = normalize(eye_to_world * normalize(vec3(-1.0, +1.0, +1.0)));

      ray shadow_ray;
      shadow_ray.origin = hitPos + world_n * 1e-3;
      shadow_ray.dir = light0;

      bool in_shadow = csg_shadow_test(shadow_ray);
      if (in_shadow) shadow_factor = 0.0;
    }

    vec4 span_color = shade_span(hit, r.origin, r.dir, shadow_factor);
    accumulated += span_color.rgb * span_color.a * remaining;
    remaining *= (1.0 - span_color.a);
  }

  if (depth_written) {
    // Blend remaining transparency with background
    return vec4(accumulated + u_background * remaining, 1.0);
  }

  imageStore(depthOutput, pixel_coords, vec4(1.0, 0.0, 0.0, 0.0));
  return vec4(u_background, 1.0);
}

shared uint wg_bounds_skipped;
shared uint wg_cache_hits;
shared uint wg_cache_misses;
shared uint wg_nodes_visited;
shared uint wg_leaves_visited;

float hash(vec2 p) {
  return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x))));
}

void main() {
  // Zero workgroup-local stat accumulators
  if (gl_LocalInvocationIndex == 0u) {
    wg_bounds_skipped = 0u;
    wg_cache_hits = 0u;
    wg_cache_misses = 0u;
    wg_nodes_visited = 0u;
    wg_leaves_visited = 0u;
  }
  barrier();

  ivec2 pixel_coords = ivec2(gl_GlobalInvocationID.xy);
  ivec2 dims = imageSize(imgOutput);

  uint inv_bounds = 0u;
  uint inv_hits = 0u;
  uint inv_misses = 0u;
  uint inv_nodes = 0u;
  uint inv_leaves = 0u;

  if (pixel_coords.x < dims.x && pixel_coords.y < dims.y) {
    vec4 average_color = vec4(0.0);
    int samples = u_samples;
    for (int s = 0; s < samples; s++) {
      vec2 jitter = vec2(
          hash(vec2(pixel_coords) + float(s)),
          hash(vec2(pixel_coords) + float(s) * 2.0)
        ) - 0.5;
      vec2 uv = (vec2(pixel_coords) + jitter) / vec2(dims);
      uv = uv * 2.0 - 1.0;

      float tanHalfFov = tan(radians(fov) * 0.5);
      vec3 rayDirLocal = normalize(vec3(uv.x * aspectRatio * tanHalfFov, uv.y * tanHalfFov, -1.0));
      vec3 rayDirWorld = normalize(mat3(u_inv_view) * rayDirLocal);

      ray r;
      r.origin = u_camera_pos;
      r.dir = rayDirWorld;

      interval_list spans = csg_traverse(r, inv_bounds, inv_hits, inv_misses, inv_nodes, inv_leaves);
      vec4 sample_color = composite(spans, r, pixel_coords);

      // Render OBB outlines for debug
      if (u_rendering_mode == 1) {
        float wire = 0.0;
        for (uint i = 0; i < commands.length(); i++) {
          vec3 local_ro = (bounds[i].inv_transform * vec4(r.origin, 1.0)).xyz;
          vec3 local_rd = (bounds[i].inv_transform * vec4(r.dir, 0.0)).xyz;
          wire += wireframe_box(local_ro, local_rd, vec3(-1.0), vec3(1.0));
        }
        if (wire > 0.0) sample_color = mix(sample_color, vec4(1.0, 0.0, 0.2, 1.0), 0.6);
      }
      average_color += sample_color;
    }
    imageStore(imgOutput, pixel_coords, average_color / float(samples));
  }

  atomicAdd(wg_bounds_skipped, inv_bounds);
  atomicAdd(wg_cache_hits, inv_hits);
  atomicAdd(wg_cache_misses, inv_misses);
  atomicAdd(wg_nodes_visited, inv_nodes);
  atomicAdd(wg_leaves_visited, inv_leaves);
  barrier();
  if (gl_LocalInvocationIndex == 0u) {
    atomicAdd(bounds_skipped, wg_bounds_skipped);
    atomicAdd(cache_hits, wg_cache_hits);
    atomicAdd(cache_misses, wg_cache_misses);
    atomicAdd(nodes_visited, wg_nodes_visited);
    atomicAdd(leaves_visited, wg_leaves_visited);
  }
}
