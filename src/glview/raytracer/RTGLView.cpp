// RTGLView.cc
#include "RTGLView.h"

#include <QKeyEvent>
#include <iostream>
#include <string>


static std::string loadShaderFile(const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "[RT] Failed to load shader: " << path << std::endl;
        return "";
    }
    return file.readAll().toStdString();
}

RTGLView::RTGLView(QWidget *parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);  // to receive key events
}

RTGLView::~RTGLView() {
    makeCurrent();
    if (primitivesSSBO) glDeleteBuffers(1, &primitivesSSBO);
    if (operationsSSBO) glDeleteBuffers(1, &operationsSSBO);
    if (commandsSSBO)   glDeleteBuffers(1, &commandsSSBO);
    if (outputTexture)  glDeleteTextures(1, &outputTexture);
    if (quadVAO)        glDeleteVertexArrays(1, &quadVAO);
    if (quadVBO)        glDeleteBuffers(1, &quadVBO);
    if (computeProgram) glDeleteProgram(computeProgram);
    if (quadProgram)    glDeleteProgram(quadProgram);
    doneCurrent();
}

void RTGLView::setRTTree(std::shared_ptr<RTCSGNode> root) {
    this->rtRoot = root;
    this->needsRebuild = true;
    update();  // trigger repaint
}

// ---- GL Init ----

void RTGLView::initializeGL() {
    std::cout << "[RT] OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
  std::string computeSrc = ShaderUtils::loadShaderSource("raytracer/raytracer.glsl");
  computeProgram = compileComputeShader(computeSrc);

  // Quad shader — use OpenSCAD's utility
  std::string vertSrc = ShaderUtils::loadShaderSource("raytracer/base.vert");
  std::string fragSrc = ShaderUtils::loadShaderSource("raytracer/base.frag");
  auto quadShader = ShaderUtils::compileShaderProgram(vertSrc, fragSrc);
  quadProgram = quadShader.shader_program;

    // Setup fullscreen quad
    float quadVertices[] = {
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

    // texture coordinate
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);

    initialized = true;
}

// ---- Resize ----

void RTGLView::resizeGL(int w, int h) {
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
    glBindImageTexture(0, outputTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
}

// ---- Paint (main render) ----

Eigen::Matrix4f RTGLView::getViewMatrix(const Camera& cam) {
  float dist = cam.viewer_distance;

  Eigen::Vector3f eye(0, -dist, 0);
  Eigen::Vector3f center(0, 0, 0);
  Eigen::Vector3f up(0, 0, 1);

  Eigen::Vector3f f = (center - eye).normalized();
  Eigen::Vector3f s = f.cross(up).normalized();
  Eigen::Vector3f u = s.cross(f);

  Eigen::Matrix4f lookAt = Eigen::Matrix4f::Identity();
  lookAt(0,0) = s.x(); lookAt(0,1) = s.y(); lookAt(0,2) = s.z();
  lookAt(1,0) = u.x(); lookAt(1,1) = u.y(); lookAt(1,2) = u.z();
  lookAt(2,0) =-f.x(); lookAt(2,1) =-f.y(); lookAt(2,2) =-f.z();
  lookAt(0,3) = -s.dot(eye);
  lookAt(1,3) = -u.dot(eye);
  lookAt(2,3) =  f.dot(eye);

  // Rotations X, Y, Z (same order as OpenSCAD)
  Eigen::Affine3f rx(Eigen::AngleAxisf(cam.object_rot.x() * M_PI / 180.0, Eigen::Vector3f::UnitX()));
  Eigen::Affine3f ry(Eigen::AngleAxisf(cam.object_rot.y() * M_PI / 180.0, Eigen::Vector3f::UnitY()));
  Eigen::Affine3f rz(Eigen::AngleAxisf(cam.object_rot.z() * M_PI / 180.0, Eigen::Vector3f::UnitZ()));

  // Translation to object
  Eigen::Affine3f t_object(Eigen::Translation3f(
      cam.object_trans.x(), cam.object_trans.y(), cam.object_trans.z()));

  // Combine: lookAt * Rx * Ry * Rz * T (same order as openscad GL calls)
  Eigen::Matrix4f rot = (rx * ry * rz).matrix();
  return lookAt * rot * t_object.matrix();
}

void RTGLView::paintGL() {
    if (!initialized || !rtRoot) return;

    const int w = width() * devicePixelRatio();
    const int h = height() * devicePixelRatio();

    if (needsRebuild) {
        rebuildGPUData();
        needsRebuild = false;
    }

    // compute shader
    glUseProgram(computeProgram);

    // TODO make all these configurable
    float aspectRatio = (float)w / (float)h;
    glUniform1f(glGetUniformLocation(computeProgram, "fov"), fov);
    glUniform1f(glGetUniformLocation(computeProgram, "aspectRatio"), aspectRatio);
    glUniform3f(glGetUniformLocation(computeProgram, "u_light_dir"), 0.5f, 1.0f, 0.5f);
    glUniform1i(glGetUniformLocation(computeProgram, "u_samples"), 8);
    glUniform1i(glGetUniformLocation(computeProgram, "u_rendering_mode"), 0);

  Eigen::Matrix4f view = getViewMatrix(*openscadCam);
  Eigen::Matrix4f invView = view.inverse();
  Eigen::Vector3f camPos = invView.block<3,1>(0,3);  // extract position from inv view

  glUniformMatrix4fv(glGetUniformLocation(computeProgram, "u_inv_view"),
                     1, GL_FALSE, invView.data());
  glUniform3f(glGetUniformLocation(computeProgram, "u_camera_pos"),
              camPos.x(), camPos.y(), camPos.z());

    // Bind SSBOs
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, primitivesSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, operationsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, commandsSSBO);

    // Bind output texture
    glBindImageTexture(0, outputTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    // Dispatch
    glDispatchCompute((w  / 8), (h  / 8), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // --- Draw fullscreen quad ---
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(quadProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, outputTexture);
    glUniform1i(glGetUniformLocation(quadProgram, "screenTexture"), 0);

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void RTGLView::setCamera(const Camera* cam) {
  this->openscadCam = cam;
  this->fov = cam->fovValue();
  update();
}

// ---- Rebuild GPU data from RTCSGNode tree ----

void RTGLView::rebuildGPUData() {
    // Flatten the tree — reuse your CSGTree logic
    std::vector<Primitive> gpuPrimitives;
    std::vector<Operation> gpuOperations;
    std::vector<CSGCommand> gpuCommands;

    CSGTree tree(rtRoot);  // RTCSGNode is compatible with CSGNode for flatten
    tree.flatten_tree(rtRoot, gpuPrimitives, gpuOperations, gpuCommands);

    std::cout << "[RT] Primitives: " << gpuPrimitives.size()
              << " Operations: " << gpuOperations.size()
              << " Commands: " << gpuCommands.size() << std::endl;

    // Delete old SSBOs
    if (primitivesSSBO) glDeleteBuffers(1, &primitivesSSBO);
    if (operationsSSBO) glDeleteBuffers(1, &operationsSSBO);
    if (commandsSSBO)   glDeleteBuffers(1, &commandsSSBO);

    // Create new SSBOs
    auto createSSBO = [](GLuint& id, size_t size, const void* data, GLuint binding) {
        glGenBuffers(1, &id);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, GL_STATIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    };

    createSSBO(primitivesSSBO, gpuPrimitives.size() * sizeof(Primitive), gpuPrimitives.data(), 1);
    createSSBO(operationsSSBO, gpuOperations.size() * sizeof(Operation), gpuOperations.data(), 2);
    createSSBO(commandsSSBO,   gpuCommands.size() * sizeof(CSGCommand), gpuCommands.data(), 3);
}

// ---- Shader compilation helpers ----

GLuint RTGLView::compileComputeShader(const std::string& source) {
    const char* src = source.c_str();
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

// ---- Keyboard input (basic camera for testing) ----

void RTGLView::keyPressEvent(QKeyEvent *event) {
    float step = 0.3f;
    switch (event->key()) {
        case Qt::Key_W: camPos.z() -= step; break;
        case Qt::Key_S: camPos.z() += step; break;
        case Qt::Key_A: camPos.x() -= step; break;
        case Qt::Key_D: camPos.x() += step; break;
        case Qt::Key_Q: camPos.y() += step; break;
        case Qt::Key_E: camPos.y() -= step; break;
        default: QOpenGLWidget::keyPressEvent(event); return;
    }
    update();  // trigger repaint
}
