// Reverend Ricky Rose's Dance Party Visuals: native CUDA edition.
// Full parity build: same controls, same settings files, same exports as
// the Electron build, with the raymarcher running as a CUDA kernel.

#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "params.h"
#include "render.h"
#include "audio.h"
#include "exports.h"

// ---------------- small helpers ----------------
static std::string wsToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
static std::wstring utf8ToWs(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
static std::string saveDialog(const wchar_t* title, const wchar_t* filter, const wchar_t* defName, const wchar_t* defExt) {
  wchar_t buf[MAX_PATH] = {};
  wcsncpy(buf, defName, MAX_PATH - 1);
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrTitle = title;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = defExt;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (!GetSaveFileNameW(&ofn)) return {};
  return wsToUtf8(buf);
}
static std::string openDialog(const wchar_t* title, const wchar_t* filter) {
  wchar_t buf[MAX_PATH] = {};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrTitle = title;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&ofn)) return {};
  return wsToUtf8(buf);
}
static std::string dirOf(const std::string& path) {
  size_t p = path.find_last_of("\\/");
  return p == std::string::npos ? std::string(".") : path.substr(0, p);
}

// ---------------- settings ----------------
struct Field { const char* id; char kind; void* ptr; }; // kind: f, i, b
static std::vector<Field> buildFields(UiState& u) {
  std::vector<Field> F;
  auto f = [&](const char* id, float* p) { F.push_back({id, 'f', p}); };
  auto i = [&](const char* id, int* p) { F.push_back({id, 'i', p}); };
  auto b = [&](const char* id, bool* p) { F.push_back({id, 'b', p}); };
  i("ftype", &u.ftype); i("pal", &u.pal); i("mtype", &u.mtype); i("csgsel", &u.csgsel);
  i("prunesel", &u.prunesel); i("patbsel", &u.patbsel); i("patbblend", &u.patbblend); i("ctype", &u.ctype);
  f("iter", &u.iter); f("fscale", &u.fscale); f("twist", &u.twist); f("spread", &u.spread);
  f("glow", &u.glow); f("lean", &u.lean); f("bulge", &u.bulge); f("squash", &u.squash);
  f("tumble", &u.tumble); f("morph", &u.morph); f("lang2", &u.lang2); f("fog", &u.fog);
  f("eglow", &u.eglow); f("band", &u.band); f("stripe", &u.stripe);
  f("wave", &u.wave); f("wavef", &u.wavef); f("pulse", &u.pulse); f("pulserate", &u.pulserate);
  f("flute", &u.flute); f("flutef", &u.flutef); f("smorph", &u.smorph); f("radial", &u.radial);
  f("noiseb", &u.noiseb); f("noises", &u.noises); f("gyroid", &u.gyroid); f("gyros", &u.gyros);
  f("worley", &u.worley); f("wors", &u.wors); f("shell", &u.shell); f("invert", &u.invert);
  f("csgsize", &u.csgsize); f("csgh", &u.csgh); f("repeat", &u.repeat); f("juliaw", &u.juliaw);
  f("branch", &u.branch); f("branchalt", &u.branchalt); f("taper", &u.taper); f("leader", &u.leader);
  f("wobble", &u.wobble); f("seed", &u.seed); f("pruneh", &u.pruneh); f("prunetight", &u.prunetight);
  f("voxel", &u.voxel); f("voxelgrid", &u.voxelgrid); f("patbscale", &u.patbscale); f("patbamt", &u.patbamt);
  f("groove", &u.groove); f("groovef", &u.groovef); f("panel", &u.panel); f("panels", &u.panels);
  f("helix", &u.helix); f("helixturns", &u.helixturns); f("onion", &u.onion); f("patcol", &u.patcol);
  f("morphy", &u.morphy); f("mgrad", &u.mgrad); f("arc", &u.arc); f("twistgrad", &u.twistgrad);
  b("spin", &u.spin); b("flow", &u.flow); b("animform", &u.animform); f("zoom", &u.zoom);
  f("exd", &u.exd); f("exscale", &u.exscale); f("copies", &u.copies); f("clustersp", &u.clustersp);
  f("trend", &u.trend); f("baseh", &u.baseh); b("smooth", &u.smooth); b("baseon", &u.baseon);
  i("quality", &u.quality);
  f("asens", &u.asens); f("asmooth", &u.asmooth);
  for (int t = 0; t < 8; t++) {
    static char sid[8][24], aid[8][24];
    snprintf(sid[t], 24, "asrc-%s", audioTargetId(t));
    snprintf(aid[t], 24, "aamt-%s", audioTargetId(t));
    F.push_back({sid[t], 'i', &u.asrc[t]});
    F.push_back({aid[t], 'f', &u.aamt[t]});
  }
  return F;
}

static std::string collectSettings(UiState& u) {
  std::ostringstream out;
  out << "# Fractal Explorer settings (Reverend Ricky Rosè's Dance Party Visuals)\n";
  out << "# Load this file in either Explorer to put every slider back.\n";
  char buf[64];
  for (auto& fd : buildFields(u)) {
    if (fd.kind == 'f') { snprintf(buf, 64, "%g", *(float*)fd.ptr); out << fd.id << "=" << buf << "\n"; }
    else if (fd.kind == 'i') { out << fd.id << "=" << *(int*)fd.ptr << "\n"; }
    else { out << fd.id << "=" << (*(bool*)fd.ptr ? "on" : "off") << "\n"; }
  }
  // string-valued selects, written the way the web build writes them
  static const char* pivots[3] = {"center", "ground", "top"};
  static const char* layouts[3] = {"row", "ring", "grid"};
  out << "pivot=" << pivots[std::clamp(u.pivot, 0, 2)] << "\n";
  out << "layout=" << layouts[std::clamp(u.layout, 0, 2)] << "\n";
  out << "resmode=" << (u.resmode == 0 ? std::string("auto") : std::to_string(u.resScale)) << "\n";
  return out.str();
}

static void applySettingsText(UiState& u, const std::string& text) {
  auto fields = buildFields(u);
  std::istringstream in(text);
  std::string line;
  auto setNamed = [&](const std::string& id, const std::string& val) {
    for (auto& fd : fields) {
      if (id != fd.id) continue;
      if (fd.kind == 'f') *(float*)fd.ptr = (float)atof(val.c_str());
      else if (fd.kind == 'i') *(int*)fd.ptr = atoi(val.c_str());
      else *(bool*)fd.ptr = (val == "on" || val == "1" || val == "true");
      return;
    }
    // string selects from the web build
    if (id == "pivot") u.pivot = val == "ground" ? 1 : val == "top" ? 2 : 0;
    else if (id == "layout") u.layout = val == "ring" ? 1 : val == "grid" ? 2 : 0;
    else if (id == "resmode") {
      if (val == "auto") { u.resmode = 0; u.resScale = 1.0f; }
      else { u.resmode = 1; u.resScale = (float)atof(val.c_str()); }
    }
  };
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    setNamed(line.substr(0, eq), line.substr(eq + 1));
  }
}

static bool extractGlbRecipe(const std::string& path, std::string& settingsOut) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  size_t k = data.find("\"settings\"");
  if (k == std::string::npos) return false;
  k = data.find('"', data.find(':', k));
  if (k == std::string::npos) return false;
  std::string out;
  for (size_t i = k + 1; i < data.size(); i++) {
    char c = data[i];
    if (c == '\\' && i + 1 < data.size()) {
      char n = data[++i];
      if (n == 'n') out += '\n';
      else if (n == 't') out += '\t';
      else out += n;
    } else if (c == '"') break;
    else out += c;
  }
  settingsOut = out;
  return true;
}

// ---------------- app state ----------------
struct App {
  UiState ui;
  Renderer renderer;
  AudioEngine audio;
  Camera cam;
  float theta = 0.7f, phi = 1.05f;
  float animT = 0.0f, phase = 0.0f;
  float qualityScale = 0.75f;
  double fpsAccum = 0.0;
  int fpsFrames = 0;
  float fpsShown = 0.0f;
  std::string info = "Drag to orbit. Scroll to zoom. Press F for full screen.";
  float audioBase[8] = {2.0f, 0, 0, 0, 1.2f, 0, 0, 0};
  std::vector<std::string> micDevices;
  int micIndex = -1;
  bool fullscreen = false;
  int winX = 100, winY = 100, winW = 1420, winH = 960;
};

static Camera makeCamera(const App& a) {
  Camera c;
  float sp = sinf(a.phi), cp = cosf(a.phi);
  float tx = 0.0f, ty = 0.05f, tz = 0.0f;
  float px = tx + a.ui.zoom * sp * sinf(a.theta);
  float py = ty + a.ui.zoom * cp;
  float pz = tz + a.ui.zoom * sp * cosf(a.theta);
  float fx = tx - px, fy = ty - py, fz = tz - pz;
  float fl = sqrtf(fx * fx + fy * fy + fz * fz);
  if (fl < 1e-9f) fl = 1.0f;
  fx /= fl; fy /= fl; fz /= fl;
  // right = normalize(cross(fwd, up0)), up = cross(right, fwd)
  float rx = fy * 0.0f - fz * 1.0f, ry = fz * 0.0f - fx * 0.0f, rz = fx * 1.0f - fy * 0.0f;
  float rl = sqrtf(rx * rx + ry * ry + rz * rz);
  if (rl < 1e-9f) rl = 1.0f;
  rx /= rl; ry /= rl; rz /= rl;
  float ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;
  c.camPos[0] = px; c.camPos[1] = py; c.camPos[2] = pz;
  c.camFwd[0] = fx; c.camFwd[1] = fy; c.camFwd[2] = fz;
  c.camRight[0] = rx; c.camRight[1] = ry; c.camRight[2] = rz;
  c.camUp[0] = ux; c.camUp[1] = uy; c.camUp[2] = uz;
  return c;
}

// ---------------- UI helpers ----------------
static bool SFloat(const char* label, float* v, float mn, float mx, const char* fmt = "%.2f") {
  ImGui::TextUnformatted(label);
  ImGui::SameLine(170);
  ImGui::SetNextItemWidth(-1);
  std::string id = std::string("##") + label;
  return ImGui::SliderFloat(id.c_str(), v, mn, mx, fmt);
}
static bool SInt(const char* label, float* v, float mn, float mx) {
  int iv = (int)llround(*v);
  ImGui::TextUnformatted(label);
  ImGui::SameLine(170);
  ImGui::SetNextItemWidth(-1);
  std::string id = std::string("##") + label;
  if (ImGui::SliderInt(id.c_str(), &iv, (int)mn, (int)mx)) { *v = (float)iv; return true; }
  return false;
}
static bool SCombo(const char* label, int* v, const char* const* items, int count) {
  ImGui::TextUnformatted(label);
  ImGui::SameLine(170);
  ImGui::SetNextItemWidth(-1);
  std::string id = std::string("##") + label;
  return ImGui::Combo(id.c_str(), v, items, count);
}
static void Section(const char* title) {
  ImGui::Separator();
  ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.84f, 1.0f), "%s", title);
}

static const char* kFractalTypes[6] = {"Sierpinski pyramid", "Crystal octahedron", "Cube lattice", "Icosahedron crystal", "Mandelbulb", "Julia slice"};
static const char* kPalettes[4] = {"Rainbow depth", "Ember", "Glacier", "Ultraviolet"};
static const char* kCsg[5] = {"Off", "Add orb", "Carve orb", "Intersect orb", "Slice level"};
static const char* kPrune[4] = {"Off", "Cone canopy", "Height cap", "Core column"};
static const char* kPatB[4] = {"Off", "Gyroid sheet", "Worley cells", "Ring stack"};
static const char* kPatBlend[3] = {"Grow onto form", "Cut into form", "Only where both agree"};
static const char* kPivot[3] = {"Center", "Ground (stands on floor)", "Top"};
static const char* kLayout[3] = {"Row", "Ring", "Grid"};
static const char* kQuality[3] = {"Cinema", "Balanced", "Turbo (fast GPU)"};
static const char* kBands[6] = {"Off", "Bass", "Mids", "Treble", "Volume", "Beat"};

static void drawAudioPanel(App& a);
static void drawExportPanel(App& a);

static void drawControls(App& a) {
  UiState& u = a.ui;
  ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_None);
  ImGui::TextWrapped("%s", a.info.c_str());
  ImGui::Separator();
  ImGui::Checkbox("Slowly rotate", &u.spin);
  ImGui::SameLine(); ImGui::Checkbox("Color flow", &u.flow);
  ImGui::SameLine(); ImGui::Checkbox("Living form", &u.animform);
  if (ImGui::Button("Reset view")) { a.theta = 0.7f; a.phi = 1.05f; u.zoom = 3.6f; }
  ImGui::SameLine();
  if (ImGui::Button(a.fullscreen ? "Exit full screen" : "Full screen")) { /* handled in main loop via flag */ a.fullscreen = !a.fullscreen; }
  ImGui::SameLine();
  SCombo("Quality", &u.quality, kQuality, 3);

  Section("FRACTAL");
  SCombo("Fractal", &u.ftype, kFractalTypes, 6);
  SCombo("Coloring", &u.pal, kPalettes, 4);
  SInt("Depth (iterations)", &u.iter, 2, 20);
  SFloat("Scale", &u.fscale, 1.55f, 2.45f);
  SFloat("Twist per level", &u.twist, -35, 35, "%.1f");
  SFloat("Color spread", &u.spread, 0.2f, 3.0f);
  SFloat("Rim glow", &u.glow, 0, 100, "%.0f");

  Section("FORM");
  SFloat("Lean", &u.lean, -100, 100, "%.0f");
  SFloat("Bulge", &u.bulge, -60, 60, "%.0f");
  SFloat("Squash", &u.squash, 45, 220, "%.0f");
  SFloat("Tumble", &u.tumble, -35, 35, "%.1f");
  SCombo("Morph into", &u.mtype, kFractalTypes, 6);
  SFloat("Morph", &u.morph, 0, 100, "%.0f");
  SFloat("Light angle", &u.lang2, 0, 360, "%.0f");
  SFloat("Fog depth", &u.fog, 0, 100, "%.0f");
  SFloat("Edge glow", &u.eglow, 0, 100, "%.0f");
  SFloat("Color by level", &u.band, 0, 100, "%.0f");
  SFloat("Stripe", &u.stripe, 0, 100, "%.0f");

  Section("FORM LAB: SINE AND SPACE");
  SFloat("Wave warp", &u.wave, 0, 100, "%.0f");
  SFloat("Wave frequency", &u.wavef, 0.5f, 12, "%.1f");
  SFloat("Level pulse", &u.pulse, 0, 100, "%.0f");
  SFloat("Pulse rate", &u.pulserate, 0.3f, 3, "%.1f");
  SFloat("Sine fluting", &u.flute, 0, 60, "%.0f");
  SFloat("Flute frequency", &u.flutef, 2, 40, "%.1f");
  SFloat("Spatial morph", &u.smorph, 0, 100, "%.0f");
  SInt("Radial symmetry", &u.radial, 1, 16);

  Section("FORM LAB: FIELDS AND FLESH");
  SFloat("Noise bend", &u.noiseb, 0, 90, "%.0f");
  SFloat("Noise scale", &u.noises, 0.5f, 5, "%.1f");
  SFloat("Gyroid lattice", &u.gyroid, 0, 100, "%.0f");
  SFloat("Gyroid scale", &u.gyros, 1, 10, "%.1f");
  SFloat("Worley carve", &u.worley, 0, 90, "%.0f");
  SFloat("Worley scale", &u.wors, 0.8f, 6, "%.1f");
  SFloat("Shell wall", &u.shell, 0, 45, "%.0f");
  SFloat("Sphere inversion", &u.invert, 0, 100, "%.0f");
  SCombo("Sculpt", &u.csgsel, kCsg, 5);
  SFloat("Orb size", &u.csgsize, 0.2f, 2.4f);
  SFloat("Orb height", &u.csgh, -1.5f, 1.5f);
  SFloat("Repeat spacing", &u.repeat, 0, 8);
  SFloat("4D slice (Julia)", &u.juliaw, -100, 100, "%.0f");

  Section("ADVANCED BRANCHING");
  SFloat("Branch angle", &u.branch, -45, 45, "%.1f");
  SFloat("Branch alternation", &u.branchalt, 0, 100, "%.0f");
  SFloat("Taper", &u.taper, -80, 100, "%.0f");
  SFloat("Leader branch", &u.leader, 0, 100, "%.0f");
  SFloat("Seeded wobble", &u.wobble, 0, 100, "%.0f");
  ImGui::TextUnformatted("Seed");
  ImGui::SameLine(170); ImGui::SetNextItemWidth(-90);
  ImGui::SliderFloat("##seed", &u.seed, 0, 999, "%.0f");
  ImGui::SameLine();
  if (ImGui::Button("New seed")) {
    u.seed = (float)(rand() % 1000);
    a.info = "New seed " + std::to_string((int)u.seed) + ". The same seed always grows the same skeleton.";
  }
  SCombo("Pruning", &u.prunesel, kPrune, 4);
  SFloat("Prune height", &u.pruneh, -2, 2);
  SFloat("Prune tightness", &u.prunetight, 0, 100, "%.0f");

  Section("GEOMETRIC OVERLAYS");
  SFloat("Voxel snap", &u.voxel, 0, 100, "%.0f");
  SInt("Voxel grid", &u.voxelgrid, 4, 36);
  SCombo("Pattern B", &u.patbsel, kPatB, 4);
  SCombo("Pattern B blend", &u.patbblend, kPatBlend, 3);
  SFloat("Pattern B scale", &u.patbscale, 0.8f, 10);
  SFloat("Pattern B amount", &u.patbamt, 0, 100, "%.0f");
  SFloat("Groove rings", &u.groove, 0, 100, "%.0f");
  SFloat("Groove frequency", &u.groovef, 2, 48, "%.0f");
  SFloat("Panel grooves", &u.panel, 0, 100, "%.0f");
  SFloat("Panel scale", &u.panels, 1, 12, "%.1f");
  SFloat("Helix rails", &u.helix, 0, 100, "%.0f");
  SFloat("Helix turns", &u.helixturns, 1, 12, "%.1f");
  SInt("Onion layers", &u.onion, 0, 6);
  SFloat("Pattern color", &u.patcol, 0, 100, "%.0f");

  Section("MORPHING");
  SCombo("Third form", &u.ctype, kFractalTypes, 6);
  SFloat("Morph to third", &u.morphy, 0, 100, "%.0f");
  SFloat("Morph height gradient", &u.mgrad, -100, 100, "%.0f");
  SFloat("Arc bend", &u.arc, -100, 100, "%.0f");
  SFloat("Twist gradient", &u.twistgrad, -180, 180, "%.0f");
  drawAudioPanel(a);
  drawExportPanel(a);
  ImGui::End();
}

static void drawAudioPanel(App& a) {
  UiState& u = a.ui;
  Section("AUDIO REACTIVE (MICROPHONE)");
  if (!a.audio.running()) {
    if (ImGui::Button("Start microphone")) {
      if (a.audio.start(a.micIndex)) {
        for (int t = 0; t < 8; t++) a.audioBase[t] = *audioTargetSlot(u, t);
        a.info = "Microphone is listening. Play something.";
      }
      else a.info = "The microphone did not open. " + a.audio.lastError();
    }
  } else {
    if (ImGui::Button("Stop microphone")) {
      a.audio.stop();
      // restore hand-set positions, like the web build
      for (int t = 0; t < 8; t++) *audioTargetSlot(u, t) = a.audioBase[t];
      a.info = "Microphone stopped.";
    }
    ImGui::SameLine(); ImGui::TextUnformatted("listening");
  }
  if (ImGui::Button("Refresh inputs")) {
    a.micDevices = a.audio.deviceNames();
    a.info = "Found " + std::to_string(a.micDevices.size()) + " microphone inputs.";
  }
  if (!a.micDevices.empty()) {
    std::vector<const char*> names;
    names.push_back("Default microphone");
    for (auto& d : a.micDevices) names.push_back(d.c_str());
    int sel = a.micIndex + 1;
    ImGui::TextUnformatted("Input");
    ImGui::SameLine(170); ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##micdev", &sel, names.data(), (int)names.size())) {
      a.micIndex = sel - 1;
      if (a.audio.running()) a.audio.start(a.micIndex);
    }
  }
  SFloat("Sensitivity", &u.asens, 50, 300, "%.0f");
  SFloat("Smoothing", &u.asmooth, 0, 95, "%.0f");
  const AudioLevels& L = a.audio.levels();
  const char* labels[4] = {"BASS", "MID", "HIGH", "VOL"};
  float vals[4] = {L.bass, L.mid, L.high, L.vol};
  for (int i = 0; i < 4; i++) {
    ImGui::TextUnformatted(labels[i]);
    ImGui::SameLine(60);
    ImGui::ProgressBar(vals[i], ImVec2(-1, 0), "");
  }
  ImGui::Text("Beat %s", L.beat > 0.5f ? "*" : " ");
  ImGui::TextWrapped("Each mapping pushes its slider around the position you set, so your hand stays the center and the music dances around it.");
  for (int t = 0; t < 8; t++) {
    ImGui::PushID(t);
    ImGui::TextUnformatted(audioTargetName(t));
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##src", &u.asrc[t], kBands, 6);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-40);
    if (ImGui::SliderFloat("##amt", &u.aamt[t], 0, 100, "%.0f")) {}
    ImGui::SameLine(); ImGui::Text("%%");
    ImGui::PopID();
  }
}

static void drawExportPanel(App& a) {
  UiState& u = a.ui;
  Section("SCENE EXPORT");
  const char* resItems[4] = {"Auto (smooth)", "Crisp 1.5x", "Sharp 2x", "Ultra 3x"};
  int resSel = u.resmode == 0 ? 0 : (u.resScale >= 2.5f ? 3 : u.resScale >= 1.75f ? 2 : 1);
  ImGui::TextUnformatted("Resolution");
  ImGui::SameLine(170); ImGui::SetNextItemWidth(-1);
  if (ImGui::Combo("##resmode", &resSel, resItems, 4)) {
    if (resSel == 0) { u.resmode = 0; u.resScale = 1.0f; }
    else { u.resmode = 1; u.resScale = resSel == 1 ? 1.5f : resSel == 2 ? 2.0f : 3.0f; }
  }
  SInt("Export detail", &u.exd, 32, 224);
  ImGui::Checkbox("Smooth mesh (Taubin)", &u.smooth);
  SFloat("Export scale", &u.exscale, 0.25f, 3.0f);
  SCombo("Pivot", &u.pivot, kPivot, 3);
  SInt("Cluster copies", &u.copies, 1, 12);
  SCombo("Cluster layout", &u.layout, kLayout, 3);
  SFloat("Cluster spacing", &u.clustersp, 0.6f, 3.0f);
  SFloat("Size trend", &u.trend, -80, 120, "%.0f");
  ImGui::Checkbox("Cut a flat standing base (export only)", &u.baseon);
  SFloat("Base height", &u.baseh, -2, 1.5f);

  std::string err;
  auto params = [&]() { return buildParams(u, a.animT, a.phase); };
  if (ImGui::Button("Export GLB (for Nomad)")) {
    std::string path = saveDialog(L"Save GLB", L"GLB files\0*.glb\0", L"fractal.glb", L"glb");
    if (!path.empty()) {
      if (Carver::exportSingle(params(), u, collectSettings(u), true, path, err)) a.info = "GLB saved: " + path;
      else a.info = "Export failed. " + err;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Export STL (3D print)")) {
    std::string path = saveDialog(L"Save STL", L"STL files\0*.stl\0", L"fractal.stl", L"stl");
    if (!path.empty()) {
      if (Carver::exportSingle(params(), u, collectSettings(u), false, path, err)) a.info = "STL saved: " + path;
      else a.info = "Export failed. " + err;
    }
  }
  if (ImGui::Button("Export growth series")) {
    std::string path = saveDialog(L"Save growth series (folder pick)", L"GLB files\0*.glb\0", L"growth-1.glb", L"glb");
    if (!path.empty()) {
      if (Carver::exportGrowth(u, a.animT, dirOf(path), collectSettings(u), err)) a.info = "Growth series saved in " + dirOf(path);
      else a.info = "Export failed. " + err;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Export morph frames")) {
    std::string path = saveDialog(L"Save morph frames (folder pick)", L"GLB files\0*.glb\0", L"morph-0.glb", L"glb");
    if (!path.empty()) {
      if (Carver::exportMorphFrames(u, a.animT, dirOf(path), collectSettings(u), err)) a.info = "Morph frames saved in " + dirOf(path);
      else a.info = "Export failed. " + err;
    }
  }
  if (ImGui::Button("Export 5 variants")) {
    std::string path = saveDialog(L"Save variants (folder pick)", L"GLB files\0*.glb\0", L"variant-1.glb", L"glb");
    if (!path.empty()) {
      if (Carver::exportVariants(u, a.animT, dirOf(path), collectSettings(u), err)) a.info = "Variants saved in " + dirOf(path);
      else a.info = "Export failed. " + err;
    }
  }
  ImGui::Separator();
  if (ImGui::Button("Save settings")) {
    std::string path = saveDialog(L"Save settings", L"Text files\0*.txt\0", L"fractal-settings.txt", L"txt");
    if (!path.empty()) {
      std::ofstream f(path, std::ios::binary);
      f << collectSettings(u);
      a.info = "Settings saved: " + path;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Load settings")) {
    std::string path = openDialog(L"Load settings", L"Settings or GLB\0*.txt;*.glb\0");
    if (!path.empty()) {
      if (path.size() > 4 && path.substr(path.size() - 4) == ".glb") {
        std::string recipe;
        if (extractGlbRecipe(path, recipe)) { applySettingsText(u, recipe); a.info = "Settings loaded from GLB recipe."; }
        else a.info = "That GLB has no recipe in it.";
      } else {
        std::ifstream f(path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        applySettingsText(u, text);
        a.info = "Settings loaded: " + path;
      }
      for (int t = 0; t < 8; t++) a.audioBase[t] = *audioTargetSlot(u, t);
    }
  }
}

// ---------------- entry ----------------
static void glfwError(int code, const char* desc) {
  fprintf(stderr, "GLFW error %d: %s\n", code, desc ? desc : "");
}

int main(int argc, char** argv) {
  std::string snapshotPath, settingsPath;
  int snapW = 960, snapH = 540;
  bool carveTest = false;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--snapshot" && i + 1 < argc) snapshotPath = argv[++i];
    else if (arg == "--settings" && i + 1 < argc) settingsPath = argv[++i];
    else if (arg == "--carve-test") carveTest = true;
  }

  glfwSetErrorCallback(glfwError);
  if (!glfwInit()) { fprintf(stderr, "GLFW init failed.\n"); return 1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  bool headless = !snapshotPath.empty() || carveTest;
  if (headless) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* window = glfwCreateWindow(1420, 960, "Reverend Ricky Rosè's Dance Party Visuals", nullptr, nullptr);
  if (!window) { fprintf(stderr, "Window creation failed.\n"); return 1; }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  if (!loadGlProcs()) { fprintf(stderr, "OpenGL buffer functions unavailable.\n"); return 1; }

  App a;
  for (int t = 0; t < 8; t++) a.audioBase[t] = *audioTargetSlot(a.ui, t);
  std::string err;
  if (!a.renderer.init(err)) {
    fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  a.audio.init();
  if (!settingsPath.empty()) {
    std::ifstream f(settingsPath, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    applySettingsText(a.ui, text);
  }

  if (carveTest) {
    Params P = buildParams(a.ui, 0.0f, 0.0f);
    std::vector<float> pos;
    std::vector<unsigned int> idx;
    if (Carver::carve(P, a.ui, 48, false, false, pos, idx, err)) {
      printf("carve-test: %zu triangles at detail 48\n", idx.size() / 3);
      return idx.empty() ? 2 : 0;
    }
    fprintf(stderr, "carve-test failed: %s\n", err.c_str());
    return 1;
  }
  if (!snapshotPath.empty()) {
    Params P = buildParams(a.ui, 0.0f, 0.0f);
    P.detail = 1.0f;
    P.step = computeStepBase(P);
    Camera cam = makeCamera(a);
    bool ok = a.renderer.renderSnapshot(P, cam, snapW, snapH, snapshotPath, err);
    printf(ok ? "snapshot written: %s\n" : "snapshot failed: %s\n", ok ? snapshotPath.c_str() : err.c_str());
    return ok ? 0 : 1;
  }

  // ImGui setup
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.FrameRounding = 6.0f;
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");
  srand((unsigned)time(nullptr));
  a.info = "Running on " + a.renderer.deviceName() + ". Drag to orbit. Press F for full screen.";

  QualityMode qm = qualityMode(a.ui.quality);
  a.qualityScale = qm.qMax;
  auto prev = std::chrono::steady_clock::now();
  bool prevFullscreen = false, prevF = false, dragging = false;
  double lastX = 0, lastY = 0;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    auto now = std::chrono::steady_clock::now();
    float dt = std::min(0.05f, std::chrono::duration<float>(now - prev).count());
    prev = now;

    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW < 1 || fbH < 1) continue;

    // clocks, same rules as the web loop
    if (a.ui.spin && !dragging) a.theta += dt * 0.25f;
    if (a.ui.flow) { a.phase = fmodf(a.phase + dt * 0.08f, 1.0f); }
    if (a.ui.animform) a.animT += dt;

    // audio drive (before UI so the sliders show the music)
    if (a.audio.running()) {
      a.audio.poll(dt, a.ui.asens / 100.0f, a.ui.asmooth / 100.0f);
      const AudioLevels& L = a.audio.levels();
      float sig[6] = {0, L.bass, L.mid, L.high, L.vol, L.beat};
      for (int t = 0; t < 8; t++) {
        int src = a.ui.asrc[t];
        if (src < 1 || src > 5) continue;
        float mn, mx;
        audioTargetRange(t, mn, mx);
        float amt = a.ui.aamt[t] / 100.0f;
        float eff = a.audioBase[t] + (sig[src] - 0.18f) * amt * (mx - mn) * 0.5f;
        *audioTargetSlot(a.ui, t) = std::clamp(eff, mn, mx);
      }
    }

    // quality bookkeeping
    qm = qualityMode(a.ui.quality);
    a.qualityScale = std::clamp(a.qualityScale, qm.qMin, qm.qMax);
    float eff = a.ui.resmode == 0 ? a.qualityScale : a.ui.resScale;
    int iw = std::max(64, (int)llround(fbW * eff));
    int ih = std::max(64, (int)llround(fbH * eff));

    Params P = buildParams(a.ui, a.animT, a.phase);
    P.step = computeStepBase(P) * qm.stepMul;
    P.detail = eff;
    Camera cam = makeCamera(a);
    if (a.renderer.resize(iw, ih, err) && !a.renderer.renderFrame(P, cam, err)) {
      a.info = err;
    }

    // fps adaptation, the web rules
    a.fpsAccum += dt;
    a.fpsFrames++;
    if (a.fpsAccum > 2.5) {
      float fps = (float)(a.fpsFrames / a.fpsAccum);
      a.fpsShown = fps;
      a.fpsAccum = 0; a.fpsFrames = 0;
      if (a.ui.resmode == 0) {
        if (fps < 42 && a.qualityScale > qm.qMin) a.qualityScale = std::max(qm.qMin, a.qualityScale - 0.15f);
        else if (fps > 57 && a.qualityScale < qm.qMax) a.qualityScale = std::min(qm.qMax, a.qualityScale + 0.1f);
      }
    }

    // ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddImage((ImTextureID)(intptr_t)a.renderer.textureId(), ImVec2(0, 0), ImVec2((float)fbW, (float)fbH));

    // orbit input on the image itself
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.WantCaptureMouse) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);
      if (dragging) {
        a.theta += (float)(mx - lastX) * 0.005f;
        a.phi = std::clamp(a.phi + (float)(my - lastY) * 0.005f, 0.05f, 3.09f);
      }
      dragging = true;
      lastX = mx; lastY = my;
    } else dragging = false;
    if (!io.WantCaptureMouse && io.MouseWheel != 0.0f) {
      a.ui.zoom = std::clamp(a.ui.zoom * (1.0f - io.MouseWheel * 0.08f), 1.6f, 8.0f);
    }

    if (!a.fullscreen) {
      char title[160];
      snprintf(title, 160, "Reverend Ricky Rosè's Dance Party Visuals  |  %s  |  %.0f fps", a.renderer.deviceName().c_str(), a.fpsShown);
      glfwSetWindowTitle(window, title);
      drawControls(a);
    }

    ImGui::Render();
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);

    // fullscreen transitions (button flag or F key; Esc leaves)
    bool fNow = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
    if (fNow && !prevF && !io.WantCaptureKeyboard) a.fullscreen = !a.fullscreen;
    prevF = fNow;
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && a.fullscreen) a.fullscreen = false;
    if (a.fullscreen != prevFullscreen) {
      GLFWmonitor* mon = glfwGetPrimaryMonitor();
      const GLFWvidmode* mode = glfwGetVideoMode(mon);
      if (a.fullscreen) {
        glfwGetWindowPos(window, &a.winX, &a.winY);
        glfwGetWindowSize(window, &a.winW, &a.winH);
        glfwSetWindowMonitor(window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
      } else {
        glfwSetWindowMonitor(window, nullptr, a.winX, a.winY, a.winW, a.winH, 0);
      }
      prevFullscreen = a.fullscreen;
    }
  }

  a.audio.stop();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  a.renderer.shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
