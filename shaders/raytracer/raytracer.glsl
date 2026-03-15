#version 450 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba32f, binding = 0) uniform image2D imgOutput;

// depth texture: used for drawing things on top/behind later
layout(rgba32f, binding = 1) uniform image2D depthOutput;

uniform mat4 u_view;
uniform mat4 u_proj;

#define MAX_SPANS 4
#define MAX_STACK 8
#define MAX_COMMANDS 1024
#define MASK_WORDS (MAX_COMMANDS / 32)
#define CACHE_SIZE 8

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

// --- STACK ---
#define STACK(i)  stack[i]
#define SCACHE(i) span_cache[i]
// --- STACK ---

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
  if (invert_normal) world_normal = -world_normal;

  vec3 view_dir = normalize(u_camera_pos - world_pos);

  // Two headlights in eye/camera space — matches OpenSCAD's GL_LIGHT0 and GL_LIGHT1.
  // Multiplying by mat3(u_inv_view) rotates them into world space so they follow the camera.
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

void set_bit(inout uint mask[MASK_WORDS], int bit) {
  mask[bit / 32] |= (1u << (bit % 32));
}

bool get_bit(uint mask[MASK_WORDS], int bit) {
  return (mask[bit / 32] & (1u << (bit % 32))) != 0u;
}

vec4 csg_span(ray r, ivec2 pixel_coords) {
  interval_list stack[MAX_STACK]; // per-invocation local stack (register/spill, no shared-memory limit)
  vec3 inv_ray_dir = 1.0 / (r.dir + vec3(1e-6));
  uint num_ops = commands.length();
  uint root_id = num_ops - 1;
  CSGCommand root_cmd = commands[root_id];

  if (u_use_obb == 1) {
    if (obbs[root_id].skip == 1u || !intersect_obb(r.origin, r.dir, obbs[root_id].inv_transform)) {
      imageStore(depthOutput, pixel_coords, vec4(1.0, 0.0, 0.0, 0.0));
      return vec4(u_background, 1.0);
    }
  }

  uint skip_mask[MASK_WORDS];
  for (int i = 0; i < MASK_WORDS; i++) skip_mask[i] = 0u;

  // First pass: determine which commands to skip based on OBB intersection
  if (u_use_obb == 1) {
    for (int i = int(num_ops) - 1; i >= 0; i--) {
      if (get_bit(skip_mask, i)) continue;

      CSGCommand cmd = commands[i];
      if (obbs[i].skip == 1u || !intersect_obb(r.origin, r.dir, obbs[i].inv_transform)) {
        uint skip_count = cmd.skip_children;
        for (int j = 0; j < int(skip_count); j++) {
          if (i - j >= 0) set_bit(skip_mask, i - j);
        }
      }
    }
  }

  // Second pass: process commands, skipping those marked in the first pass
  int sp = 0;

  // Initialize ring buffer cache
  for (int c = 0; c < u_cache_size; c++) SCACHE(c).duplicate_id = 0u;
  cache_current_id = 0;

  for (uint i = 0; i < num_ops; i++) {
    CSGCommand command = commands[i];
    bool is_skipped = get_bit(skip_mask, int(i));

    // Cache lookup for duplicate nodes
    if (u_use_cache == 1 && !is_skipped && command.duplicate_id != 0u) {
      int cached = find_in_cache(command.duplicate_id);
      if (cached != -1) {
        // Hit: push cached span -> skip entire subtree
        STACK(sp) = SCACHE(cached).spans;
        sp++;
        i += command.skip_children - 1u; // -1 beacuse loop increments i at next iteration.
        continue;
      }
    }

    // CACHED_REF: no subtree in commands after, result must come from cache.
    if (command.type == ID_OP_TYPE_CACHED_REF) {
      STACK(sp).count = 0;
      sp++;
      continue;
    }

    if (command.type == ID_OP_TYPE_PRIMITIVE) {
      if (is_skipped) {
        STACK(sp).count = 0;
        sp++;
      } else {
        Primitive p = primitives[command.id];
        ray local_ray;
        local_ray.origin = (p.inv_transform * vec4(r.origin, 1.0)).xyz;
        local_ray.dir = (p.inv_transform * vec4(r.dir, 0.0)).xyz;

        // Primitives Span Logic
        vec2 hit = NO_HIT_SPAN;
        if (p.type == PRIMITIVE_TYPE_SPHERE) hit = intersect_unit_sphere(local_ray);
        if (p.type == PRIMITIVE_TYPE_CUBE) hit = intersect_box_AABB(local_ray);
        if (p.type == PRIMITIVE_TYPE_CYLINDER) hit = intersect_cylinder(local_ray, p.r1, p.r2);

        if (hit.x < hit.y) {
          STACK(sp).count = 1;
          STACK(sp).spans[0].interval = hit;
          STACK(sp).spans[0].packed_prim = pack_span_prim(command.id, false);
        } else {
          STACK(sp).count = 0;
        }

        // Save to cache if duplicate
        if (u_use_cache == 1 && command.duplicate_id != 0u) {
          save_to_cache(command.duplicate_id, STACK(sp));
        }

        sp++;
      }
    } else {
      sp--;
      bool has_op2 = STACK(sp).count > 0;

      sp--;
      bool has_op1 = STACK(sp).count > 0;

      if (is_skipped || (!has_op1 && !has_op2 && command.type != OP_TYPE_OPUNION)) {
        STACK(sp).count = 0;
        sp++;
      } else {
        interval_list op2_val;
        op2_val.count = 0;
        if (has_op2) op2_val = STACK(sp + 1);

        interval_list op1_val;
        op1_val.count = 0;
        if (has_op1) op1_val = STACK(sp);

        STACK(sp) = merge_spans(op1_val, op2_val, operations[command.id].type);

        // Save to cache if duplicate
        if (u_use_cache == 1 && command.duplicate_id != 0u) {
          save_to_cache(command.duplicate_id, STACK(sp));
        }

        sp++;
      }
    }
  }

  if (sp > 0 && STACK(0).count > 0) {
    interval_list final_list = STACK(0);
    float t = 1e10;
    int best_idx = -1;
    for (int k = 0; k < final_list.count; k++) {
      float t_enter = final_list.spans[k].interval.x;
      if (t_enter > 0.01 && t_enter < t) {
        t = t_enter;
        best_idx = k;
      }
    }
    if (best_idx != -1) {
      span hit = final_list.spans[best_idx];

      vec3 hitPos = r.origin + r.dir * t;

      // Save also depth value in the openscad viewproj space in the depth texture
      vec4 clipPos = u_proj * u_view * vec4(hitPos, 1.0);
      float depth = (clipPos.z / clipPos.w) * 0.5 + 0.5;
      imageStore(depthOutput, pixel_coords, vec4(depth, 0.0, 0.0, 0.0));

      return get_final_color(hitPos, primitives[span_prim_id(hit)], span_invert(hit));
    }
  }

  // NO HIT
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
