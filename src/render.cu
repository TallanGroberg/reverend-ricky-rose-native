// CUDA renderer: one raymarched pixel per thread, written into a
// CUDA-mapped OpenGL pixel buffer, then uploaded to the display texture.

#include "render.h"
#include "fractal.cuh"

#include <cuda_runtime.h>
#include <gl/GL.h>
#include <cuda_gl_interop.h>
#include "glprocs.h"
#include <cstdio>
#include <cstring>

#define CUDA_OK(call) do { cudaError_t e_ = (call); if (e_ != cudaSuccess) { \
  err = std::string("CUDA error: ") + cudaGetErrorString(e_); return false; } } while (0)

__global__ void renderKernel(const Params P, float3 ro, float3 right, float3 up, float3 fwd,
                             int W, int H, uchar4* out) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= W || y >= H) return;
  // gl_FragCoord is bottom-up; our texture row 0 is the top.
  float fx = x + 0.5f;
  float fy = (H - 1 - y) + 0.5f;
  float2 uv = make_float2((2.0f * fx - W) / (float)H, (2.0f * fy - H) / (float)H);
  float3 rd = norm3(fwd * 1.6f + right * uv.x + up * uv.y);
  float3 col = shadeRay(P, ro, rd);
  uchar4 px;
  px.x = (unsigned char)(clampf(col.x, 0.0f, 1.0f) * 255.0f);
  px.y = (unsigned char)(clampf(col.y, 0.0f, 1.0f) * 255.0f);
  px.z = (unsigned char)(clampf(col.z, 0.0f, 1.0f) * 255.0f);
  px.w = 255;
  out[y * W + x] = px;
}

__global__ void fieldKernel(const Params P, int W, float B, float step, float skin, float* out) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  int j = blockIdx.y * blockDim.y + threadIdx.y;
  int k = blockIdx.z * blockDim.z + threadIdx.z;
  if (i >= W || j >= W || k >= W) return;
  float3 p = make_float3(-B + i * step, -B + j * step, -B + k * step);
  EvalCtx ctx;
  float v = de(P, p, ctx);
  if (P.baseOn) v = fmaxf(v, (P.baseH - p.y) / fmaxf(P.squash, 1.0f));
  out[i + W * (j + W * k)] = v - skin;
}

bool Renderer::init(std::string& err) {
  int count = 0;
  CUDA_OK(cudaGetDeviceCount(&count));
  if (count < 1) { err = "No CUDA device found. The native build needs an NVIDIA GPU."; return false; }
  // Prefer the device with the most multiprocessors (the 5090 in a mixed rig).
  int best = 0, bestSM = -1;
  for (int i = 0; i < count; i++) {
    cudaDeviceProp prop;
    CUDA_OK(cudaGetDeviceProperties(&prop, i));
    if (prop.multiProcessorCount > bestSM) { bestSM = prop.multiProcessorCount; best = i; }
  }
  CUDA_OK(cudaSetDevice(best));
  cudaDeviceProp prop;
  CUDA_OK(cudaGetDeviceProperties(&prop, best));
  deviceName_ = prop.name;
  return true;
}

void Renderer::shutdown() {
  if (glResource_) {
    cudaGraphicsUnregisterResource((cudaGraphicsResource*)glResource_);
    glResource_ = nullptr;
  }
  if (pbo_) { pglDeleteBuffers(1, &pbo_); pbo_ = 0; }
  if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
  if (dBuf_) { cudaFree(dBuf_); dBuf_ = nullptr; dBufCap_ = 0; }
}

bool Renderer::resize(int w, int h, std::string& err) {
  if (w == w_ && h == h_ && tex_) return true;
  if (glResource_) {
    cudaGraphicsUnregisterResource((cudaGraphicsResource*)glResource_);
    glResource_ = nullptr;
  }
  if (!tex_) glGenTextures(1, &tex_);
  glBindTexture(GL_TEXTURE_2D, tex_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  if (!pbo_) pglGenBuffers(1, &pbo_);
  pglBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_);
  pglBufferData(GL_PIXEL_UNPACK_BUFFER, (size_t)w * h * 4, nullptr, GL_DYNAMIC_DRAW);
  pglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  cudaGraphicsResource* res = nullptr;
  CUDA_OK(cudaGraphicsGLRegisterBuffer(&res, pbo_, cudaGraphicsMapFlagsWriteDiscard));
  glResource_ = res;
  w_ = w; h_ = h;
  return true;
}

bool Renderer::renderFrame(const Params& P, const Camera& cam, std::string& err) {
  if (!glResource_ || w_ < 1 || h_ < 1) { err = "Renderer not sized yet."; return false; }
  cudaGraphicsResource* res = (cudaGraphicsResource*)glResource_;
  CUDA_OK(cudaGraphicsMapResources(1, &res, 0));
  uchar4* dev = nullptr;
  size_t bytes = 0;
  cudaError_t e = cudaGraphicsResourceGetMappedPointer((void**)&dev, &bytes, res);
  if (e != cudaSuccess || !dev || bytes < (size_t)w_ * h_ * 4) {
    cudaGraphicsUnmapResources(1, &res, 0);
    err = std::string("Could not map the pixel buffer: ") + cudaGetErrorString(e);
    return false;
  }
  float3 ro = make_float3(cam.camPos[0], cam.camPos[1], cam.camPos[2]);
  float3 right = make_float3(cam.camRight[0], cam.camRight[1], cam.camRight[2]);
  float3 up = make_float3(cam.camUp[0], cam.camUp[1], cam.camUp[2]);
  float3 fwd = make_float3(cam.camFwd[0], cam.camFwd[1], cam.camFwd[2]);
  dim3 block(16, 16), grid((w_ + 15) / 16, (h_ + 15) / 16);
  renderKernel<<<grid, block>>>(P, ro, right, up, fwd, w_, h_, dev);
  CUDA_OK(cudaGetLastError());
  CUDA_OK(cudaGraphicsUnmapResources(1, &res, 0));
  glBindTexture(GL_TEXTURE_2D, tex_);
  pglBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  pglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  return true;
}

bool Renderer::renderSnapshot(const Params& P, const Camera& cam, int w, int h,
                              const std::string& ppmPath, std::string& err) {
  size_t need = (size_t)w * h * 4;
  if (dBufCap_ < need) {
    if (dBuf_) cudaFree(dBuf_);
    CUDA_OK(cudaMalloc(&dBuf_, need));
    dBufCap_ = need;
  }
  float3 ro = make_float3(cam.camPos[0], cam.camPos[1], cam.camPos[2]);
  float3 right = make_float3(cam.camRight[0], cam.camRight[1], cam.camRight[2]);
  float3 up = make_float3(cam.camUp[0], cam.camUp[1], cam.camUp[2]);
  float3 fwd = make_float3(cam.camFwd[0], cam.camFwd[1], cam.camFwd[2]);
  dim3 block(16, 16), grid((w + 15) / 16, (h + 15) / 16);
  renderKernel<<<grid, block>>>(P, ro, right, up, fwd, w, h, (uchar4*)dBuf_);
  CUDA_OK(cudaGetLastError());
  std::vector<unsigned char> host(need);
  CUDA_OK(cudaMemcpy(host.data(), dBuf_, need, cudaMemcpyDeviceToHost));
  FILE* f = fopen(ppmPath.c_str(), "wb");
  if (!f) { err = "Could not open snapshot path for writing."; return false; }
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (size_t i = 0; i < (size_t)w * h; i++) fwrite(host.data() + i * 4, 1, 3, f);
  fclose(f);
  return true;
}

bool evalFieldSkin(const Params& P, int N, float skin,
                   std::vector<float>& fieldOut, float& BOut, float& stepOut,
                   std::string& err) {
  const float B = 2.2f;
  const float step = 2.0f * B / (float)N;
  const int W = N + 1;
  size_t count = (size_t)W * W * W;
  float* dev = nullptr;
  cudaError_t e = cudaMalloc(&dev, count * sizeof(float));
  if (e != cudaSuccess) { err = std::string("CUDA field allocation failed: ") + cudaGetErrorString(e); return false; }
  dim3 block(8, 8, 8), grid((W + 7) / 8, (W + 7) / 8, (W + 7) / 8);
  fieldKernel<<<grid, block>>>(P, W, B, step, skin, dev);
  e = cudaGetLastError();
  if (e == cudaSuccess) {
    fieldOut.assign(count, 0.0f);
    e = cudaMemcpy(fieldOut.data(), dev, count * sizeof(float), cudaMemcpyDeviceToHost);
  }
  cudaFree(dev);
  if (e != cudaSuccess) { err = std::string("CUDA field evaluation failed: ") + cudaGetErrorString(e); return false; }
  BOut = B; stepOut = step;
  return true;
}
