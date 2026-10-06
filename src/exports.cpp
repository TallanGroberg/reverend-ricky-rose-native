#include "exports.h"

#include "render.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace {

// Math.round from JavaScript: halves round toward positive infinity.
long long jsRound(double v) { return (long long)std::floor(v + 0.5); }

void writeU32(std::ofstream& out, uint32_t v) {
  unsigned char b[4] = {(unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff),
                        (unsigned char)((v >> 16) & 0xff), (unsigned char)((v >> 24) & 0xff)};
  out.write((const char*)b, 4);
}

std::ofstream openBinary(const std::string& path, std::string& err) {
  std::error_code ec;
  std::filesystem::path p(path);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  if (!out) err = "Could not open " + path + " for writing.";
  return out;
}

std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// Shortest float formatting that round-trips, like JSON.stringify.
std::string fmtFloat(float v) {
  char buf[64];
  auto r = std::to_chars(buf, buf + sizeof buf, v);
  if (r.ec == std::errc()) return std::string(buf, r.ptr);
  std::snprintf(buf, sizeof buf, "%.9g", (double)v);
  return buf;
}

// The web build's settingsTextWith: rewrite the value of any id=value
// line whose id appears in overrides, leave everything else untouched.
std::string settingsTextWith(const std::string& text, const std::map<std::string, std::string>& overrides) {
  std::string out;
  size_t start = 0;
  bool first = true;
  while (true) {
    size_t nl = text.find('\n', start);
    std::string line = (nl == std::string::npos) ? text.substr(start) : text.substr(start, nl - start);
    size_t eq = line.find('=');
    if (eq != std::string::npos && eq > 0) {
      auto it = overrides.find(line.substr(0, eq));
      if (it != overrides.end()) line = line.substr(0, eq) + "=" + it->second;
    }
    if (!first) out += '\n';
    out += line;
    first = false;
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return out;
}

// Area-weighted smooth vertex normals, as the web build computes them.
void vertexNormals(const std::vector<float>& pos, const std::vector<unsigned int>& tris, std::vector<float>& nor) {
  nor.assign(pos.size(), 0.0f);
  for (size_t t = 0; t + 2 < tris.size(); t += 3) {
    size_t a = (size_t)tris[t] * 3, b = (size_t)tris[t + 1] * 3, c = (size_t)tris[t + 2] * 3;
    double ux = (double)pos[b] - pos[a], uy = (double)pos[b + 1] - pos[a + 1], uz = (double)pos[b + 2] - pos[a + 2];
    double vx = (double)pos[c] - pos[a], vy = (double)pos[c + 1] - pos[a + 1], vz = (double)pos[c + 2] - pos[a + 2];
    float nx = (float)(uy * vz - uz * vy), ny = (float)(uz * vx - ux * vz), nz = (float)(ux * vy - uy * vx);
    nor[a] += nx; nor[a + 1] += ny; nor[a + 2] += nz;
    nor[b] += nx; nor[b + 1] += ny; nor[b + 2] += nz;
    nor[c] += nx; nor[c + 1] += ny; nor[c + 2] += nz;
  }
  for (size_t i = 0; i + 2 < nor.size(); i += 3) {
    double l = std::sqrt((double)nor[i] * nor[i] + (double)nor[i + 1] * nor[i + 1] + (double)nor[i + 2] * nor[i + 2]);
    if (!(l > 0)) l = 1;
    nor[i] = (float)(nor[i] / l); nor[i + 1] = (float)(nor[i + 1] / l); nor[i + 2] = (float)(nor[i + 2] / l);
  }
}

// Taubin smoothing, the web build's smoothMesh: rounds of lambda 0.5
// then -0.53, neighbor average over directed triangle edges (duplicates
// where triangles share an edge pair are kept, exactly like the web).
void smoothMesh(std::vector<float>& pos, const std::vector<unsigned int>& tris, int rounds) {
  const size_t nv = pos.size() / 3;
  if (!nv || tris.empty()) return;
  std::vector<uint32_t> deg(nv, 0);
  for (size_t t = 0; t + 2 < tris.size(); t += 3) {
    deg[tris[t]] += 2; deg[tris[t + 1]] += 2; deg[tris[t + 2]] += 2;
  }
  std::vector<uint32_t> offs(nv + 1, 0);
  for (size_t i = 0; i < nv; i++) offs[i + 1] = offs[i] + deg[i];
  std::vector<int32_t> nbr(offs[nv]);
  std::vector<uint32_t> fill(offs.begin(), offs.end() - 1);
  auto addEdge = [&](uint32_t a, uint32_t b) { nbr[fill[a]++] = (int32_t)b; };
  for (size_t t = 0; t + 2 < tris.size(); t += 3) {
    uint32_t a = tris[t], b = tris[t + 1], c = tris[t + 2];
    addEdge(a, b); addEdge(b, a); addEdge(b, c); addEdge(c, b); addEdge(c, a); addEdge(a, c);
  }
  std::vector<float> tmp(pos.size());
  const double lambdas[2] = {0.5, -0.53};
  for (int r = 0; r < rounds; r++) {
    for (double lambda : lambdas) {
      tmp = pos;
      for (size_t v = 0; v < nv; v++) {
        uint32_t s = offs[v], e = offs[v + 1];
        size_t m = (size_t)(e - s);
        if (!m) continue;
        double ax = 0, ay = 0, az = 0;
        for (uint32_t n = s; n < e; n++) {
          size_t w = (size_t)nbr[n] * 3;
          ax += tmp[w]; ay += tmp[w + 1]; az += tmp[w + 2];
        }
        size_t vi = v * 3;
        pos[vi] = (float)(tmp[vi] + lambda * (ax / (double)m - tmp[vi]));
        pos[vi + 1] = (float)(tmp[vi + 1] + lambda * (ay / (double)m - tmp[vi + 1]));
        pos[vi + 2] = (float)(tmp[vi + 2] + lambda * (az / (double)m - tmp[vi + 2]));
      }
    }
  }
}

// The web build's sceneTransform: recenter x and z on the bounds,
// offset y by pivot (center keeps y as authored, ground sits on min y,
// top hangs from max y), apply export scale, then lay out cluster
// copies in a row, ring, or grid with spacing and a size trend.
void sceneTransform(const std::vector<float>& posIn, const std::vector<unsigned int>& trisIn,
                    const UiState& ui, bool useCluster,
                    std::vector<float>& posOut, std::vector<unsigned int>& trisOut) {
  posOut.clear();
  trisOut.clear();
  if (posIn.empty()) return;
  const double scale = ui.exscale;
  double mn[3] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                  std::numeric_limits<double>::infinity()};
  double mx[3] = {-mn[0], -mn[1], -mn[2]};
  for (size_t i = 0; i + 2 < posIn.size(); i += 3)
    for (int a = 0; a < 3; a++) {
      if (posIn[i + a] < mn[a]) mn[a] = posIn[i + a];
      if (posIn[i + a] > mx[a]) mx[a] = posIn[i + a];
    }
  const double cx = (mn[0] + mx[0]) / 2, cz = (mn[2] + mx[2]) / 2;
  const double oy = ui.pivot == 1 ? mn[1] : (ui.pivot == 2 ? mx[1] : 0.0);
  const int copies = useCluster ? (int)jsRound(ui.copies) : 1;
  const double spacing = ui.clustersp, trend = ui.trend / 100.0;
  const double wdt = (mx[0] - mn[0]) * spacing, dep = (mx[2] - mn[2]) * spacing;
  const double twoPi = 6.283185307179586;
  for (int i = 0; i < copies; i++) {
    const double f = copies > 1 ? std::pow(1.0 + trend, i - (copies - 1) / 2.0) : 1.0;
    double tx = 0, tz = 0, rot = 0;
    if (copies > 1) {
      if (ui.layout == 0) {  // row
        tx = (i - (copies - 1) / 2.0) * wdt;
      } else if (ui.layout == 2) {  // grid
        int cols = (int)std::ceil(std::sqrt((double)copies));
        int rows = (int)std::ceil((double)copies / cols);
        tx = ((i % cols) - (cols - 1) / 2.0) * wdt;
        tz = ((i / cols) - (rows - 1) / 2.0) * dep;
      } else {  // ring
        double ang = i / (double)copies * twoPi;
        double rad = spacing * std::max(wdt, dep) * copies / twoPi;
        tx = std::cos(ang) * rad;
        tz = std::sin(ang) * rad;
        rot = -ang;
      }
    }
    const unsigned int voff = (unsigned int)(posOut.size() / 3);
    const double cr = std::cos(rot), sr = std::sin(rot);
    for (size_t v = 0; v + 2 < posIn.size(); v += 3) {
      double lx = (posIn[v] - cx) * scale * f;
      double ly = (posIn[v + 1] - oy) * scale * f;
      double lz = (posIn[v + 2] - cz) * scale * f;
      posOut.push_back((float)(cr * lx + sr * lz + tx));
      posOut.push_back((float)ly);
      posOut.push_back((float)(-sr * lx + cr * lz + tz));
    }
    for (unsigned int ix : trisIn) trisOut.push_back(ix + voff);
  }
}

// Surface nets over the GPU field, the same algorithm the web build's
// carveMesh runs (slab by slab, per-cell vertex at the average of the
// sign-changing edge crossings, quads around sign-changing grid edges).
// The web build approximates the field with a coarse/fine band evaluator
// that clamps far points to plus or minus 10; that only changes crossing
// interpolation slightly, never signs or topology, so evaluating the
// full grid directly is the faithful equivalent here.
bool carveMeshNative(const Params& P, int N, std::vector<float>& verts,
                     std::vector<unsigned int>& tris, std::string& err) {
  verts.clear();
  tris.clear();
  if (N < 1) {
    err = "Export detail is too low to carve.";
    return false;
  }
  const int W = N + 1;
  // The web carver aims half a step inside the skin (deSkin): the raw
  // raymarched field never crosses zero. evalFieldSkin subtracts the
  // skin we pass, so ask for step * 0.45 with step from the expected
  // 2.2 half extent, then correct by the renderer's returned step.
  const float skinPassed = (float)((2.0 * 2.2 / N) * 0.45);
  std::vector<float> field;
  float B = 0, step = 0;
  std::string evalErr;
  if (!evalFieldSkin(P, N, skinPassed, field, B, step, evalErr)) {
    err = evalErr.empty() ? "The GPU field evaluation failed, so there is nothing to carve." : evalErr;
    return false;
  }
  if (field.size() != (size_t)W * (size_t)W * (size_t)W) {
    err = "The GPU field evaluation returned an unexpected grid size.";
    return false;
  }
  const float skinWanted = step * 0.45f;
  if (std::fabs(skinWanted - skinPassed) > 1e-9f) {
    const float d = skinWanted - skinPassed;
    for (float& v : field) v -= d;
  }
  auto F = [&](int i, int j, int k) -> float {
    return field[(size_t)i + (size_t)W * ((size_t)j + (size_t)W * (size_t)k)];
  };
  static const int corners[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
                                    {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
  static const int edges[12][2] = {{0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
                                   {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}};
  std::vector<int32_t> prevCell((size_t)N * N, -1), curCell((size_t)N * N, -1);
  auto cellAt = [](const std::vector<int32_t>& arr, int i, int j, int n) -> int32_t {
    return (i < 0 || j < 0 || i >= n || j >= n) ? -1 : arr[(size_t)i + (size_t)n * (size_t)j];
  };
  auto quad = [&](int32_t a, int32_t b, int32_t c, int32_t d, bool flip) {
    if (a < 0 || b < 0 || c < 0 || d < 0) return;
    if (flip) {
      tris.push_back((unsigned int)a); tris.push_back((unsigned int)b); tris.push_back((unsigned int)c);
      tris.push_back((unsigned int)b); tris.push_back((unsigned int)d); tris.push_back((unsigned int)c);
    } else {
      tris.push_back((unsigned int)a); tris.push_back((unsigned int)c); tris.push_back((unsigned int)b);
      tris.push_back((unsigned int)b); tris.push_back((unsigned int)c); tris.push_back((unsigned int)d);
    }
  };
  for (int k = 0; k < N; k++) {
    std::fill(curCell.begin(), curCell.end(), -1);
    for (int j = 0; j < N; j++)
      for (int i = 0; i < N; i++) {
        float cv[8];
        int mask = 0;
        for (int n = 0; n < 8; n++) {
          cv[n] = F(i + corners[n][0], j + corners[n][1], k + corners[n][2]);
          if (cv[n] < 0) mask |= (1 << n);
        }
        if (mask == 0 || mask == 255) continue;
        double ax = 0, ay = 0, az = 0;
        int cnt = 0;
        for (const auto& e : edges) {
          float va = cv[e[0]], vb = cv[e[1]];
          if ((va < 0) == (vb < 0)) continue;
          double t = (double)va / ((double)va - (double)vb);
          const int* pa = corners[e[0]];
          const int* pb = corners[e[1]];
          ax += -B + (i + pa[0] + (pb[0] - pa[0]) * t) * step;
          ay += -B + (j + pa[1] + (pb[1] - pa[1]) * t) * step;
          az += -B + (k + pa[2] + (pb[2] - pa[2]) * t) * step;
          cnt++;
        }
        curCell[(size_t)i + (size_t)N * (size_t)j] = (int32_t)(verts.size() / 3);
        verts.push_back((float)(ax / cnt));
        verts.push_back((float)(ay / cnt));
        verts.push_back((float)(az / cnt));
      }
    for (int j = 0; j < W; j++)
      for (int i = 0; i < W; i++) {
        bool s = F(i, j, k) < 0;
        if (i < N && ((F(i + 1, j, k) < 0) != s))
          quad(cellAt(prevCell, i, j - 1, N), cellAt(prevCell, i, j, N),
               cellAt(curCell, i, j - 1, N), cellAt(curCell, i, j, N), s);
        if (j < N && ((F(i, j + 1, k) < 0) != s))
          quad(cellAt(prevCell, i - 1, j, N), cellAt(curCell, i - 1, j, N),
               cellAt(prevCell, i, j, N), cellAt(curCell, i, j, N), !s);
        if ((F(i, j, k + 1) < 0) != s)
          quad(cellAt(curCell, i - 1, j - 1, N), cellAt(curCell, i, j - 1, N),
               cellAt(curCell, i - 1, j, N), cellAt(curCell, i, j, N), s);
      }
    prevCell.swap(curCell);
  }
  return true;
}

// One batch frame, web semantics: carve and smooth, apply the
// cluster-free scene transform, write a GLB. An empty carve is not an
// error (the web build logs it and moves on to the next frame).
bool writeBatchGLB(const Params& P, const UiState& ui, int N,
                   const std::filesystem::path& file, const std::string& settingsText,
                   std::string& err) {
  std::vector<float> pos;
  std::vector<unsigned int> tris;
  if (!Carver::carve(P, ui, N, ui.smooth, false, pos, tris, err)) return false;
  if (tris.empty()) return true;
  std::vector<float> tp;
  std::vector<unsigned int> ti;
  sceneTransform(pos, tris, ui, false, tp, ti);
  return ExportJobs::writeGLB(file.string(), tp, ti, settingsText, err);
}

}  // namespace

bool ExportJobs::writeGLB(const std::string& path, const std::vector<float>& positions,
                          const std::vector<unsigned int>& indices,
                          const std::string& settingsText, std::string& err) {
  std::vector<float> nor;
  vertexNormals(positions, indices, nor);
  const uint32_t posBytes = (uint32_t)(positions.size() * sizeof(float));
  const uint32_t norBytes = (uint32_t)(nor.size() * sizeof(float));
  const uint32_t idxBytes = (uint32_t)(indices.size() * sizeof(uint32_t));
  const uint32_t binLen = posBytes + norBytes + idxBytes;
  float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
  if (!positions.empty()) {
    mn[0] = mx[0] = positions[0];
    mn[1] = mx[1] = positions[1];
    mn[2] = mx[2] = positions[2];
    for (size_t i = 0; i + 2 < positions.size(); i += 3)
      for (int a = 0; a < 3; a++) {
        if (positions[i + a] < mn[a]) mn[a] = positions[i + a];
        if (positions[i + a] > mx[a]) mx[a] = positions[i + a];
      }
  }
  std::string json;
  json += "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Fractal Explorer Advanced\",\"extras\":{\"fractalExplorer\":{\"version\":1,\"settings\":\"";
  json += jsonEscape(settingsText);
  json += "\"}}},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"name\":\"Fractal\"}],";
  json += "\"meshes\":[{\"name\":\"Fractal\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2,\"material\":0}]}],";
  json += "\"materials\":[{\"name\":\"Sculpt clay\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.78,0.8,0.83,1],\"metallicFactor\":0,\"roughnessFactor\":0.85},\"doubleSided\":true}],";
  json += "\"buffers\":[{\"byteLength\":" + std::to_string(binLen) + "}],";
  json += "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" + std::to_string(posBytes) +
          "},{\"buffer\":0,\"byteOffset\":" + std::to_string(posBytes) + ",\"byteLength\":" + std::to_string(norBytes) +
          "},{\"buffer\":0,\"byteOffset\":" + std::to_string(posBytes + norBytes) + ",\"byteLength\":" + std::to_string(idxBytes) + "}],";
  json += "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":" + std::to_string(positions.size() / 3) +
          ",\"type\":\"VEC3\",\"min\":[" + fmtFloat(mn[0]) + "," + fmtFloat(mn[1]) + "," + fmtFloat(mn[2]) +
          "],\"max\":[" + fmtFloat(mx[0]) + "," + fmtFloat(mx[1]) + "," + fmtFloat(mx[2]) +
          "]},{\"bufferView\":1,\"componentType\":5126,\"count\":" + std::to_string(nor.size() / 3) +
          ",\"type\":\"VEC3\"},{\"bufferView\":2,\"componentType\":5125,\"count\":" + std::to_string(indices.size()) +
          ",\"type\":\"SCALAR\"}]}";
  while (json.size() % 4) json.push_back(' ');
  std::ofstream out = openBinary(path, err);
  if (!out) return false;
  const uint32_t totalLen = 12 + 8 + (uint32_t)json.size() + 8 + binLen;
  writeU32(out, 0x46546C67);  // "glTF"
  writeU32(out, 2);
  writeU32(out, totalLen);
  writeU32(out, (uint32_t)json.size());
  writeU32(out, 0x4E4F534A);  // "JSON"
  out.write(json.data(), (std::streamsize)json.size());
  writeU32(out, binLen);
  writeU32(out, 0x004E4942);  // "BIN\0"
  if (!positions.empty()) out.write((const char*)positions.data(), posBytes);
  if (!nor.empty()) out.write((const char*)nor.data(), norBytes);
  if (!indices.empty()) out.write((const char*)indices.data(), idxBytes);
  out.flush();
  if (!out) {
    err = "Writing " + path + " failed partway through.";
    return false;
  }
  return true;
}

bool ExportJobs::writeSTL(const std::string& path, const std::vector<float>& positions,
                          const std::vector<unsigned int>& indices, std::string& err) {
  std::ofstream out = openBinary(path, err);
  if (!out) return false;
  char header[80] = {};
  out.write(header, 80);
  writeU32(out, (uint32_t)(indices.size() / 3));
  auto wf = [&](float v) { out.write((const char*)&v, 4); };
  for (size_t t = 0; t + 2 < indices.size(); t += 3) {
    size_t a = (size_t)indices[t] * 3, b = (size_t)indices[t + 1] * 3, c = (size_t)indices[t + 2] * 3;
    double ux = (double)positions[b] - positions[a], uy = (double)positions[b + 1] - positions[a + 1],
           uz = (double)positions[b + 2] - positions[a + 2];
    double vx = (double)positions[c] - positions[a], vy = (double)positions[c + 1] - positions[a + 1],
           vz = (double)positions[c + 2] - positions[a + 2];
    double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!(nl > 0)) nl = 1;
    wf((float)(nx / nl));
    wf((float)(ny / nl));
    wf((float)(nz / nl));
    for (size_t vi : {a, b, c}) {
      wf(positions[vi]);
      wf(positions[vi + 1]);
      wf(positions[vi + 2]);
    }
    uint16_t attr = 0;
    out.write((const char*)&attr, 2);
  }
  out.flush();
  if (!out) {
    err = "Writing " + path + " failed partway through.";
    return false;
  }
  return true;
}

bool Carver::carve(const Params& P, const UiState& ui, int N, bool smooth, bool applyScene,
                   std::vector<float>& positions, std::vector<unsigned int>& indices, std::string& err) {
  if (!carveMeshNative(P, N, positions, indices, err)) return false;
  if (smooth) smoothMesh(positions, indices, 2);
  if (applyScene) {
    std::vector<float> tp;
    std::vector<unsigned int> ti;
    sceneTransform(positions, indices, ui, true, tp, ti);
    positions.swap(tp);
    indices.swap(ti);
  }
  return true;
}

bool Carver::exportSingle(const Params& P, const UiState& ui, const std::string& settingsText,
                          bool glb, const std::string& path, std::string& err) {
  const int N = (int)jsRound(ui.exd);
  std::vector<float> pos;
  std::vector<unsigned int> tris;
  if (!carve(P, ui, N, ui.smooth, true, pos, tris, err)) return false;
  if (tris.empty()) {
    err = "Nothing to export: no surface in range. Ease off Bulge (pulled back), Shell wall, "
          "Onion layers, Prune, the orb cutters or Flatten base, then try again.";
    return false;
  }
  return glb ? ExportJobs::writeGLB(path, pos, tris, settingsText, err)
             : ExportJobs::writeSTL(path, pos, tris, err);
}

bool Carver::exportGrowth(const UiState& ui, float anim, const std::string& dir,
                          const std::string& settingsText, std::string& err) {
  Params P0 = buildParams(ui, anim, 0.0f);
  const int N = (int)jsRound(ui.exd);
  const std::filesystem::path d(dir);
  const double offs[5] = {-0.24, -0.12, 0.0, 0.12, 0.24};
  for (int i = 0; i < 5; i++) {
    Params P = P0;
    P.bulge = (float)std::max(-0.58, std::min(0.58, (double)P0.bulge + offs[i]));
    std::map<std::string, std::string> ov{{"bulge", std::to_string(jsRound((double)P.bulge * 100.0))}};
    std::string name = "fractal-explorer-advanced-growth-" + std::to_string(i + 1) + ".glb";
    if (!writeBatchGLB(P, ui, N, d / name, settingsTextWith(settingsText, ov), err)) return false;
  }
  return true;
}

bool Carver::exportMorphFrames(const UiState& ui, float anim, const std::string& dir,
                               const std::string& settingsText, std::string& err) {
  Params P0 = buildParams(ui, anim, 0.0f);
  const int N = (int)jsRound(ui.exd);
  const std::filesystem::path d(dir);
  const double steps[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
  for (int i = 0; i < 5; i++) {
    Params P = P0;
    P.morph = (float)steps[i];
    long long pct = jsRound(steps[i] * 100.0);
    std::map<std::string, std::string> ov{{"morph", std::to_string(pct)}};
    std::string name = "fractal-explorer-advanced-morph-" + std::to_string(pct) + ".glb";
    if (!writeBatchGLB(P, ui, N, d / name, settingsTextWith(settingsText, ov), err)) return false;
  }
  return true;
}

bool Carver::exportVariants(const UiState& ui, float anim, const std::string& dir,
                            const std::string& settingsText, std::string& err) {
  Params P0 = buildParams(ui, anim, 0.0f);
  const int N = (int)jsRound(ui.exd);
  const std::filesystem::path d(dir);
  const long long seed0 = jsRound(ui.seed);
  const double wob = std::max((double)P0.wobble, 0.45);
  for (int i = 0; i < 5; i++) {
    const long long seed = (seed0 + i * 137) % 1000;
    Params P = P0;
    P.wobble = (float)wob;
    fillBranchSeq((float)seed, P.branchSeq);
    std::map<std::string, std::string> ov{{"seed", std::to_string(seed)},
                                          {"wobble", std::to_string(jsRound(wob * 100.0))}};
    std::string name = "fractal-explorer-advanced-variant-" + std::to_string(i + 1) + ".glb";
    if (!writeBatchGLB(P, ui, N, d / name, settingsTextWith(settingsText, ov), err)) return false;
  }
  return true;
}
