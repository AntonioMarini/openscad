#version 450 core

// Defines
#define MAX_SPANS        3
#define MAX_SHADOW_SPANS 2
#define SHADOW_T_MIN     0.01
#define MAX_BVH_STACK    20
#define MAX_DUP_CACHE    4

// local workgroup input layout
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// local workgroup output layout
layout(rgba32f, binding = 0) uniform image2D imgOutput;
layout(r32f, binding = 1) uniform image2D depthOutput;

// Camera
uniform mat4 u_view, u_proj;
uniform mat4 u_inv_view;
uniform vec3 u_camera_pos;
uniform float fov;
uniform float aspect_ratio;

// Light
uniform vec3 u_light_dir;

// OpensSCAD colors
uniform vec3 u_background;
uniform vec3 u_default_mat_color;
uniform vec3 u_default_back_color;

// Config uniform flags
uniform int u_use_bounds;
uniform int u_use_tbest;
uniform int u_use_shadows;
uniform int u_use_dup_cache;
uniform int u_samples;
uniform int u_rendering_mode;

// Enums and Constants

// PRIMITIVES
const uint PRIMITIVE_TYPE_SPHERE = 1u;
const uint PRIMITIVE_TYPE_CUBE = 2u;
const uint PRIMITIVE_TYPE_CYLINDER = 4u;

// OPERATIONS
const uint OP_TYPE_OPUNION = 1u;
const uint OP_TYPE_OPINTERSECTION = 2u;
const uint OP_TYPE_OPDIFFERENCE = 4u;

// ProductCommand.type values
const uint CMD_TYPE_PRIMITIVE = 0u;
const uint CMD_TYPE_OPERATION = 1u;

const vec2 NO_HIT_SPAN = vec2(1.0 / 0.0, -1.0 / 0.0);

// Data Structures

struct Primitive {
  int type;
  int defaultColor;
  int _pad0;
  int _pad1;
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
