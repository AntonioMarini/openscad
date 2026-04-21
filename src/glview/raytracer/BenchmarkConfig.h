#pragma once
#include <string>

struct BenchmarkConfig {
  bool active = false;
  int steps = 72;
  std::string output_dir;

  // Shader knobs (mirror of RT uniforms)
  int rtUseBounds = 1;
  int rtUseCache = 1;
  int rtUseShadows = 4;
  int rtSamples = 1;
  int rtUseDistribution = 1;
  int rtBinarization = 1;  // 0 = naive, 1 = KD
  int rtUseDNF = 1;        // 1 = use DNF (Goldfeather) shader path

  // Viewport size (0 = keep current)
  int bench_width = 0;
  int bench_height = 0;

  bool skipScreenshot = false;
};
