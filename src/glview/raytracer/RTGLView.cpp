// RTGLView.cc
#include "RTGLView.h"

#include "raytracer/CSGCommand.h"
#include "raytracer/CSGTree.h"
#include "glview/system-gl.h"

#include <QApplication>
#include <QKeyEvent>
#include <QSurfaceFormat>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <string>

RTGLView::RTGLView(QWidget *parent) : QOpenGLWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);  // to receive key events
  QSurfaceFormat fmt = format();
  fmt.setSwapInterval(0);  // disable vsync for uncapped benchmark FPS
  setFormat(fmt);
}

RTGLView::~RTGLView()
{
  makeCurrent();
  if (primitivesSSBO) glDeleteBuffers(1, &primitivesSSBO);
  if (operationsSSBO) glDeleteBuffers(1, &operationsSSBO);
  if (productBVHSSBO) glDeleteBuffers(1, &productBVHSSBO);
  if (productCommandsSSBO) glDeleteBuffers(1, &productCommandsSSBO);
  if (statsSSBO) glDeleteBuffers(1, &statsSSBO);
  if (outputTexture) glDeleteTextures(1, &outputTexture);
  if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
  if (quadVBO) glDeleteBuffers(1, &quadVBO);
  if (gpuTimerQuery) glDeleteQueries(1, &gpuTimerQuery);
  if (computeProgram) glDeleteProgram(computeProgram);
  if (quadProgram) glDeleteProgram(quadProgram);
  doneCurrent();
}

void RTGLView::setRTTree(std::shared_ptr<RTCSGNode> root)
{
  std::cout << "[RT] setRTTree called, root=" << (root != nullptr) << std::endl;
  this->rtRoot = root;
  this->needsRebuild = true;
  update();  // trigger repaint
}

float RTGLView::getDPI() { return devicePixelRatio(); }
// ---- GL Init ----

void RTGLView::initializeGL()
{
  std::cout << "[RT] OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

  computeShaderSrc = ShaderUtils::loadShaderSource("raytracer/raytracer_span_dnf.glsl");
  currentDNFMaxStack = 4;
  computeProgram = compileComputeShader(computeShaderSrc, currentDNFMaxStack, "MAX_PRODUCT_STACK");

  // Quad shader — use OpenSCAD's utility
  std::string vertSrc = ShaderUtils::loadShaderSource("raytracer/base.vert");
  std::string fragSrc = ShaderUtils::loadShaderSource("raytracer/base.frag");
  auto quadShader = ShaderUtils::compileShaderProgram(vertSrc, fragSrc);
  quadProgram = quadShader.shader_program;

  std::string depthSrc = ShaderUtils::loadShaderSource("raytracer/base_depth.glsl");
  auto depthShader = ShaderUtils::compileShaderProgram(vertSrc, depthSrc);
  depthCopyProgram = depthShader.shader_program;

  float quadVertices[] = {
    -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
  };

  glGenVertexArrays(1, &quadVAO);
  glGenBuffers(1, &quadVBO);
  glBindVertexArray(quadVAO);
  glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

  // position
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);

  // texture coordinate
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));

  glBindVertexArray(0);

  glGenQueries(1, &gpuTimerQuery);

  initialized = true;
}

// ---- Resize ----

void RTGLView::resizeGL(int w, int h)
{
  // Recreate output texture at new size
  if (outputTexture) glDeleteTextures(1, &outputTexture);

  glGenTextures(1, &outputTexture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, outputTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glBindImageTexture(0, outputTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

  if (depthTexture) glDeleteTextures(1, &depthTexture);

  glGenTextures(1, &depthTexture);
  glBindTexture(GL_TEXTURE_2D, depthTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
  accumFrame = 0;
}

// ---- Paint (main render) ----

Eigen::Matrix4f RTGLView::getViewMatrix(const Camera& cam)
{
  float dist = cam.viewer_distance;

  Eigen::Vector3f eye(0, -dist, 0);
  Eigen::Vector3f center(0, 0, 0);
  Eigen::Vector3f up(0, 0, 1);

  Eigen::Vector3f f = (center - eye).normalized();
  Eigen::Vector3f s = f.cross(up).normalized();
  Eigen::Vector3f u = s.cross(f);

  Eigen::Matrix4f lookAt = Eigen::Matrix4f::Identity();
  lookAt(0, 0) = s.x();
  lookAt(0, 1) = s.y();
  lookAt(0, 2) = s.z();
  lookAt(1, 0) = u.x();
  lookAt(1, 1) = u.y();
  lookAt(1, 2) = u.z();
  lookAt(2, 0) = -f.x();
  lookAt(2, 1) = -f.y();
  lookAt(2, 2) = -f.z();
  lookAt(0, 3) = -s.dot(eye);
  lookAt(1, 3) = -u.dot(eye);
  lookAt(2, 3) = f.dot(eye);

  // Rotations X, Y, Z (same order as OpenSCAD)
  Eigen::Affine3f rx(Eigen::AngleAxisf(cam.object_rot.x() * M_PI / 180.0, Eigen::Vector3f::UnitX()));
  Eigen::Affine3f ry(Eigen::AngleAxisf(cam.object_rot.y() * M_PI / 180.0, Eigen::Vector3f::UnitY()));
  Eigen::Affine3f rz(Eigen::AngleAxisf(cam.object_rot.z() * M_PI / 180.0, Eigen::Vector3f::UnitZ()));

  // Translation to object
  Eigen::Affine3f t_object(
    Eigen::Translation3f(cam.object_trans.x(), cam.object_trans.y(), cam.object_trans.z()));

  // Combine: lookAt * Rx * Ry * Rz * T (same order as openscad GL calls)
  Eigen::Matrix4f rot = (rx * ry * rz).matrix();
  return lookAt * rot * t_object.matrix();
}

void RTGLView::paintGL()
{
  frameCount++;
  if (frameCount == 1) fpsTimer.start();
  if (fpsTimer.elapsed() >= 1000) {
    currentFps = frameCount * 1000.0f / fpsTimer.elapsed();
    std::cout << "[RT] FPS: " << currentFps << std::endl;
    frameCount = 0;
    fpsTimer.restart();
  }

  if (!initialized || !rtRoot) return;

  const int w = width() * devicePixelRatio();
  const int h = height() * devicePixelRatio();

  if (needsRebuild) {
    rebuildGPUData();
    needsRebuild = false;
    accumFrame = 0;
  }

  // Reset GPU stats before each measured frame so per-frame values fit in uint32
  if (benchConfig.active && benchStep >= 1 && statsSSBO) {
    const uint32_t zeros[3] = {0, 0, 0};
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, statsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 3 * sizeof(uint32_t), zeros);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  }

  // ---- Compute shader ----
  GLuint activeProgram = computeProgram;
  glUseProgram(activeProgram);

  float aspectRatio = (float)w / (float)h;
  glUniform1f(glGetUniformLocation(activeProgram, "fov"), fov);
  glUniform1f(glGetUniformLocation(activeProgram, "aspectRatio"), aspectRatio);
  glUniform3f(glGetUniformLocation(activeProgram, "u_light_dir"), -1.0f, -1.0f, 1.0f);
  bool interactive = mouse_drag_active;
  glUniform1i(glGetUniformLocation(activeProgram, "u_samples"), 4);
  glUniform1i(glGetUniformLocation(activeProgram, "u_rendering_mode"), 0);
  glUniform1i(glGetUniformLocation(activeProgram, "u_use_bounds"), rtUseBounds);
  glUniform1i(glGetUniformLocation(activeProgram, "u_use_tbest"), rtUseTBest);
  glUniform1i(glGetUniformLocation(activeProgram, "u_use_shadows"), 0);
  glUniform1i(glGetUniformLocation(activeProgram, "u_use_dup_cache"), rtUseCache);
  glUniform1i(glGetUniformLocation(activeProgram, "u_frame_index"), accumFrame);
  glUniform1i(glGetUniformLocation(activeProgram, "u_use_ao"), 0);

  if (colorscheme) {
    Color4f bg = ColorMap::getColor(*colorscheme, RenderColor::BACKGROUND_COLOR);
    Color4f defaultMatColor = ColorMap::getColor(*colorscheme, RenderColor::OPENCSG_FACE_FRONT_COLOR);
    Color4f backFaceColor = ColorMap::getColor(*colorscheme, RenderColor::OPENCSG_FACE_BACK_COLOR);

    glUniform3f(glGetUniformLocation(activeProgram, "u_background"), bg.r(), bg.g(), bg.b());
    glUniform3f(glGetUniformLocation(activeProgram, "u_default_mat_color"), defaultMatColor.r(),
                defaultMatColor.g(), defaultMatColor.b());
    glUniform3f(glGetUniformLocation(activeProgram, "u_default_back_color"), backFaceColor.r(),
                backFaceColor.g(), backFaceColor.b());
  } else {
    glUniform3f(glGetUniformLocation(activeProgram, "u_background"), 0.5f, 0.7f, 1.0f);
    glUniform3f(glGetUniformLocation(activeProgram, "u_default_mat_color"), 1.0f, 1.0f,
                1.0f);  // White fallback
    glUniform3f(glGetUniformLocation(activeProgram, "u_default_back_color"), 0.8f, 0.2f, 0.8f);
  }

  Eigen::Matrix4f view = getViewMatrix(*openscadCam);
  Eigen::Matrix4f invView = view.inverse();
  Eigen::Vector3f camPos = invView.block<3, 1>(0, 3);

  glUniformMatrix4fv(glGetUniformLocation(activeProgram, "u_inv_view"), 1, GL_FALSE, invView.data());
  glUniform3f(glGetUniformLocation(activeProgram, "u_camera_pos"), camPos.x(), camPos.y(), camPos.z());

  // Projection matrix — must match gluPerspective used for axes
  float dist = openscadCam->viewer_distance;
  float nearP = 0.1f;
  float farP = dist * 10.0f;
  float fovRad = openscadCam->fov * M_PI / 180.0f;
  float tanHalf = tan(fovRad / 2.0f);

  Eigen::Matrix4f proj = Eigen::Matrix4f::Zero();
  proj(0, 0) = 1.0f / (aspectRatio * tanHalf);
  proj(1, 1) = 1.0f / tanHalf;
  proj(2, 2) = -(farP + nearP) / (farP - nearP);
  proj(2, 3) = -(2.0f * farP * nearP) / (farP - nearP);
  proj(3, 2) = -1.0f;

  glUniformMatrix4fv(glGetUniformLocation(activeProgram, "u_view"), 1, GL_FALSE, view.data());
  glUniformMatrix4fv(glGetUniformLocation(activeProgram, "u_proj"), 1, GL_FALSE, proj.data());

  // Bind SSBOs
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, primitivesSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, productBVHSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, productCommandsSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, statsSSBO);

  // Bind output textures (color + depth)
  glBindImageTexture(0, outputTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
  glBindImageTexture(1, depthTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

  // (GPU timing via GL_TIME_ELAPSED query below)

  // Dispatch (timed with GL timer query)
  glBeginQuery(GL_TIME_ELAPSED, gpuTimerQuery);
  glDispatchCompute(((w + 7) / 8), ((h + 7) / 8), 1);
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  glEndQuery(GL_TIME_ELAPSED);

  // Read back GPU compute time
  GLuint64 elapsedNs = 0;
  glGetQueryObjectui64v(gpuTimerQuery, GL_QUERY_RESULT, &elapsedNs);
  computeMs = static_cast<float>(elapsedNs) / 1e6f;

  accumFrame++;

  // ---- Draw fullscreen quad (color) ----
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glUseProgram(quadProgram);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, outputTexture);
  glUniform1i(glGetUniformLocation(quadProgram, "screenTexture"), 0);
  if (colorscheme) {
    Color4f bg = ColorMap::getColor(*colorscheme, RenderColor::BACKGROUND_COLOR);
    glUniform3f(glGetUniformLocation(quadProgram, "u_background"), bg.r(), bg.g(), bg.b());
  } else {
    glUniform3f(glGetUniformLocation(quadProgram, "u_background"), 0.5f, 0.7f, 1.0f);
  }

  glBindVertexArray(quadVAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_ALWAYS);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glDepthMask(GL_TRUE);

  glUseProgram(depthCopyProgram);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, depthTexture);
  glUniform1i(glGetUniformLocation(depthCopyProgram, "depthTex"), 0);

  glBindVertexArray(quadVAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthFunc(GL_LESS);

  // Draw axes with depth test
  glUseProgram(0);
  glBindTexture(GL_TEXTURE_2D, 0);

  Color4f axesColor = ColorMap::getColor(*colorscheme, RenderColor::AXES_COLOR);

  float aspect = (float)w / (float)h;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(openscadCam->fov, aspect, nearP, farP);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt(0.0, -dist, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);

  glRotated(openscadCam->object_rot.x(), 1.0, 0.0, 0.0);
  glRotated(openscadCam->object_rot.y(), 0.0, 1.0, 0.0);
  glRotated(openscadCam->object_rot.z(), 0.0, 0.0, 1.0);

  glTranslated(openscadCam->object_trans.x(), openscadCam->object_trans.y(),
               openscadCam->object_trans.z());

  glDisable(GL_DEPTH_TEST);
  showSmallaxes(axesColor);
  glEnable(GL_DEPTH_TEST);

  if (benchConfig.active) {
    if (benchStep == 0) {
      // Capture screenshot at rot_degrees 0° (warmup frame) so the overview image
      // shows a canonical front-facing view, unaffected by timing.
      if (!benchConfig.skipScreenshot) {
        grabFramebuffer().save(QString::fromStdString(benchConfig.output_dir + "/frame.png"));
      }
      benchScreenshotTaken = true;
    } else {
      float ms = computeMs;
      FrameStats fs{ms, 0, 0, 0};

      // Read back per-frame GPU stats.
      // SSBO was reset at the start of this frame, so values represent this frame only.
      if (statsSSBO) {
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        uint32_t gpuStats[3] = {0, 0, 0};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, statsSSBO);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 3 * sizeof(uint32_t), gpuStats);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        fs.boundsSkipped = gpuStats[0];
        fs.nodesVisited = gpuStats[1];
        fs.leavesVisited = gpuStats[2];
      }
      benchFrames.push_back(fs);
    }
    advanceBenchmarkStep();
  }
  if (accumFrame < maxAccumFrames || benchConfig.active) {
    update();
  }
}

void RTGLView::setCamera(const Camera *cam)
{
  this->openscadCam = cam;
  this->fov = cam->fovValue();
  accumFrame = 0;
  update();
}

// ---- Rebuild GPU data from RTCSGNode tree ----

void RTGLView::rebuildGPUData()
{
  std::vector<Primitive> gpuPrimitives;
  std::vector<Operation> gpuOperations;
  std::vector<CSGCommand> gpuCommands;
  std::vector<RTBounds> gpuBounds;

  CSGTree tree(rtRoot);

  // ---- Upload SSBOs ----
  auto createSSBO = [](GLuint& id, size_t size, const void *data, GLuint binding) {
    if (id) glDeleteBuffers(1, &id);
    // Always upload at least 1 byte so the SSBO binding is valid even if unused
    size_t safeSize = size > 0 ? size : 16;
    glGenBuffers(1, &id);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(safeSize), size > 0 ? data : nullptr,
                 GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  };

  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  // DNF path: flatten into product BVH + product commands
  std::vector<ProductCommand> gpuProductCmds;
  std::vector<ProductBVHNode> gpuBVHNodes;
  uint32_t maxProductStack = tree.flatten_to_dnf(rtRoot, gpuPrimitives, gpuOperations, gpuProductCmds,
                                                 gpuBVHNodes, rtProductBVH != 0);

  // Leaf count = (bvh_nodes.size() + 1) / 2 for a full binary tree
  uint32_t leaf_count = 0;
  for (const auto& n : gpuBVHNodes)
    if (n.is_leaf) leaf_count++;
  std::cout << "[RT/DNF] products=" << leaf_count << " bvh_nodes=" << gpuBVHNodes.size()
            << " cmds=" << gpuProductCmds.size() << " prims=" << gpuPrimitives.size()
            << " ops=" << gpuOperations.size() << std::endl;

  int neededDNFStack = static_cast<int>(maxProductStack);
  if (neededDNFStack != currentDNFMaxStack) {
    currentDNFMaxStack = neededDNFStack;
    if (computeProgram) glDeleteProgram(computeProgram);
    computeProgram = compileComputeShader(computeShaderSrc, currentDNFMaxStack, "MAX_PRODUCT_STACK");
    std::cout << "[RT/DNF] Recompiled shader with MAX_PRODUCT_STACK=" << currentDNFMaxStack << std::endl;
  }

  createSSBO(primitivesSSBO, gpuPrimitives.size() * sizeof(Primitive), gpuPrimitives.data(), 1);
  createSSBO(operationsSSBO, gpuOperations.size() * sizeof(Operation), gpuOperations.data(), 2);
  createSSBO(productBVHSSBO, gpuBVHNodes.size() * sizeof(ProductBVHNode), gpuBVHNodes.data(), 3);
  createSSBO(productCommandsSSBO, gpuProductCmds.size() * sizeof(ProductCommand), gpuProductCmds.data(),
             4);

  // Stats SSBO (binding 5)
  if (statsSSBO) glDeleteBuffers(1, &statsSSBO);
  const uint32_t zeroStats[3] = {0, 0, 0};
  glGenBuffers(1, &statsSSBO);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, statsSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER, 3 * sizeof(uint32_t), zeroStats, GL_DYNAMIC_READ);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, statsSSBO);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// ---- Shader compilation helpers ----

GLuint RTGLView::compileComputeShader(const std::string& source, int maxStack,
                                      const std::string& define_name)
{
  // Inject #define after the #version line so it overrides the shader default.
  std::string patched = source;
  auto nl = patched.find('\n');
  if (nl != std::string::npos) {
    patched.insert(nl + 1, "#define " + define_name + " " + std::to_string(maxStack) + "\n");
  }

  const char *src = patched.c_str();
  GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[1024];
    glGetShaderInfoLog(shader, 1024, nullptr, log);
    std::cerr << "[RT] Compute shader error:\n" << log << std::endl;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, shader);
  glLinkProgram(program);

  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    char log[1024];
    glGetProgramInfoLog(program, 1024, nullptr, log);
    std::cerr << "[RT] Program link error:\n" << log << std::endl;
  }

  glDeleteShader(shader);
  return program;
}

void RTGLView::showSmallaxes(const Color4f& col)
{
  auto dpi = this->getDPI();
  // Small axis cross in the lower left corner
  glDepthFunc(GL_ALWAYS);

  const int w = width() * devicePixelRatio();
  const int h = height() * devicePixelRatio();
  float aspectratio = (float)w / (float)h;

  // Set up an orthographic projection of the axis cross in the corner
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glTranslatef(-0.8f, -0.8f, 0.0f);
  auto scale = 90.0;
  glOrtho(-scale * dpi * aspectratio, scale * dpi * aspectratio, -scale * dpi, scale * dpi, -scale * dpi,
          scale * dpi);
  gluLookAt(0.0, -1.0, 0.0,  // eye
            0.0, 0.0, 0.0,   // center
            0.0, 0.0, 1.0);  // up

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glRotated(openscadCam->object_rot.x(), 1.0, 0.0, 0.0);
  glRotated(openscadCam->object_rot.y(), 0.0, 1.0, 0.0);
  glRotated(openscadCam->object_rot.z(), 0.0, 0.0, 1.0);

  glLineWidth(dpi);
  glBegin(GL_LINES);
  glColor3d(1.0, 0.0, 0.0);
  glVertex3d(0, 0, 0);
  glVertex3d(10 * dpi, 0, 0);
  glColor3d(0.0, 1.0, 0.0);
  glVertex3d(0, 0, 0);
  glVertex3d(0, 10 * dpi, 0);
  glColor3d(0.0, 0.0, 1.0);
  glVertex3d(0, 0, 0);
  glVertex3d(0, 0, 10 * dpi);
  glEnd();

  GLdouble mat_model[16];
  glGetDoublev(GL_MODELVIEW_MATRIX, mat_model);

  GLdouble mat_proj[16];
  glGetDoublev(GL_PROJECTION_MATRIX, mat_proj);

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);

  GLdouble xlabel_x, xlabel_y, xlabel_z;
  gluProject(12 * dpi, 0, 0, mat_model, mat_proj, viewport, &xlabel_x, &xlabel_y, &xlabel_z);
  xlabel_x = std::round(xlabel_x);
  xlabel_y = std::round(xlabel_y);

  GLdouble ylabel_x, ylabel_y, ylabel_z;
  gluProject(0, 12 * dpi, 0, mat_model, mat_proj, viewport, &ylabel_x, &ylabel_y, &ylabel_z);
  ylabel_x = std::round(ylabel_x);
  ylabel_y = std::round(ylabel_y);

  GLdouble zlabel_x, zlabel_y, zlabel_z;
  gluProject(0, 0, 12 * dpi, mat_model, mat_proj, viewport, &zlabel_x, &zlabel_y, &zlabel_z);
  zlabel_x = std::round(zlabel_x);
  zlabel_y = std::round(zlabel_y);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glTranslated(-1, -1, 0);
  glScaled(2.0 / viewport[2], 2.0 / viewport[3], 1);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glColor3f(col.r(), col.g(), col.b());

  float d = 3 * dpi;
  glBegin(GL_LINES);
  // X Label
  glVertex3d(xlabel_x - d, xlabel_y - d, 0);
  glVertex3d(xlabel_x + d, xlabel_y + d, 0);
  glVertex3d(xlabel_x - d, xlabel_y + d, 0);
  glVertex3d(xlabel_x + d, xlabel_y - d, 0);
  // Y Label
  glVertex3d(ylabel_x - d, ylabel_y - d, 0);
  glVertex3d(ylabel_x + d, ylabel_y + d, 0);
  glVertex3d(ylabel_x - d, ylabel_y + d, 0);
  glVertex3d(ylabel_x, ylabel_y, 0);
  // Z Label
  glVertex3d(zlabel_x - d, zlabel_y - d, 0);
  glVertex3d(zlabel_x + d, zlabel_y - d, 0);
  glVertex3d(zlabel_x - d, zlabel_y + d, 0);
  glVertex3d(zlabel_x + d, zlabel_y + d, 0);
  glVertex3d(zlabel_x - d, zlabel_y - d, 0);
  glVertex3d(zlabel_x + d, zlabel_y + d, 0);
  glEnd();
}


void RTGLView::setColorScheme(const ColorScheme *cs) { this->colorscheme = cs; }

void RTGLView::setBenchmarkConfig(const BenchmarkConfig& cfg)
{
  benchConfig = cfg;
  rtUseBounds = cfg.rtUseBounds;
  rtUseCache = cfg.rtUseCache;
  rtUseShadows = cfg.rtUseShadows;
  rtSamples = cfg.rtSamples;
  rtUseTBest = cfg.rtUseTBest;
  rtProductBVH = cfg.rtProductBVH;
  needsRebuild = true;
}

void RTGLView::setTreeStats(int nodeCount, int depth, int preDistNodeCount)
{
  benchNodeCount = nodeCount;
  benchTreeDepth = depth;
  benchPreDistNodeCount = preDistNodeCount > 0 ? preDistNodeCount : nodeCount;
}

void RTGLView::startBenchmarkOrbit()
{
  if (!openscadCam) return;
  benchmarkCam = *openscadCam;
  benchmarkCam.object_rot.y() = 0.0;
  openscadCam = &benchmarkCam;

  benchStep = 0;
  benchFrames.clear();
  benchScreenshotTaken = false;

  if (benchConfig.bench_width > 0 && benchConfig.bench_height > 0) {
    qreal dpr = devicePixelRatio();
    int lw = static_cast<int>(std::ceil(benchConfig.bench_width / dpr));
    int lh = static_cast<int>(std::ceil(benchConfig.bench_height / dpr));
    setFixedSize(lw, lh);
  }

  // Reset GPU stats counters for this run
  makeCurrent();
  if (statsSSBO) {
    const uint32_t zeros[3] = {0, 0, 0};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, statsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 3 * sizeof(uint32_t), zeros);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  }

  std::filesystem::create_directories(benchConfig.output_dir);

  if (!benchTimer) {
    benchTimer = new QTimer(this);
    benchTimer->setInterval(0);
    connect(benchTimer, &QTimer::timeout, this, QOverload<>::of(&QOpenGLWidget::update));
  }
  benchTimer->start();
}

void RTGLView::advanceBenchmarkStep()
{
  benchStep++;
  if (benchStep <= benchConfig.steps) {
    benchmarkCam.object_rot.y() =
      static_cast<double>(benchStep) * 360.0 / static_cast<double>(benchConfig.steps);
  } else {
    finishBenchmark();
  }
}

void RTGLView::finishBenchmark()
{
  if (benchTimer) benchTimer->stop();

  // Write CSV with per-frame GPU stats
  std::string csvPath = benchConfig.output_dir + "/measurements.csv";
  std::ofstream csv(csvPath);
  csv << "step,rot_degrees,frame_ms,fps,nodes_visited,leaves_visited\n";
  uint64_t nodesVisitedTotal = 0, leavesVisitedTotal = 0;
  for (int i = 0; i < static_cast<int>(benchFrames.size()); ++i) {
    const auto& fs = benchFrames[i];
    float fps = fs.ms > 0.0f ? 1000.0f / fs.ms : 0.0f;
    double rot_degrees = static_cast<double>(i + 1) * 360.0 / static_cast<double>(benchConfig.steps);
    csv << (i + 1) << "," << rot_degrees << "," << fs.ms << "," << fps << "," << fs.nodesVisited << ","
        << fs.leavesVisited << "\n";
    nodesVisitedTotal += fs.nodesVisited;
    leavesVisitedTotal += fs.leavesVisited;
  }
  csv.close();

  // Print summary
  if (!benchFrames.empty()) {
    float total = 0.0f;
    float min_ms = benchFrames[0].ms, max_ms = benchFrames[0].ms;
    for (const auto& fs : benchFrames) {
      total += fs.ms;
      min_ms = std::min(min_ms, fs.ms);
      max_ms = std::max(max_ms, fs.ms);
    }
    float avg_ms = total / static_cast<float>(benchFrames.size());
    float avg_fps = avg_ms > 0.0f ? 1000.0f / avg_ms : 0.0f;
    float max_fps = min_ms > 0.0f ? 1000.0f / min_ms : 0.0f;
    float min_fps = max_ms > 0.0f ? 1000.0f / max_ms : 0.0f;

    const int px = width() * devicePixelRatio();
    const int py = height() * devicePixelRatio();
    std::cout << "[BENCH] total_frames=" << benchFrames.size() << " avg_fps=" << avg_fps
              << " min_fps=" << min_fps << " max_fps=" << max_fps << " node_count=" << benchNodeCount
              << " tree_depth=" << benchTreeDepth << " render_w=" << px << " render_h=" << py
              << " nodes_visited=" << nodesVisitedTotal << " leaves_visited=" << leavesVisitedTotal
              << " output=" << benchConfig.output_dir << std::endl;
  }

  QApplication::quit();
}


// ---- Keyboard input (basic camera for testing) ----

void RTGLView::keyPressEvent(QKeyEvent *event)
{
  float step = 0.3f;
  switch (event->key()) {
  case Qt::Key_W: camPos.z() -= step; break;
  case Qt::Key_S: camPos.z() += step; break;
  case Qt::Key_A: camPos.x() -= step; break;
  case Qt::Key_D: camPos.x() += step; break;
  case Qt::Key_Q: camPos.y() += step; break;
  case Qt::Key_E: camPos.y() -= step; break;
  default:        QOpenGLWidget::keyPressEvent(event); return;
  }
  accumFrame = 0;
  update();  // trigger repaint
}

void RTGLView::setQGLView(QGLView *view)
{
  this->qglview = view;
  connect(qglview, &QGLView::cameraChanged, this, [this]() {
    accumFrame = 0;
    update();
  });
}

// Mouse events
void RTGLView::mousePressEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  lastMousePos = event->globalPosition();
#else
  lastMousePos = event->globalPos();
#endif
  this->mouse_drag_active = true;
}

void RTGLView::mouseMoveEvent(QMouseEvent *event)
{
  if (!qglview || !mouse_drag_active) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  auto thisPos = event->globalPosition();
#else
  auto thisPos = event->globalPos();
#endif

  double dx = (thisPos.x() - lastMousePos.x()) * 0.7;
  double dy = (thisPos.y() - lastMousePos.y()) * 0.7;

  // Determine button
  int buttonIndex = -1;
  bool multipleButtons = false;
  if (event->buttons() & Qt::LeftButton) buttonIndex = 0;
  if (event->buttons() & Qt::MiddleButton) {
    if (buttonIndex != -1) multipleButtons = true;
    else buttonIndex = 1;
  }
  if (event->buttons() & Qt::RightButton) {
    if (buttonIndex != -1) multipleButtons = true;
    else buttonIndex = 2;
  }

  // Determine modifier
  int modifierIndex = 0;
  if (QApplication::keyboardModifiers() & Qt::ShiftModifier) modifierIndex = 1;
  if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
    modifierIndex = (modifierIndex == 1) ? 3 : 2;
  }

  if (buttonIndex != -1 && !multipleButtons) {
    // Rotation
    double rx = dx, ry = dy, rz = 0;
    if (buttonIndex == 0 && modifierIndex == 0) {
      qglview->rotate(dy, 0, dx, true);
    }
    // Translation
    else if (buttonIndex == 2 && modifierIndex == 0) {
      double zoom = qglview->cam.zoomValue();
      double mx = (dx / width()) * 3.0 * zoom;
      double mz = (dy / height()) * 3.0 * zoom;
      qglview->translate(mx, 0, mz, true);
    }
  }

  lastMousePos = thisPos;
}
void RTGLView::mouseReleaseEvent(QMouseEvent *event)
{
  Q_UNUSED(event);
  this->mouse_drag_active = false;
  accumFrame = 0;
  update();
}

void RTGLView::mouseDoubleClickEvent(QMouseEvent *event) { Q_UNUSED(event); }

void RTGLView::wheelEvent(QWheelEvent *event)
{
  if (!qglview) return;
  int delta = event->angleDelta().y();
  qglview->zoom(delta, true);
}
