#version 450 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba32f, binding = 0) uniform image2D imgOutput;
layout(r32f, binding = 1) uniform image2D depthOutput;

uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat4 u_inv_view;
uniform vec3 u_camera_pos;
uniform float fov;
uniform float aspectRatio;
uniform vec3 u_light_dir;
uniform vec3 u_background;
uniform vec3 u_default_mat_color;
uniform vec3 u_default_back_color;
uniform int u_use_bounds;
uniform int u_use_tbest;
uniform int u_use_shadows;
uniform int u_use_dup_cache;
uniform int u_samples;
uniform int u_rendering_mode;

#define MAX_SPANS        3
#define MAX_SHADOW_SPANS 2
#define SHADOW_T_MIN     0.01
#define MAX_BVH_STACK    20
#define MAX_DUP_CACHE    4

// Primitive type constants — must match Primitive.h
const uint PRIMITIVE_TYPE_SPHERE = 1u;
const uint PRIMITIVE_TYPE_CUBE = 2u;
const uint PRIMITIVE_TYPE_CYLINDER = 4u;

// Operation type constants — must match Operation.h
const uint OP_TYPE_OPUNION = 1u;
const uint OP_TYPE_OPINTERSECTION = 2u;
const uint OP_TYPE_OPDIFFERENCE = 4u;

// ProductCommand.type values
const uint CMD_TYPE_PRIMITIVE = 0u;
const uint CMD_TYPE_OPERATION = 1u;

const vec2 NO_HIT_SPAN = vec2(1.0 / 0.0, -1.0 / 0.0);

// ---- GPU struct declarations (must match C++ std430 layout) ----

struct Primitive {
  int type;
  float r1;
  float r2;
  int defaultColor;
  vec4 color;
  mat4 inv_transform;
};

struct Operation {
  int type;
  uint left_id;
  uint right_id;
  int _pad;
};

struct ProductBVHNode {
  uint is_leaf; // 0 = internal union node, 1 = leaf product
  uint skip_children; // preorder skip count (includes self)
  uint bounds_skip; // 1 = skip entirely
  uint bounds_type; // 0 = AABB (col(0).xyz=min, col(1).xyz=max), 1 = OBB
  uint cmd_start; // leaf only
  uint cmd_count; // leaf only
  uint _pad0;
  uint _pad1;
  mat4 bounds_inv;
};

struct ProductCommand {
  uint type; // 0 = PRIMITIVE, 1 = OPERATION
  uint id; // index into primitives[] or operations[]
  uint skip_children; // preorder skip count for this subtree
  uint bounds_skip; // 1 = degenerate, skip test entirely
  uint bounds_type; // 0 = AABB (slab), 1 = OBB (matrix)
  uint duplicate_id; // 0 = unique, >0 = shared subtree (cache key)
  uint _pad1;
  uint _pad2;
  mat4 bounds_inv; // OBB inv-transform, OR col(0).xyz=min/col(1).xyz=max for AABB
};

// ---- SSBOs ----

layout(std430, binding = 1) readonly buffer PrimitivesBuffer {
  Primitive primitives[];
};
layout(std430, binding = 2) readonly buffer OperationsBuffer {
  Operation operations[];
};
layout(std430, binding = 3) readonly buffer ProductBVHBuf {
  ProductBVHNode product_bvh[];
};
layout(std430, binding = 4) readonly buffer ProductCommandsBuf {
  ProductCommand product_commands[];
};

layout(std430, binding = 5) buffer StatsBuffer {
  uint bounds_skipped;
  uint nodes_visited;
  uint leaves_visited;
};

// ---- Workgroup-local stat accumulators ----

shared uint wg_bounds_skipped;
shared uint wg_nodes_visited;
shared uint wg_leaves_visited;

// ---- Ray and span types ----

struct ray {
  vec3 origin;
  vec3 dir;
};

struct span {
  float t_enter;
  float t_exit;
  uint packed_prim; // bits 30-0: prim_id, bit 31: invert_normal
};

struct interval_list {
  span spans[MAX_SPANS];
  int count;
};

struct shadow_span {
  uint t_packed;
};
struct shadow_il {
  shadow_span spans[MAX_SHADOW_SPANS];
  int count;
};

uint span_prim_id(span s) {
  return s.packed_prim & 0x7FFFFFFFu;
}
bool span_invert(span s) {
  return (s.packed_prim & 0x80000000u) != 0u;
}
uint pack_span_prim(uint id, bool inv) {
  return id | (inv ? 0x80000000u : 0u);
}

shadow_span pack_shadow_span(float a, float b) {
  shadow_span s;
  s.t_packed = packHalf2x16(vec2(a, b));
  return s;
}
float shadow_span_enter(shadow_span s) {
  return unpackHalf2x16(s.t_packed).x;
}
float shadow_span_exit(shadow_span s) {
  return unpackHalf2x16(s.t_packed).y;
}

// ---- Op-stack helpers (same encoding as span shader) ----
// bits 31-4: dup_id  bits 3-2: op_type  bits 1-0: received

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

// ---- Duplicate subtree cache (workgroup shared memory) ----
// Layout: [CACHE_SIZE][WG_SIZE] — each thread owns an independent lane
// indexed by gl_LocalInvocationIndex, so cached span lists are ray-private.
#define WG_SIZE 64

shared uint          wg_span_cache_keys[MAX_DUP_CACHE][WG_SIZE];
shared interval_list wg_span_cache[MAX_DUP_CACHE][WG_SIZE];

void dup_cache_clear() {
  uint tid = gl_LocalInvocationIndex;
  for (int i = 0; i < MAX_DUP_CACHE; i++)
    wg_span_cache_keys[i][tid] = 0u;
}

bool dup_cache_lookup(uint dup_id, out interval_list result) {
  uint tid = gl_LocalInvocationIndex;
  uint slot = (dup_id - 1u) % uint(MAX_DUP_CACHE);
  if (wg_span_cache_keys[slot][tid] == dup_id) {
    result = wg_span_cache[slot][tid];
    return true;
  }
  return false;
}

void dup_cache_store(uint dup_id, interval_list result) {
  uint tid = gl_LocalInvocationIndex;
  uint slot = (dup_id - 1u) % uint(MAX_DUP_CACHE);
  wg_span_cache_keys[slot][tid] = dup_id;
  wg_span_cache[slot][tid] = result;
}

// ---- OBB intersection ----

bool intersect_oriented(vec3 ro, vec3 rd, mat4 inv_t) {
  vec3 lo = (inv_t * vec4(ro, 1.0)).xyz;
  vec3 ld = (inv_t * vec4(rd, 0.0)).xyz;
  vec3 inv_d = 1.0 / ld;
  float m = 1.01;
  vec3 t0 = (vec3(-m) - lo) * inv_d;
  vec3 t1 = (vec3(m) - lo) * inv_d;
  vec3 tmin = min(t0, t1);
  vec3 tmax = max(t0, t1);
  float t_near = max(max(tmin.x, tmin.y), tmin.z);
  float t_far = min(min(tmax.x, tmax.y), tmax.z);
  return t_near <= t_far && t_far > 0.0;
}

bool intersect_aabb(vec3 ro, vec3 rd, vec3 mn, vec3 mx) {
  vec3 inv_d = 1.0 / (rd + vec3(1e-30));
  vec3 t0 = (mn - ro) * inv_d;
  vec3 t1 = (mx - ro) * inv_d;
  float t_near = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
  float t_far = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
  return t_near <= t_far && t_far > 0.0;
}

// Returns true if hit; sets t_near to the entry distance (or leaves it undefined on miss).
bool bounds_hit_tnear(vec3 ro, vec3 rd, uint btype, mat4 data, out float t_near) {
  if (btype == 0u) { // AABB
    vec3 inv_d = 1.0 / (rd + vec3(1e-30));
    // the first two columns are min and max bounds
    vec3 t0 = (data[0].xyz - ro) * inv_d;
    vec3 t1 = (data[1].xyz - ro) * inv_d;

    // slab method
    t_near = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    float t_far = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    return t_near <= t_far && t_far > 0.0;
  } else {
    // OBB -> needs two vec * mat multiplications for tessting a unit box
    vec3 lo = (data * vec4(ro, 1.0)).xyz;
    vec3 ld = (data * vec4(rd, 0.0)).xyz;

    vec3 inv_d = 1.0 / ld;
    float m = 1.01; // margin
    vec3 t0 = (vec3(-m) - lo) * inv_d;
    vec3 t1 = (vec3(m) - lo) * inv_d;
    t_near = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    float t_far = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    return t_near <= t_far && t_far > 0.0;
  }
}

// ---- Primitive intersections ----

vec2 intersect_unit_sphere(ray r) {
  float a = dot(r.dir, r.dir);
  float b = 2.0 * dot(r.origin, r.dir);
  float c = dot(r.origin, r.origin) - 1.0;
  float d = b * b - 4.0 * a * c;
  if (d < 0.0) return NO_HIT_SPAN;
  float sq = sqrt(d);
  return vec2(-b - sq, -b + sq) / (2.0 * a);
}

vec2 intersect_box_AABB(ray r) {
  vec3 t0 = (vec3(-0.5) - r.origin) / r.dir;
  vec3 t1 = (vec3(0.5) - r.origin) / r.dir;
  float te = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
  float tx = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
  if (tx < te) return NO_HIT_SPAN;
  return vec2(te, tx);
}

vec2 cylinder_cap_check(ray r, float te, float tx,
    float ty_enter, float ty_exit, float r1, float r2) {
  if (te >= tx) return NO_HIT_SPAN;
  // Cap radius check: if entry/exit is on a cap, verify inside circle
  if (te == ty_enter) {
    vec2 p = r.origin.xz + te * r.dir.xz;
    float cap_r = (r.origin.y + te * r.dir.y < 0.0) ? r1 : r2;
    if (dot(p, p) > cap_r * cap_r) return NO_HIT_SPAN;
  }
  if (tx == ty_exit) {
    vec2 p = r.origin.xz + tx * r.dir.xz;
    float cap_r = (r.origin.y + tx * r.dir.y < 0.0) ? r1 : r2;
    if (dot(p, p) > cap_r * cap_r) return NO_HIT_SPAN;
  }
  return vec2(te, tx);
}

vec2 intersect_cylinder(ray r, float r1, float r2) {
  // Cylinder along Y axis, y in [-0.5, 0.5], radius r1 (bottom) to r2 (top)

  float slope = r2 - r1;
  float R0 = r1 + slope * (r.origin.y + 0.5);

  // --- Infinite tube intersection (2D circle/cone in XZ) ---
  float a = r.dir.x * r.dir.x + r.dir.z * r.dir.z - slope * slope * r.dir.y * r.dir.y;
  float bh = r.origin.x * r.dir.x + r.origin.z * r.dir.z - R0 * slope * r.dir.y;
  float c = r.origin.x * r.origin.x + r.origin.z * r.origin.z - R0 * R0;

  // --- Y-slab clamp: intersect y = -0.5 and y = 0.5 planes ---
  float ty_enter, ty_exit;
  if (abs(r.dir.y) > 1e-30) {
    float iy = 1.0 / r.dir.y;
    ty_enter = (-0.5 - r.origin.y) * iy;
    ty_exit = (0.5 - r.origin.y) * iy;
    if (ty_enter > ty_exit) {
      float tmp = ty_enter;
      ty_enter = ty_exit;
      ty_exit = tmp;
    }
  } else {
    // Ray parallel to caps — either fully inside or outside slab
    if (r.origin.y < -0.5 || r.origin.y > 0.5) return NO_HIT_SPAN;
    ty_enter = -(1.0 / 0.0);
    ty_exit = (1.0 / 0.0);
  }

  float disc = bh * bh - a * c;

  if (abs(a) <= 1e-30) {
    // Ray parallel to cone surface — inside only if origin is inside cone
    if (c >= 0.0) return NO_HIT_SPAN;
    return cylinder_cap_check(r, ty_enter, ty_exit, ty_enter, ty_exit, r1, r2);
  }

  if (a > 0.0) {
    // Standard case: ray inside cone between roots [t0, t1]
    if (disc < 0.0) return NO_HIT_SPAN;
    float sq = sqrt(max(disc, 0.0));
    float t0 = (-bh - sq) / a;
    float t1 = (-bh + sq) / a;

    float te = max(t0, ty_enter);
    float tx = min(t1, ty_exit);
    return cylinder_cap_check(r, te, tx, ty_enter, ty_exit, r1, r2);
  }

  // a < 0: ray steeper than cone slope.
  // Inside region is the COMPLEMENT: (-inf, lo) ∪ (hi, inf)
  if (disc < 0.0) {
    // No roots — ray is always inside cone, return slab
    return cylinder_cap_check(r, ty_enter, ty_exit, ty_enter, ty_exit, r1, r2);
  }

  float sq = sqrt(max(disc, 0.0));
  float ra = (-bh - sq) / a;
  float rb = (-bh + sq) / a;
  float lo = min(ra, rb);
  float hi = max(ra, rb);

  // First inside interval: (-inf, lo) ∩ slab
  float te1 = ty_enter;
  float tx1 = min(lo, ty_exit);
  vec2 hit1 = cylinder_cap_check(r, te1, tx1, ty_enter, ty_exit, r1, r2);
  if (hit1 != NO_HIT_SPAN) return hit1;

  // Second inside interval: (hi, inf) ∩ slab
  float te2 = max(hi, ty_enter);
  float tx2 = ty_exit;
  return cylinder_cap_check(r, te2, tx2, ty_enter, ty_exit, r1, r2);
}

// ---- Normals and shading ----

vec3 get_local_normal(uint type, vec3 p, float r1, float r2) {
  if (type == PRIMITIVE_TYPE_SPHERE) return normalize(p);
  if (type == PRIMITIVE_TYPE_CUBE) {
    vec3 a = abs(p);
    float m = max(max(a.x, a.y), a.z);
    return normalize(step(vec3(m - 0.0001), a) * sign(p));
  }
  if (type == PRIMITIVE_TYPE_CYLINDER) {
    if (abs(p.y) > 0.499) return vec3(0.0, sign(p.y), 0.0);
    float R = r1 + (r2 - r1) * (p.y + 0.5);
    return normalize(vec3(p.x, -(r2 - r1) * R, p.z));
  }
  return vec3(0, 1, 0);
}

vec4 get_final_color(vec3 world_pos, Primitive prim, bool invert_normal, float shadow_factor) {
  vec3 lp = (prim.inv_transform * vec4(world_pos, 1.0)).xyz;
  vec3 ln = get_local_normal(prim.type, lp, prim.r1, prim.r2);
  vec3 wn = normalize(transpose(mat3(prim.inv_transform)) * ln);
  if (invert_normal) wn = -wn;

  vec3 view = normalize(u_camera_pos - world_pos);
  mat3 e2w = mat3(u_inv_view);
  vec3 l0 = normalize(e2w * normalize(vec3(-1, +1, +1)));
  vec3 l1 = normalize(e2w * normalize(vec3(+1, -1, -1)));

  vec3 mat_color = (invert_normal && prim.defaultColor == 1) ? u_default_back_color : prim.color.rgb;

  float diffuse = max(0.0, dot(wn, l0)) * shadow_factor + max(0.0, dot(wn, l1));
  vec3 half_v = normalize(l0 + view);
  float specular = pow(max(0.0, dot(wn, half_v)), 48.0) * 0.25 * shadow_factor;
  return vec4(clamp(mat_color * (0.2 + diffuse) + vec3(specular), 0.0, 1.0), prim.color.a);
}

// ---- Interval list helpers ----

bool is_inside(uint op, bool in_a, bool in_b) {
  if (op == OP_TYPE_OPUNION) return in_a || in_b;
  if (op == OP_TYPE_OPINTERSECTION) return in_a && in_b;
  if (op == OP_TYPE_OPDIFFERENCE) return in_a && !in_b;
  return false;
}

interval_list make_primitive_interval(vec2 hit, uint id) {
  interval_list il;
  il.count = 0;
  if (hit.x < hit.y) {
    il.count = 1;
    il.spans[0].t_enter = hit.x;
    il.spans[0].t_exit = hit.y;
    il.spans[0].packed_prim = pack_span_prim(id, false);
  }
  return il;
}

shadow_il make_shadow_interval(vec2 hit) {
  shadow_il il;
  il.count = 0;
  if (hit.x < hit.y) {
    il.count = 1;
    il.spans[0] = pack_shadow_span(hit.x, hit.y);
  }
  return il;
}

interval_list merge_spans(interval_list la, interval_list lb, uint op) {
  if (op == OP_TYPE_OPUNION) {
    if (la.count == 0) return lb;
    if (lb.count == 0) return la;
  }
  if (op == OP_TYPE_OPINTERSECTION) {
    if (la.count == 0 || lb.count == 0) {
      interval_list e;
      e.count = 0;
      return e;
    }
  }
  if (op == OP_TYPE_OPDIFFERENCE) {
    if (la.count == 0) {
      interval_list e;
      e.count = 0;
      return e;
    }
    if (lb.count == 0) return la;
  }

  interval_list res;
  res.count = 0;
  int i = 0, j = 0;
  bool in_a = false, in_b = false, last_in = false;
  float t_start = 0.0;
  uint start_prim = 0u;
  bool start_inv = false;

  while ((i < la.count || j < lb.count) && res.count < MAX_SPANS) {
    float ta = (i < la.count) ? (in_a ? la.spans[i].t_exit : la.spans[i].t_enter) : 1.0 / 0.0;
    float tb = (j < lb.count) ? (in_b ? lb.spans[j].t_exit : lb.spans[j].t_enter) : 1.0 / 0.0;
    float ct;
    uint cp;
    bool ci2;
    if (ta < tb) {
      ct = ta;
      cp = span_prim_id(la.spans[i]);
      ci2 = span_invert(la.spans[i]);
      in_a = !in_a;
      if (!in_a) i++;
    } else {
      ct = tb;
      cp = span_prim_id(lb.spans[j]);
      bool diff = (op == OP_TYPE_OPDIFFERENCE);
      ci2 = diff ? !span_invert(lb.spans[j]) : span_invert(lb.spans[j]);
      in_b = !in_b;
      if (!in_b) j++;
    }
    bool in_res = is_inside(op, in_a, in_b);
    if (in_res != last_in) {
      if (in_res) {
        t_start = ct;
        start_prim = cp;
        start_inv = ci2;
      }
      else if (ct > t_start + max(t_start * 1e-5, 1e-6)) {
        res.spans[res.count].t_enter = t_start;
        res.spans[res.count].t_exit = ct;
        res.spans[res.count].packed_prim = pack_span_prim(start_prim, start_inv);
        res.count++;
      }
      last_in = in_res;
    }
  }
  return res;
}

shadow_il merge_shadow_spans(shadow_il la, shadow_il lb, uint op) {
  if (op == OP_TYPE_OPUNION) {
    if (la.count == 0) return lb;
    if (lb.count == 0) return la;
  }
  if (op == OP_TYPE_OPINTERSECTION) {
    if (la.count == 0 || lb.count == 0) {
      shadow_il e;
      e.count = 0;
      return e;
    }
  }
  if (op == OP_TYPE_OPDIFFERENCE) {
    if (la.count == 0) {
      shadow_il e;
      e.count = 0;
      return e;
    }
    if (lb.count == 0) return la;
  }

  shadow_il res;
  res.count = 0;
  int i = 0, j = 0;
  bool in_a = false, in_b = false, last_in = false;
  float t_start = 0.0;
  while ((i < la.count || j < lb.count) && res.count < MAX_SHADOW_SPANS) {
    float ta = (i < la.count) ? (in_a ? shadow_span_exit(la.spans[i]) : shadow_span_enter(la.spans[i])) : 1.0 / 0.0;
    float tb = (j < lb.count) ? (in_b ? shadow_span_exit(lb.spans[j]) : shadow_span_enter(lb.spans[j])) : 1.0 / 0.0;
    float ct;
    if (ta < tb) {
      ct = ta;
      in_a = !in_a;
      if (!in_a) i++;
    }
    else {
      ct = tb;
      in_b = !in_b;
      if (!in_b) j++;
    }
    bool in_res = is_inside(op, in_a, in_b);
    if (in_res != last_in) {
      if (in_res) {
        t_start = ct;
      }
      else if (ct > t_start + max(t_start * 1e-5, 1e-6)) {
        res.spans[res.count] = pack_shadow_span(t_start, ct);
        res.count++;
      }
      last_in = in_res;
    }
  }
  return res;
}

// ---- DNF product evaluator ----
//
// Traverses a single union-free product subtree in preorder.
// The product is a slice of the flatten product commands ssbo
interval_list eval_product(ray r, uint cmd_start, uint cmd_count,
  inout uint s_bounds, inout uint s_nodes, inout uint s_leaves,
  float best_t)
{
  interval_list res_stack[MAX_PRODUCT_STACK];
  int res_sp = 0;
  uint op_stack[MAX_PRODUCT_STACK];
  uint op_end[MAX_PRODUCT_STACK];
  int op_sp = 0;

  interval_list empty_il;
  empty_il.count = 0;

  for (uint i = 0; i < cmd_count; ) {
    ProductCommand cmd = product_commands[cmd_start + i];
    s_nodes++;

    bool has_result = false;
    interval_list result = empty_il;
    uint advance = 1u;

    // Duplicate cache hit: reuse result for shared subtrees across products
    if (u_use_dup_cache == 1 && cmd.duplicate_id != 0u &&
        dup_cache_lookup(cmd.duplicate_id, result)) {
      has_result = true;
      advance = cmd.skip_children;
    }

    if (!has_result) {
      float cmd_t_near;
      bool _bounds_miss = cmd.bounds_skip == 1u ||
          !bounds_hit_tnear(r.origin, r.dir, cmd.bounds_type, cmd.bounds_inv, cmd_t_near) ||
          (u_use_tbest == 1 && cmd_t_near > best_t);
      if (u_use_bounds == 1 && _bounds_miss) {
        s_bounds += cmd.skip_children;
        if (op_sp == 0) return empty_il;
        result = empty_il;
        has_result = true;
        advance = cmd.skip_children;
      } else if (cmd.type == CMD_TYPE_PRIMITIVE) {
        Primitive p = primitives[cmd.id];
        ray lr;
        lr.origin = (p.inv_transform * vec4(r.origin, 1.0)).xyz;
        lr.dir = (p.inv_transform * vec4(r.dir, 0.0)).xyz;
        vec2 hit = NO_HIT_SPAN;
        if (p.type == PRIMITIVE_TYPE_SPHERE) hit = intersect_unit_sphere(lr);
        else if (p.type == PRIMITIVE_TYPE_CUBE) hit = intersect_box_AABB(lr);
        else if (p.type == PRIMITIVE_TYPE_CYLINDER) hit = intersect_cylinder(lr, p.r1, p.r2);
        result = make_primitive_interval(hit, cmd.id);
        if (u_use_dup_cache == 1 && cmd.duplicate_id != 0u)
          dup_cache_store(cmd.duplicate_id, result);
        has_result = true;
        s_leaves++;
      } else { // CMD_TYPE_OPERATION
        op_stack[op_sp] = make_op_entry(cmd.duplicate_id, uint(operations[cmd.id].type));
        op_end[op_sp] = i + cmd.skip_children;
        op_sp++;
        i++;
        continue;
      }
    }

    // Push result and collapse completed operations
    res_stack[res_sp++] = result;
    bool early_skipped = false;
    while (op_sp > 0) {
      op_stack[op_sp - 1]++;
      if (get_op_received(op_stack[op_sp - 1]) < 2u) {
        uint op_t = get_op_type(op_stack[op_sp - 1]);
        if (res_stack[res_sp - 1].count == 0 &&
            (op_t == OP_TYPE_OPINTERSECTION || op_t == OP_TYPE_OPDIFFERENCE)) {
          uint skip_to = op_end[op_sp - 1];
          uint dup = get_op_dup_id(op_stack[op_sp - 1]);
          op_sp--;
          if (u_use_dup_cache == 1 && dup != 0u)
            dup_cache_store(dup, empty_il);
          i = skip_to;
          early_skipped = true;
          continue;
        }
        break;
      }

      interval_list b = res_stack[--res_sp];
      interval_list a = res_stack[--res_sp];
      op_sp--;
      uint op_t = get_op_type(op_stack[op_sp]);
      interval_list merged;
      if ((op_t == OP_TYPE_OPINTERSECTION && (a.count == 0 || b.count == 0)) ||
          (op_t == OP_TYPE_OPDIFFERENCE && a.count == 0)) {
        if (op_sp == 0) return empty_il;
        merged = empty_il;
      } else {
        merged = merge_spans(a, b, op_t);
      }

      // cache store if duplicate
      uint dup = get_op_dup_id(op_stack[op_sp]);
      if (u_use_dup_cache == 1 && dup != 0u)
        dup_cache_store(dup, merged);

      // push merged to the stack
      res_stack[res_sp++] = merged;
    }
    if (!early_skipped)
      i += advance;
  }

  return (res_sp > 0) ? res_stack[0] : empty_il;
}

// Shadow traversal: no prim tracking, early exit on first occlusion
shadow_il eval_product_shadow(ray r, uint cmd_start, uint cmd_count)
{
  shadow_il res_stack[MAX_PRODUCT_STACK];
  int res_sp = 0;
  uint op_stack[MAX_PRODUCT_STACK];
  uint op_end[MAX_PRODUCT_STACK];
  int op_sp = 0;

  shadow_il empty_il;
  empty_il.count = 0;

  for (uint i = 0; i < cmd_count; ) {
    ProductCommand cmd = product_commands[cmd_start + i];

    bool has_result = false;
    shadow_il result = empty_il;
    uint advance = 1u;

    bool _bounds_miss = cmd.bounds_skip == 1u ||
        (cmd.bounds_type == 0u
        ? !intersect_aabb(r.origin, r.dir, cmd.bounds_inv[0].xyz, cmd.bounds_inv[1].xyz) : !intersect_oriented(r.origin, r.dir, cmd.bounds_inv));
    if (u_use_bounds == 1 && _bounds_miss) {
      if (op_sp == 0) return empty_il;
      result = empty_il;
      has_result = true;
      advance = cmd.skip_children;
    } else if (cmd.type == CMD_TYPE_PRIMITIVE) {
      Primitive p = primitives[cmd.id];
      ray lr;
      lr.origin = (p.inv_transform * vec4(r.origin, 1.0)).xyz;
      lr.dir = (p.inv_transform * vec4(r.dir, 0.0)).xyz;
      vec2 hit = NO_HIT_SPAN;
      if (p.type == PRIMITIVE_TYPE_SPHERE) hit = intersect_unit_sphere(lr);
      else if (p.type == PRIMITIVE_TYPE_CUBE) hit = intersect_box_AABB(lr);
      else if (p.type == PRIMITIVE_TYPE_CYLINDER) hit = intersect_cylinder(lr, p.r1, p.r2);
      result = make_shadow_interval(hit);
      has_result = true;
    } else {
      op_stack[op_sp] = (encode_op_type(uint(operations[cmd.id].type)) << 2);
      op_end[op_sp] = i + cmd.skip_children;
      op_sp++;
      i++;
      continue;
    }

    res_stack[res_sp++] = result;
    bool early_skipped = false;
    while (op_sp > 0) {
      op_stack[op_sp - 1]++;
      if (get_op_received(op_stack[op_sp - 1]) < 2u) {
        uint op_t = get_op_type(op_stack[op_sp - 1]);
        if (res_stack[res_sp - 1].count == 0 &&
            (op_t == OP_TYPE_OPINTERSECTION || op_t == OP_TYPE_OPDIFFERENCE)) {
          i = op_end[op_sp - 1];
          op_sp--;
          early_skipped = true;
          continue;
        }
        break;
      }
      shadow_il b = res_stack[--res_sp];
      shadow_il a = res_stack[--res_sp];
      op_sp--;
      uint op_t = get_op_type(op_stack[op_sp]);
      if ((op_t == OP_TYPE_OPINTERSECTION && (a.count == 0 || b.count == 0)) ||
          (op_t == OP_TYPE_OPDIFFERENCE && a.count == 0)) {
        if (op_sp == 0) return empty_il;
        res_stack[res_sp++] = empty_il;
      } else {
        res_stack[res_sp++] = merge_shadow_spans(a, b, op_t);
      }
    }
    if (early_skipped) continue;
    i += advance;
  }

  return (res_sp > 0) ? res_stack[0] : empty_il;
}

// ---- Outer DNF traversal: BVH over products ----
// Internal nodes are spatial groupings (UNION BOUNDING BOXES);
// leaf nodes are individual products.
// best_t tracks the closest opaque front-face hit; used to cull BVH nodes with t_near > best_t.
interval_list csg_traverse(ray r,
  inout uint s_bounds, inout uint s_nodes, inout uint s_leaves)
{
  interval_list result;
  result.count = 0;
  float best_t = 1e30;

  // clear cache
  dup_cache_clear();

  uint bvh_stack[MAX_BVH_STACK];
  int sp = 0;

  uint n = uint(product_bvh.length());
  if (n == 0u) return result;

  // Check root bounds before entering loop
  ProductBVHNode root = product_bvh[0];
  if (root.bounds_skip == 1u) return result;
  if (u_use_bounds == 1) {
    float t_near;
    if (!bounds_hit_tnear(r.origin, r.dir, root.bounds_type, root.bounds_inv, t_near)) {
      s_bounds++;
      return result;
    }
  }
  bvh_stack[sp++] = 0u;

  while (sp > 0) {
    uint i = bvh_stack[--sp];
    if (i >= n) continue;

    ProductBVHNode node = product_bvh[i];

    if (node.bounds_skip == 1u) continue;

    // Re-check against best_t, may have tightened since this node was pushed before
    if (u_use_bounds == 1 && u_use_tbest == 1) {
      float t_near;
      if (!bounds_hit_tnear(r.origin, r.dir, node.bounds_type, node.bounds_inv, t_near)
          || t_near > best_t) {
        s_bounds++;
        continue;
      }
    }

    if (node.is_leaf == 1u) {
      interval_list prod = eval_product(r, node.cmd_start, node.cmd_count, s_bounds, s_nodes, s_leaves, best_t);
      if (u_use_tbest == 1 && prod.count > 0) {
        for (int k = 0; k < prod.count; k++) {
          float t_enter = prod.spans[k].t_enter;
          if (t_enter < best_t) {
            uint prim_id = span_prim_id(prod.spans[k]);
            if (primitives[prim_id].color.a >= 1.0)
              best_t = t_enter;
            break;
          }
        }
      }
      result = merge_spans(result, prod, OP_TYPE_OPUNION);
    } else {
      // Internal node: test both children, push farther first (nearer popped first in neext it)
      uint left = i + 1u;
      uint right = i + 1u + product_bvh[left].skip_children;

      float t_left = 1e30, t_right = 1e30;
      bool hit_left = false, hit_right = false;

      ProductBVHNode ln = product_bvh[left];
      ProductBVHNode rn = product_bvh[right];

      if (ln.bounds_skip != 1u) {
        if (u_use_bounds == 1)
          hit_left = bounds_hit_tnear(r.origin, r.dir, ln.bounds_type, ln.bounds_inv, t_left);
        else {
          hit_left = true;
          t_left = 0.0;
        }
      }
      if (rn.bounds_skip != 1u) {
        if (u_use_bounds == 1)
          hit_right = bounds_hit_tnear(r.origin, r.dir, rn.bounds_type, rn.bounds_inv, t_right);
        else {
          hit_right = true;
          t_right = 0.0;
        }
      }

      if (u_use_tbest == 1) {
        if (hit_left && t_left > best_t) {
          hit_left = false;
          s_bounds++;
        }
        if (hit_right && t_right > best_t) {
          hit_right = false;
          s_bounds++;
        }
      }

      // Push farther child first so nearer is popped first
      if (hit_left && hit_right) {
        if (t_left <= t_right) {
          bvh_stack[sp++] = right;
          bvh_stack[sp++] = left;
        } else {
          bvh_stack[sp++] = left;
          bvh_stack[sp++] = right;
        }
      } else if (hit_left) {
        bvh_stack[sp++] = left;
      } else if (hit_right) {
        bvh_stack[sp++] = right;
      }
    }
  }
  return result;
}

bool csg_shadow_test(ray r) {
  uint bvh_stack[MAX_BVH_STACK];
  int sp = 0;
  bvh_stack[sp++] = 0u;

  uint n = uint(product_bvh.length());

  while (sp > 0) {
    uint i = bvh_stack[--sp];
    if (i >= n) continue;

    ProductBVHNode node = product_bvh[i];
    if (node.bounds_skip == 1u) continue;

    if (u_use_bounds == 1) {
      float t_near;
      bool hit = bounds_hit_tnear(r.origin, r.dir, node.bounds_type, node.bounds_inv, t_near);
      if (!hit) continue;
    }

    if (node.is_leaf == 1u) {
      shadow_il si = eval_product_shadow(r, node.cmd_start, node.cmd_count);
      for (int k = 0; k < si.count; k++) {
        if (shadow_span_enter(si.spans[k]) > SHADOW_T_MIN) return true;
      }
    } else {
      uint left = i + 1u;
      uint right = i + 1u + product_bvh[left].skip_children;

      float t_left = 1e30, t_right = 1e30;
      bool hit_left = false, hit_right = false;

      ProductBVHNode ln = product_bvh[left];
      ProductBVHNode rn = product_bvh[right];

      if (ln.bounds_skip != 1u) {
        if (u_use_bounds == 1)
          hit_left = bounds_hit_tnear(r.origin, r.dir, ln.bounds_type, ln.bounds_inv, t_left);
        else {
          hit_left = true;
          t_left = 0.0;
        }
      }
      if (rn.bounds_skip != 1u) {
        if (u_use_bounds == 1)
          hit_right = bounds_hit_tnear(r.origin, r.dir, rn.bounds_type, rn.bounds_inv, t_right);
        else {
          hit_right = true;
          t_right = 0.0;
        }
      }

      // Push farther child first so nearer is popped first
      if (hit_left && hit_right) {
        if (t_left <= t_right) {
          bvh_stack[sp++] = right;
          bvh_stack[sp++] = left;
        } else {
          bvh_stack[sp++] = left;
          bvh_stack[sp++] = right;
        }
      } else if (hit_left) {
        bvh_stack[sp++] = left;
      } else if (hit_right) {
        bvh_stack[sp++] = right;
      }
    }
  }
  return false;
}

// ---- Compositing ----
vec4 composite(interval_list fl, ray r, ivec2 px) {
  vec3 acc = vec3(0.0);
  float rem = 1.0;
  bool depth_written = false;

  for (int k = 0; k < fl.count; k++) {
    if (rem < 0.01) break;
    float te = fl.spans[k].t_enter;
    if (te <= 0.01) continue;

    vec3 hp = r.origin + r.dir * te;
    if (!depth_written) {
      vec4 clip = u_proj * u_view * vec4(hp, 1.0);
      imageStore(depthOutput, px, vec4(clip.z / clip.w * 0.5 + 0.5, 0, 0, 0));
      depth_written = true;
    }

    float shadow_factor = 1.0;
    if (u_use_shadows == 1) {
      Primitive hp_p = primitives[span_prim_id(fl.spans[k])];
      vec3 lp = (hp_p.inv_transform * vec4(hp, 1.0)).xyz;
      vec3 ln = get_local_normal(hp_p.type, lp, hp_p.r1, hp_p.r2);
      vec3 wn = normalize(transpose(mat3(hp_p.inv_transform)) * ln);
      if (span_invert(fl.spans[k])) wn = -wn;
      vec3 l0 = normalize(mat3(u_inv_view) * normalize(vec3(-1, +1, +1)));
      ray sr;
      sr.origin = hp + wn * 1e-3;
      sr.dir = l0;
      if (csg_shadow_test(sr)) shadow_factor = 0.0;
    }

    vec4 c = get_final_color(hp, primitives[span_prim_id(fl.spans[k])],
        span_invert(fl.spans[k]), shadow_factor);
    acc += c.rgb * c.a * rem;
    rem *= (1.0 - c.a);
  }

  if (!depth_written) imageStore(depthOutput, px, vec4(1, 0, 0, 0));
  return vec4(acc + u_background * rem, 1.0);
}

// ---- Hash for jitter ----
float hash(vec2 p) {
  return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x))));
}

// ---- Main ----
void main() {
  if (gl_LocalInvocationIndex == 0u) {
    wg_bounds_skipped = 0u;
    wg_nodes_visited = 0u;
    wg_leaves_visited = 0u;
  }

  barrier();

  ivec2 px = ivec2(gl_GlobalInvocationID.xy);
  ivec2 dims = imageSize(imgOutput);

  uint inv_bounds = 0u, inv_nodes = 0u, inv_leaves = 0u;

  if (px.x < dims.x && px.y < dims.y) {
    vec4 avg = vec4(0.0);
    const vec2 rgss4[4] = vec2[](
        vec2(0.125, 0.375), vec2(0.375, -0.125),
        vec2(-0.125, -0.375), vec2(-0.375, 0.125)
      );
    const vec2 rgss2[2] = vec2[](vec2(-0.25, -0.25), vec2(0.25, 0.25));
    for (int s = 0; s < u_samples; s++) {
      vec2 jitter;
      if (u_samples == 4) jitter = rgss4[s];
      else if (u_samples == 2) jitter = rgss2[s];
      else jitter = vec2(0.0);
      vec2 uv = (vec2(px) + jitter) / vec2(dims) * 2.0 - 1.0;
      float th = tan(radians(fov) * 0.5);
      vec3 ldir = normalize(vec3(uv.x * aspectRatio * th, uv.y * th, -1.0));
      ray r;
      r.origin = u_camera_pos;
      r.dir = normalize(mat3(u_inv_view) * ldir);

      interval_list spans = csg_traverse(r, inv_bounds, inv_nodes, inv_leaves);
      avg += composite(spans, r, px);
    }
    imageStore(imgOutput, px, avg / float(u_samples));
  }

  atomicAdd(wg_bounds_skipped, inv_bounds);
  atomicAdd(wg_nodes_visited, inv_nodes);
  atomicAdd(wg_leaves_visited, inv_leaves);
  barrier();
  if (gl_LocalInvocationIndex == 0u) {
    atomicAdd(bounds_skipped, wg_bounds_skipped);
    atomicAdd(nodes_visited, wg_nodes_visited);
    atomicAdd(leaves_visited, wg_leaves_visited);
  }
}
