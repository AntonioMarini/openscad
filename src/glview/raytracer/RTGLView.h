#pragma once

#include "glview/system-gl.h"

#include "gui/MouseSelector.h"
#include "gui/QGLView.h"

#include <QWheelEvent>
#include <QtGlobal>
#include <QOpenGLWidget>
#include <string>

#include <Eigen/Core>

#include "raytracer/RTCSGNode.h"

#include <QElapsedTimer>

class RTGLView : public QOpenGLWidget
{
  Q_OBJECT

public:
  explicit RTGLView(QWidget *parent = nullptr);
  ~RTGLView() override;

  void setQGLView(QGLView* view);

  bool mouse_drag_active = false;

  void setCamera(const Camera* cam);
  void setRTTree(std::shared_ptr<RTCSGNode> root);
  Eigen::Matrix4f getViewMatrix(const Camera& cam);

  void setColorScheme(const ColorScheme* cs);

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

  QGLView* qglview = nullptr;
  QPointF lastMousePos;

  void wheelEvent(QWheelEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;

  const Camera* openscadCam = nullptr;
  std::shared_ptr<RTCSGNode> rtRoot;

  // Colorscheme
  const ColorScheme* colorscheme = nullptr;

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

  bool initialized = false;
  bool needsRebuild = true;

  // Camera (simple for now)
  Eigen::Vector3f camPos{0.0f, 0.0f, 3.0f};
  float fov = 45.0f;
};
