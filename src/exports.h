#pragma once
// Mesh export for the native CUDA build of Reverend Ricky Rose's Dance
// Party Visuals: binary GLB and STL writers plus the surface-nets carver.
// Mirrors the web build's exporter in fractal-explorer-audio-desktop:
// same carve, smoothing, scene transform, embedded settings recipe,
// batch frames, and output filenames.

#include <string>
#include <vector>

#include "params.h"

struct ExportJobs {
  // Binary glTF with positions, smooth normals, uint32 indices, and the
  // settings recipe under asset.extras.fractalExplorer.settings.
  static bool writeGLB(const std::string& path, const std::vector<float>& positions, const std::vector<unsigned int>& indices, const std::string& settingsText, std::string& err);
  // Binary STL (80 zero header bytes, little-endian triangle count,
  // per-face normals), same layout the web build writes.
  static bool writeSTL(const std::string& path, const std::vector<float>& positions, const std::vector<unsigned int>& indices, std::string& err);
};

struct Carver {
  // Carve one mesh from Params at grid detail N. smooth runs 2 Taubin
  // rounds (the web build's smoothing) before any transform. When
  // applyScene is true the full single-export scene transform runs:
  // export scale, pivot, and cluster copies from the UI state. When
  // false the raw carved mesh comes back and the caller transforms it
  // (the batch exports below apply the cluster-free variant themselves).
  // Triangle count is indices.size() / 3. Returns false only when the
  // field evaluation itself fails; an empty mesh is not a failure here.
  static bool carve(const Params& P, const UiState& ui, int N, bool smooth, bool applyScene,
                    std::vector<float>& positions, std::vector<unsigned int>& indices, std::string& err);
  // Single export at the UI's export detail: GLB with the settings
  // recipe when glb is true, STL otherwise. path is the full file path.
  static bool exportSingle(const Params& P, const UiState& ui, const std::string& settingsText, bool glb, const std::string& path, std::string& err);
  // Batch exports into dir, web filenames, one carve per frame. Frames
  // that carve empty are skipped, as the web build does. anim is the
  // Living form clock value, matching the web exporter's uAnim.
  static bool exportGrowth(const UiState& ui, float anim, const std::string& dir, const std::string& settingsText, std::string& err);
  static bool exportMorphFrames(const UiState& ui, float anim, const std::string& dir, const std::string& settingsText, std::string& err);
  static bool exportVariants(const UiState& ui, float anim, const std::string& dir, const std::string& settingsText, std::string& err);
};
