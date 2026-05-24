#pragma once

#include "glview/system-gl.h"

#include "gui/QGLView.h"

#include <QWheelEvent>
#include <QtGlobal>
#include <QOpenGLWidget>
#include <QTimer>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "raytracer/RTCSGNode.h"
#include "glview/raytracer/BenchmarkConfig.h"

#include <QElapsedTimer>

class RTGLView : public QOpenGLWidget
{
  Q_OBJECT

public:
  explicit RTGLView(QWidget *parent = nullptr);
  ~RTGLView() override;

  // used for re trigger the flattening of the tree -> passing the new data to the gpu again
  bool needsRebuild = true;

  void setQGLView(QGLView *view);  // temporary solution to pick camera and other things from the
                                   // openscad main preview widget

  bool mouse_drag_active = false;

  void setCamera(const Camera *cam);
  void setRTTree(std::shared_ptr<RTCSGNode> root);

  Eigen::Matrix4f getViewMatrix(const Camera& cam);
  void setColorScheme(const ColorScheme *cs);

  void setBenchmarkConfig(const BenchmarkConfig& cfg);
  void startBenchmarkOrbit();
  void setTreeStats(int nodeCount, int depth, int preDistNodeCount = 0);

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void rebuildGPUData();
  GLuint compileComputeShader(const std::string& source, int maxStack,
                              const std::string& define_name = "MAX_STACK");

  QElapsedTimer fpsTimer;
  int frameCount = 0;
  float currentFps = 0.0f;

  GLuint gpuTimerQuery = 0;
  float computeMs = 0.0f;

  // Configurable shader uniforms
  int rtUseBounds = 1, rtUseCache = 1, rtUseShadows = 0, rtSamples = 1, rtUseTBest = 1, rtUseAO = 0;
  int rtProductBVH = 1;

  // Benchmark orbit state
  BenchmarkConfig benchConfig;
  int benchNodeCount = 0;
  int benchTreeDepth = 0;
  int benchPreDistNodeCount = 0;
  Camera benchmarkCam;
  int benchStep = 0;
  bool benchScreenshotTaken = false;
  struct FrameStats {
    float ms;
    uint32_t boundsSkipped;
    uint32_t nodesVisited;
    uint32_t leavesVisited;
  };
  std::vector<FrameStats> benchFrames;
  QTimer *benchTimer = nullptr;

  void advanceBenchmarkStep();
  void finishBenchmark();

  QGLView *qglview = nullptr;
  QPointF lastMousePos;

  void wheelEvent(QWheelEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;

  const Camera *openscadCam = nullptr;
  std::shared_ptr<RTCSGNode> rtRoot;

  // Colorscheme
  const ColorScheme *colorscheme = nullptr;

  void showSmallaxes(const Color4f& col);
  float getDPI();

  // GPU handles
  GLuint computeProgram = 0;
  GLuint quadProgram = 0;
  GLuint depthCopyProgram = 0;
  GLuint outputTexture = 0;
  GLuint depthTexture = 0;
  GLuint quadVAO = 0, quadVBO = 0;

  // SSBOs
  GLuint primitivesSSBO = 0;
  GLuint operationsSSBO = 0;
  GLuint productBVHSSBO = 0;
  GLuint productCommandsSSBO = 0;
  GLuint statsSSBO = 0;

  bool initialized = false;
  int accumFrame = 0;
  int maxAccumFrames = 1;

  // Compute shader source and current MAX_STACK compile-time constant
  std::string computeShaderSrc;
  int currentDNFMaxStack = 0;

  // Camera
  Eigen::Vector3f camPos{0.0f, 0.0f, 3.0f};
  float fov = 45.0f;
};
