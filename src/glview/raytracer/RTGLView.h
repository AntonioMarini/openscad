#pragma once

#include "glview/system-gl.h"

#include "gui/MouseSelector.h"
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

/***
 * Class responsible for handling and viewing, in a QT widget, the RT CSG Tree.
 *
 * This takes lot of duplicate code from QGLView.cc (Openscad preview widget), should be refactored to be
 * a cleaner solution.
 *
 */
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
  void setUseDNF(bool dnf) { useDNF = dnf; needsRebuild = true; }

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void rebuildGPUData();
  GLuint compileComputeShader(const std::string& source, int maxStack,
                              const std::string& define_name = "MAX_STACK");
  GLuint compileQuadShader(const std::string& vertSrc, const std::string& fragSrc);

  QElapsedTimer fpsTimer;
  int frameCount = 0;
  float currentFps = 0.0f;

  // Configurable shader uniforms
  int   rtUseBounds = 1, rtUseCache = 1, rtUseShadows = 1, rtSamples = 1;
  // Benchmark orbit state
  BenchmarkConfig benchConfig;
  int benchNodeCount = 0;
  int benchTreeDepth = 0;
  int benchPreDistNodeCount = 0;
  Camera benchmarkCam;
  int benchStep = 0;
  bool benchScreenshotTaken = false;
  std::vector<float> benchFrameMs;
  uint64_t benchBoundsSkippedTotal = 0;
  uint64_t benchCacheHitsTotal     = 0;
  uint64_t benchCacheMissesTotal   = 0;
  uint64_t benchNodesVisitedTotal  = 0;
  uint64_t benchLeavesVisitedTotal = 0;
  QTimer *benchTimer = nullptr;
  QElapsedTimer benchFrameTimer;

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

  // axes and crosshair
  void showAxes(const Color4f& col);
  void showCrosshairs(const Color4f& col);
  void showScalemarkers(const Color4f& col);
  void showSmallaxes(const Color4f& col);
  float getDPI();
  void decodeMarkerValue(double i, double l, int size_div_sm);

  // GPU handles
  GLuint computeProgram = 0;
  GLuint quadProgram = 0;
  GLuint depthCopyProgram = 0;
  GLuint outputTexture = 0;
  GLuint depthTexture = 0;
  GLuint quadVAO = 0, quadVBO = 0;
  // span shader: bindings 1-4 geometry, 5 stats
  GLuint primitivesSSBO = 0;
  GLuint operationsSSBO = 0;
  GLuint commandsSSBO = 0;
  GLuint boundsSSBO = 0;
  // DNF shader: bindings 3-4 replace commands/obbs with product BVH/commands
  GLuint dnfComputeProgram = 0;
  GLuint productBVHSSBO = 0;
  GLuint productCommandsSSBO = 0;
  // binding 5: stats (shared by both paths)
  GLuint statsSSBO = 0;

  bool initialized = false;
  bool useDNF = false;

  // Compute shader source and current MAX_STACK compile-time constant
  std::string computeShaderSrc;
  std::string dnfComputeShaderSrc;
  int currentMaxStack = 0;
  int currentDNFMaxStack = 0;

  // Camera (simple for now)
  Eigen::Vector3f camPos{0.0f, 0.0f, 3.0f};
  float fov = 45.0f;
};
