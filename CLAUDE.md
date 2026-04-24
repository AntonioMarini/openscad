# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Configure (experimental features required for raytracer)
cmake -B build -DEXPERIMENTAL=1

# Build
cmake --build build -j$(nproc)

# Or use the convenience script
./build.sh
```

Key CMake options:
- `-DHEADLESS=ON` / `-DNULLGL=ON` — headless/no-OpenGL builds
- `-DENABLE_CGAL=ON` / `-DENABLE_MANIFOLD=ON` — geometry backends
- `-DUSE_QT6=ON` — Qt6 (default on macOS)
- `-DCLANG_TIDY=1` — enable linter

## Tests

```bash
cd build

# Run default tests
ctest

# Parallel
ctest -j8

# Filter by name
ctest -R <regex>

# Test configs: Default, Heavy, Examples, Bugs, All
ctest -C Heavy

# Unit tests directly (Catch2)
./OpenSCADUnitTests
./OpenSCADUnitTests "*pattern*"

# Generate regression baseline for a test
TEST_GENERATE=1 ctest -R <testname>
```

Regression tests compare CSG text output or PNG images (structural similarity, not pixel-diff). Test data is in `tests/data/`, expected results in `tests/regression/`.

## Code Style

2-space indentation, C++17. Enforced by clang-format:

```bash
./scripts/beautify.sh         # format changed files
pre-commit install             # install hooks (runs clang-format on commit)
```

## Architecture

OpenSCAD compiles a `.scad` script into 3D geometry. The pipeline is:

**Script → Parser → AST → Evaluator → CSG Tree → Geometry → Render**

### Core subsystems (`src/`)

| Directory | Role |
|-----------|------|
| `core/` | Parser (Bison/Flex), AST, expression evaluator, CSG tree builder |
| `geometry/` | Mesh generation via CGAL (`geometry/cgal/`) or Manifold (`geometry/manifold/`) |
| `glview/` | OpenGL rendering: VBO, OpenCSG, camera, offscreen contexts |
| `gui/` | Qt UI: `MainWindow.cc` orchestrates editing/preview/export |
| `io/` | Import/export: STL, DXF, OBJ, 3MF, SVG, PNG, POV-Ray, etc. |
| `utils/` | Math, hashing, version utilities |
| `platform/` | OS-specific abstractions |

### Key classes

- **`CSGTreeEvaluator`** (`core/`) — walks AST, builds high-level CSG tree of operations
- **`GeometryEvaluator`** (`geometry/`) — converts CSG tree to concrete meshes (visitor pattern)
- **`MainWindow`** (`gui/`) — central coordinator; triggers re-evaluation on edits
- **`GLView`** / **`QGLView`** (`glview/`) — OpenGL preview pane

### Raytracer integration (`raytracer-integration` branch)

New GPU ray-tracing path added in parallel to the existing OpenGL renderer:

- **`RTCSGTreeVisitor`** (`src/raytracer/`) — converts AST to `RTCSGNode` binary tree (analogous to `CSGTreeEvaluator`); handles binarization and operation distribution (hoisting Unions upward)
- **`RTCSGNode`** (`src/raytracer/`) — binary tree node: leaf = primitive (sphere/cube/cylinder + transform + color), internal = operation (union/intersection/difference)
- **`CSGTree`** (flattener) — serializes tree to GPU-friendly linear buffers: primitives, operations, commands (with skip offsets for cached subtrees), OBBs for ray acceleration
- **`RTGLView`** (`src/glview/raytracer/`) — QOpenGL widget; dispatches GLSL compute shader, displays result texture with axes/crosshairs/FPS
- **Shaders** (`shaders/raytracer/`) — `raytracer.glsl` is the main compute shader doing single preorder traversal of the command buffer

Raytracer examples live in `examples/Basics/RT/` and `examples/Advanced/RT/`.

### Architecture diagrams

PDF diagrams in `doc/` cover class hierarchy, compile flow, CSG tree structure, and polygon representation.
