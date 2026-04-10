#pragma once
#include <string>

struct BenchmarkConfig {
  bool active = false;
  int steps = 72;
  double elevation = 25.0;  // camera pitch (degrees), applied to object_rot.x
  double distance = -1.0;   // orbit radius; -1 = use camera's existing viewer_distance
  std::string output_dir;

  // Shader knobs (mirror of RT uniforms)
  int rtUseObb = 1;
  int rtUseCache = 1;
  int rtUseShadows = 4;
  int rtSamples = 1;
  int rtUseDistribution = 1;
  int rtBinarization = 1;  // 0 = naive, 1 = KD

  // Viewport size (0 = keep current)
  int bench_width = 0;
  int bench_height = 0;
};
