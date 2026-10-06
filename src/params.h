#pragma once
// Shared parameter contract for the native CUDA build of
// Reverend Ricky Rose's Dance Party Visuals.
//
// Params holds shader-ready values, mirroring the web build's uniforms
// one to one (angles already in radians, percents already divided).
// UiState holds the raw slider positions exactly as the web UI does,
// so settings files (id=value lines) mean the same thing in both builds.

#include <cstdint>
#include <cstring>
#include <cmath>

struct Params {
  int type = 0;
  int iter = 10;
  float scale = 2.0f;
  float twist = 0.0f;        // radians
  float spread = 1.2f;
  float glowAmt = 0.4f;
  float phase = 0.0f;
  int pal = 0;
  int typeB = 2;
  float lean = 0.0f;
  float bulge = 0.0f;
  float squash = 1.0f;
  float tumble = 0.0f;       // radians
  float morph = 0.0f;
  float detail = 1.0f;
  float edgeGlow = 0.0f;
  float lightAng = 0.6981317f; // 40 degrees
  float fog = 0.55f;
  float band = 0.0f;
  float stripe = 0.0f;
  float wave = 0.0f;
  float waveF = 3.0f;
  float pulse = 0.0f;
  float pulseRate = 1.2f;
  float anim = 0.0f;
  float flute = 0.0f;
  float fluteF = 14.0f;
  float noiseB = 0.0f;
  float noiseS = 1.6f;
  float gyroid = 0.0f;
  float gyroS = 4.0f;
  float worley = 0.0f;
  float worlS = 2.4f;
  float shell = 0.0f;
  float radial = 1.0f;
  float repeat = 0.0f;
  float invert = 0.0f;
  float smorph = 0.0f;
  float csg = 0.0f;
  float csgSize = 1.1f;
  float csgY = 0.0f;
  float juliaW = 0.0f;
  float step = 0.9f;
  float branch = 0.0f;       // radians
  float branchAlt = 0.0f;
  float taper = 0.0f;
  float leader = 0.0f;
  float wobble = 0.0f;
  float branchSeq[24][4] = {};
  float pruneMode = 0.0f;
  float pruneH = 0.0f;
  float pruneTight = 0.45f;
  float voxel = 0.0f;
  float voxelGrid = 16.0f;
  float patB = 0.0f;
  float patBScale = 3.0f;
  float patBAmt = 0.7f;
  float patBBlend = 0.0f;
  float groove = 0.0f;
  float grooveF = 18.0f;
  float panel = 0.0f;
  float panelS = 4.0f;
  float helix = 0.0f;
  float helixTurns = 4.0f;
  float onion = 0.0f;
  float patColor = 0.0f;
  int typeC = 1;
  float morphY = 0.0f;
  float morphGrad = 0.0f;
  float arc = 0.0f;
  float twistGrad = 0.0f;    // radians
  // Export-only (the JS carver applies these; the render shader does not):
  bool baseOn = false;
  float baseH = -1.2f;
};

// Deterministic branch sequence, the exact algorithm the web build uses
// (xorshift on a hashed seed), so a seed grows the same skeleton in both.
inline void fillBranchSeq(float seed, float out[24][4]) {
  uint32_t s = (uint32_t)((uint64_t)(uint32_t)llround(seed) * 2654435761ULL);
  if (!s) s = 123456789u;
  auto rnd = [&]() {
    s ^= (s << 13);
    s ^= (s >> 17);
    s ^= (s << 5);
    return (float)((s / 4294967296.0) * 2.0 - 1.0);
  };
  for (int i = 0; i < 24; i++)
    for (int j = 0; j < 4; j++) out[i][j] = rnd();
}

// Raw UI state: same ids, ranges, and defaults as the web controls.
struct UiState {
  // selects
  int ftype = 0, pal = 0, mtype = 2, csgsel = 0, prunesel = 0, patbsel = 0, patbblend = 0, ctype = 1;
  // sliders (web values)
  float iter = 10, fscale = 2.0f, twist = 0, spread = 1.2f, glow = 40, lean = 0, bulge = 0,
        squash = 100, tumble = 0, morph = 0, lang2 = 40, fog = 55, eglow = 0, band = 0, stripe = 0,
        wave = 0, wavef = 3.0f, pulse = 0, pulserate = 1.2f, flute = 0, flutef = 14, smorph = 0,
        radial = 1, noiseb = 0, noises = 1.6f, gyroid = 0, gyros = 4.0f, worley = 0, wors = 2.4f,
        shell = 0, invert = 0, csgsize = 1.1f, csgh = 0, repeat = 0, juliaw = 0,
        branch = 0, branchalt = 0, taper = 0, leader = 0, wobble = 0, seed = 7,
        pruneh = 0, prunetight = 45, voxel = 0, voxelgrid = 16, patbscale = 3.0f, patbamt = 70,
        groove = 0, groovef = 18, panel = 0, panels = 4.0f, helix = 0, helixturns = 4.0f,
        onion = 0, patcol = 0, morphy = 0, mgrad = 0, arc = 0, twistgrad = 0;
  // view + transport checkboxes
  bool spin = true, flow = false, animform = false;
  float zoom = 3.6f;
  // export controls
  float exd = 128, exscale = 1.0f, copies = 1, clustersp = 1.15f, trend = 0, baseh = -1.2f;
  int pivot = 0;   // 0 center, 1 ground, 2 top (select order in web: center/ground/top)
  int layout = 0;  // 0 row, 1 ring, 2 grid
  bool smooth = true, baseon = false;
  int resmode = 0; // 0 = auto, else manual multiplier stored in resScale
  float resScale = 1.0f;
  int quality = 2; // 0 cinema, 1 balanced, 2 turbo (web v1.1.0 order/default)
  // audio reactive
  float asens = 140, asmooth = 55;
  int asrc[8] = {1, 2, 1, 4, 3, 5, 2, 5};      // per target: 0 off 1 bass 2 mid 3 high 4 vol 5 beat
  float aamt[8] = {55, 45, 45, 40, 50, 70, 30, 50};
};

// The eight audio-reactive slider targets, in the web build's order.
inline const char* audioTargetId(int i) {
  static const char* ids[8] = {"fscale", "twist", "bulge", "morph", "spread", "eglow", "wave", "pulse"};
  return ids[i];
}
inline const char* audioTargetName(int i) {
  static const char* names[8] = {"Scale", "Twist per level", "Bulge", "Morph", "Color spread", "Edge glow", "Wave warp", "Level pulse"};
  return names[i];
}
// Pointer to the UiState slider an audio target drives.
inline float* audioTargetSlot(UiState& u, int i) {
  switch (i) {
    case 0: return &u.fscale;
    case 1: return &u.twist;
    case 2: return &u.bulge;
    case 3: return &u.morph;
    case 4: return &u.spread;
    case 5: return &u.eglow;
    case 6: return &u.wave;
    default: return &u.pulse;
  }
}
// Slider ranges for the audio targets (web min/max/step), for clamping.
inline void audioTargetRange(int i, float& mn, float& mx) {
  switch (i) {
    case 0: mn = 1.55f; mx = 2.45f; break;
    case 1: mn = -35; mx = 35; break;
    case 2: mn = -60; mx = 60; break;
    case 3: mn = 0; mx = 100; break;
    case 4: mn = 0.2f; mx = 3.0f; break;
    case 5: mn = 0; mx = 100; break;
    case 6: mn = 0; mx = 100; break;
    default: mn = 0; mx = 100; break;
  }
}

constexpr float kDeg2Rad = 0.01745329252f;

// Convert UI values to shader-ready Params. anim and phase come from the
// app's clocks (Living form and Color flow), exactly as the web loop does.
inline Params buildParams(const UiState& u, float anim, float phase) {
  Params P;
  P.type = u.ftype; P.pal = u.pal; P.typeB = u.mtype; P.typeC = u.ctype;
  P.iter = (int)llround(u.iter);
  P.scale = u.fscale;
  P.twist = u.twist * kDeg2Rad;
  P.spread = u.spread;
  P.glowAmt = u.glow / 100.0f;
  P.phase = phase;
  P.lean = u.lean / 100.0f;
  P.bulge = u.bulge / 100.0f;
  P.squash = u.squash / 100.0f;
  P.tumble = u.tumble * kDeg2Rad;
  P.morph = u.morph / 100.0f;
  P.lightAng = u.lang2 * kDeg2Rad;
  P.fog = u.fog / 100.0f;
  P.edgeGlow = u.eglow / 100.0f;
  P.band = u.band / 100.0f;
  P.stripe = u.stripe / 100.0f;
  P.wave = u.wave / 100.0f;
  P.waveF = u.wavef;
  P.pulse = u.pulse / 100.0f;
  P.pulseRate = u.pulserate;
  P.anim = anim;
  P.flute = u.flute / 100.0f;
  P.fluteF = u.flutef;
  P.smorph = u.smorph / 100.0f;
  P.radial = u.radial;
  P.noiseB = u.noiseb / 100.0f;
  P.noiseS = u.noises;
  P.gyroid = u.gyroid / 100.0f;
  P.gyroS = u.gyros;
  P.worley = u.worley / 100.0f;
  P.worlS = u.wors;
  P.shell = u.shell / 100.0f;
  P.invert = u.invert / 100.0f;
  P.csg = (float)u.csgsel;
  P.csgSize = u.csgsize;
  P.csgY = u.csgh;
  P.repeat = u.repeat;
  P.juliaW = u.juliaw / 100.0f;
  P.branch = u.branch * kDeg2Rad;
  P.branchAlt = u.branchalt / 100.0f;
  P.taper = u.taper / 100.0f;
  P.leader = u.leader / 100.0f;
  P.wobble = u.wobble / 100.0f;
  fillBranchSeq(u.seed, P.branchSeq);
  P.pruneMode = (float)u.prunesel;
  P.pruneH = u.pruneh;
  P.pruneTight = u.prunetight / 100.0f;
  P.voxel = u.voxel / 100.0f;
  P.voxelGrid = (float)llround(u.voxelgrid);
  P.patB = (float)u.patbsel;
  P.patBScale = u.patbscale;
  P.patBAmt = u.patbamt / 100.0f;
  P.patBBlend = (float)u.patbblend;
  P.groove = u.groove / 100.0f;
  P.grooveF = u.groovef;
  P.panel = u.panel / 100.0f;
  P.panelS = u.panels;
  P.helix = u.helix / 100.0f;
  P.helixTurns = u.helixturns;
  P.onion = (float)llround(u.onion);
  P.patColor = u.patcol / 100.0f;
  P.morphY = u.morphy / 100.0f;
  P.morphGrad = u.mgrad / 100.0f;
  P.arc = u.arc / 100.0f;
  P.twistGrad = u.twistgrad * kDeg2Rad;
  P.baseOn = u.baseon;
  P.baseH = u.baseh;
  return P;
}

// The web loop's step-size rule: heavy feature sets march in smaller steps.
inline float computeStepBase(const Params& P) {
  bool heavy = P.wave > 0.02f || P.noiseB > 0.02f || P.invert > 0.02f || P.repeat > 0.01f ||
               P.gyroid > 0.02f || P.voxel > 0.02f || P.patB > 0.5f || P.helix > 0.02f ||
               P.arc != 0.0f || P.wobble > 0.02f;
  return heavy ? 0.55f : 0.9f;
}

// Quality modes from the optimized Electron build.
struct QualityMode { float stepMul, qMin, qMax; };
inline QualityMode qualityMode(int q) {
  switch (q) {
    case 0: return {0.8f, 0.6f, 1.0f};   // Cinema
    case 1: return {1.0f, 0.45f, 1.0f};  // Balanced
    default: return {1.35f, 0.3f, 0.75f}; // Turbo
  }
}
