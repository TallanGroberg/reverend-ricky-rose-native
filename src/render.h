#pragma once
// Renderer API for the native CUDA build. The fractal is raymarched by a
// CUDA kernel into a GPU buffer, pushed into an OpenGL texture through a
// CUDA-mapped pixel buffer, and shown by the UI. No per-frame CPU copies.

#include <string>
#include <vector>
#include "params.h"

struct Camera {
  float camPos[3] = {0, 0, 3.6f};
  float camRight[3] = {1, 0, 0};
  float camUp[3] = {0, 1, 0};
  float camFwd[3] = {0, 0, -1};
};

class Renderer {
public:
  bool init(std::string& err);                 // pick the CUDA device
  void shutdown();
  // (Re)create the GL texture + interop buffer at this size.
  bool resize(int w, int h, std::string& err);
  unsigned int textureId() const { return tex_; }
  int width() const { return w_; }
  int height() const { return h_; }
  // Raymarch one frame into the texture. P.detail and P.step must be set.
  bool renderFrame(const Params& P, const Camera& cam, std::string& err);
  // Render offscreen and save a PPM (parity harness against the web build).
  bool renderSnapshot(const Params& P, const Camera& cam, int w, int h,
                      const std::string& ppmPath, std::string& err);
  std::string deviceName() const { return deviceName_; }

private:
  int w_ = 0, h_ = 0;
  unsigned int tex_ = 0, pbo_ = 0;
  void* glResource_ = nullptr;  // cudaGraphicsResource*
  unsigned char* dBuf_ = nullptr; // uchar4 device scratch for snapshots
  size_t dBufCap_ = 0;
  std::string deviceName_;
};

// Export field evaluation on the GPU: fills fieldOut with (N+1)^3 values
// (x fastest) of the carver field over [-B, B]^3: de(p) with the flatten
// base cut applied, minus the skin offset. B and step mirror the web carver.
bool evalFieldSkin(const Params& P, int N, float skin,
                   std::vector<float>& fieldOut, float& BOut, float& stepOut,
                   std::string& err);
