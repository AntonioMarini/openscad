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
  void setTreeStats(int nodeCount, int depth);

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void rebuildGPUData();
  GLuint compileComputeShader(const std::string& source);
  GLuint compileQuadShader(const std::string& vertSrc, const std::string& fragSrc);

  QElapsedTimer fpsTimer;
  int frameCount = 0;
  float currentFps = 0.0f;

  // Configurable shader uniforms
  int rtUseObb = 1, rtUseCache = 1, rtUseShadows = 1, rtSamples = 1;

  // Benchmark orbit state
  BenchmarkConfig benchConfig;
  int benchNodeCount = 0;
  int benchTreeDepth = 0;
  Camera benchmarkCam;
  int benchStep = 0;
  bool benchScreenshotTaken = false;
  std::vector<float> benchFrameMs;
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
  GLuint primitivesSSBO = 0;
  GLuint operationsSSBO = 0;
  GLuint commandsSSBO = 0;
  GLuint obbsSSBO = 0;
  GLuint statsSSBO = 0;

  bool initialized = false;

  // Camera (simple for now)
  Eigen::Vector3f camPos{0.0f, 0.0f, 3.0f};
  float fov = 45.0f;
};
