#pragma once
// The fractal distance estimator and shading, ported line for line from the
// web build's GLSL fragment shader. Same math, CUDA syntax, so a settings
// file draws the same shape in both builds. The EvalCtx carries what the
// shader kept in globals (gTrap, gLevel, gOverlay): every de() call updates
// it, including the normal-estimation calls, exactly like the original.

#include <cuda_runtime.h>
#include <math.h>
#include "params.h"

struct EvalCtx {
  float trap = 1e9f;
  float level = 0.0f;
  float overlay = 0.0f;
  float steps = 0.0f;
};

// ---- small vec helpers (GLSL semantics) ----
__device__ __forceinline__ float3 v3(float x, float y, float z) { return make_float3(x, y, z); }
__device__ __forceinline__ float3 operator+(float3 a, float3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
__device__ __forceinline__ float3 operator-(float3 a, float3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
__device__ __forceinline__ float3 operator-(float3 a) { return v3(-a.x, -a.y, -a.z); }
__device__ __forceinline__ float3 operator*(float3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
__device__ __forceinline__ float3 operator*(float s, float3 a) { return a * s; }
__device__ __forceinline__ float3 operator/(float3 a, float s) { return v3(a.x / s, a.y / s, a.z / s); }
__device__ __forceinline__ float3 operator+(float3 a, float s) { return v3(a.x + s, a.y + s, a.z + s); }
__device__ __forceinline__ float3 operator-(float3 a, float s) { return v3(a.x - s, a.y - s, a.z - s); }
__device__ __forceinline__ float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
__device__ __forceinline__ float3 cross3(float3 a, float3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
__device__ __forceinline__ float len3(float3 a) { return sqrtf(dot3(a, a)); }
__device__ __forceinline__ float3 norm3(float3 a) { return a / fmaxf(len3(a), 1e-12f); }
__device__ __forceinline__ float clampf(float x, float a, float b) { return fminf(fmaxf(x, a), b); }
__device__ __forceinline__ float mixf(float a, float b, float t) { return a + (b - a) * t; }
__device__ __forceinline__ float3 mix3(float3 a, float3 b, float t) { return a + (b - a) * t; }
__device__ __forceinline__ float fractf(float x) { return x - floorf(x); }
__device__ __forceinline__ float glslMod(float x, float y) { return x - y * floorf(x / y); }
__device__ __forceinline__ float smoothstepf(float e0, float e1, float x) {
  float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
// rotations matching the shader's column-major mat3 constructors
__device__ __forceinline__ float3 rotY(float3 v, float a) {
  float c = cosf(a), s = sinf(a);
  return v3(c * v.x - s * v.z, v.y, s * v.x + c * v.z);
}
__device__ __forceinline__ float3 rotX(float3 v, float a) {
  float c = cosf(a), s = sinf(a);
  return v3(v.x, c * v.y - s * v.z, s * v.y + c * v.z);
}
__device__ __forceinline__ float3 rotZ(float3 v, float a) {
  float c = cosf(a), s = sinf(a);
  return v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

__device__ float hash13(float3 p) {
  p = v3(fractf(p.x * 0.3183099f + 0.11f), fractf(p.y * 0.3183099f + 0.17f), fractf(p.z * 0.3183099f + 0.13f));
  p = p * 17.0f;
  return fractf(p.x * p.y * p.z * (p.x + p.y + p.z));
}
__device__ float vnoise(float3 p) {
  float3 i = v3(floorf(p.x), floorf(p.y), floorf(p.z));
  float3 f = v3(fractf(p.x), fractf(p.y), fractf(p.z));
  f = v3(f.x * f.x * (3.0f - 2.0f * f.x), f.y * f.y * (3.0f - 2.0f * f.y), f.z * f.z * (3.0f - 2.0f * f.z));
  float n000 = hash13(i), n100 = hash13(i + v3(1, 0, 0));
  float n010 = hash13(i + v3(0, 1, 0)), n110 = hash13(i + v3(1, 1, 0));
  float n001 = hash13(i + v3(0, 0, 1)), n101 = hash13(i + v3(1, 0, 1));
  float n011 = hash13(i + v3(0, 1, 1)), n111 = hash13(i + v3(1, 1, 1));
  return mixf(mixf(mixf(n000, n100, f.x), mixf(n010, n110, f.x), f.y),
              mixf(mixf(n001, n101, f.x), mixf(n011, n111, f.x), f.y), f.z);
}
__device__ float fbm3(float3 p) {
  float s = 0.0f, a = 0.5f;
  for (int i = 0; i < 4; i++) { s += a * vnoise(p); p = p * 2.03f; a *= 0.5f; }
  return s;
}
__device__ float3 hash33(float3 p3) {
  p3 = v3(fractf(p3.x * 0.1031f), fractf(p3.y * 0.1030f), fractf(p3.z * 0.0973f));
  // GLSL: p3 += dot(p3, p3.yxz + 33.33); p3.yxz is (p3.y, p3.x, p3.z)
  float d = dot3(p3, v3(p3.y, p3.x, p3.z) + 33.33f);
  p3 = p3 + d;
  // fract((p3.xxy + p3.yxx) * p3.zyx): xxy=(x,x,y), yxx=(y,x,x), zyx=(z,y,x)
  float3 a = v3(p3.x, p3.x, p3.y) + v3(p3.y, p3.x, p3.x);
  float3 b = v3(p3.z, p3.y, p3.x);
  return v3(fractf(a.x * b.x), fractf(a.y * b.y), fractf(a.z * b.z));
}
__device__ float2 worley2(float3 p) {
  float3 ip = v3(floorf(p.x), floorf(p.y), floorf(p.z));
  float3 fp = v3(fractf(p.x), fractf(p.y), fractf(p.z));
  float f1 = 8.0f, f2 = 8.0f;
  for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j++) for (int k = -1; k <= 1; k++) {
    float3 g = v3((float)i, (float)j, (float)k);
    float3 o = hash33(ip + g);
    float d = len3(g + o - fp);
    if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) { f2 = d; }
  }
  return make_float2(f1, f2);
}
struct Quat { float x, y, z, w; }; // GLSL vec4 (x, y, z, w) with qmul on (w?) see below
__device__ Quat qmul(Quat a, Quat b) {
  // GLSL qmul(vec4 a, vec4 b): (a.x*b.x - dot(a.yzw,b.yzw), a.x*b.yzw + b.x*a.yzw + cross(a.yzw,b.yzw))
  float3 ayzw = v3(a.y, a.z, a.w);
  float3 byzw = v3(b.y, b.z, b.w);
  float3 r = a.x * byzw + b.x * ayzw + cross3(ayzw, byzw);
  return {a.x * b.x - dot3(ayzw, byzw), r.x, r.y, r.z};
}

__device__ float deTyped(const Params& P, float3 p, int type, float& trap, float& level) {
  float3 offs = v3(1.0f, 1.0f, 1.0f) + P.lean * v3(1.0f, 0.35f, -0.65f);
  trap = 1e9f; level = 0.0f;
  float invDr = 1.0f;
  for (int n = 0; n < 24; n++) {
    if (n >= P.iter) break;
    if (type == 0) {
      if (p.x + p.y < 0.0f) { float nx = -p.y, ny = -p.x; p.x = nx; p.y = ny; }
      if (p.x + p.z < 0.0f) { float nx = -p.z, nz = -p.x; p.x = nx; p.z = nz; }
      if (p.y + p.z < 0.0f) { float ny = -p.z, nz = -p.y; p.y = ny; p.z = nz; }
    } else if (type == 1) {
      p = v3(fabsf(p.x), fabsf(p.y), fabsf(p.z));
      if (p.x < p.y) { float t = p.x; p.x = p.y; p.y = t; }
      if (p.x < p.z) { float t = p.x; p.x = p.z; p.z = t; }
      if (p.y < p.z) { float t = p.y; p.y = p.z; p.z = t; }
    } else if (type == 3) {
      p = v3(fabsf(p.x), fabsf(p.y), fabsf(p.z));
      float3 nrm;
      nrm = norm3(v3(1.0f, 1.618034f, 0.0f));
      if (dot3(p, nrm) < 0.0f) p = p - nrm * (2.0f * dot3(p, nrm));
      nrm = norm3(v3(0.0f, 1.0f, 1.618034f));
      if (dot3(p, nrm) < 0.0f) p = p - nrm * (2.0f * dot3(p, nrm));
      nrm = norm3(v3(1.618034f, 0.0f, 1.0f));
      if (dot3(p, nrm) < 0.0f) p = p - nrm * (2.0f * dot3(p, nrm));
      p = v3(fabsf(p.x), fabsf(p.y), fabsf(p.z));
    } else {
      p = v3(fabsf(p.x), fabsf(p.y), fabsf(p.z));
      if (p.x < p.y) { float t = p.x; p.x = p.y; p.y = t; }
      if (p.y < p.z) { float t = p.y; p.y = p.z; p.z = t; }
    }
    if (P.twist != 0.0f || P.tumble != 0.0f) p = rotZ(rotX(rotY(p, P.twist), P.tumble), P.tumble * 0.7f);
    if (P.leader > 0.001f) {
      p.y *= (1.0f + 0.16f * P.leader);
      p.x *= (1.0f - 0.06f * P.leader);
      p.z *= (1.0f - 0.06f * P.leader);
    }
    if (P.branch != 0.0f || P.branchAlt != 0.0f) {
      float bdir = (glslMod((float)n, 2.0f) < 1.0f) ? 1.0f : -1.0f;
      float bang = P.branch * (1.0f + P.branchAlt * bdir);
      p = rotZ(rotX(rotY(p, bang), bang * 0.65f), bang * 0.45f);
    }
    const float* bseq = P.branchSeq[n];
    if (P.wobble > 0.001f) {
      float wa = P.wobble * 0.38f;
      p = rotZ(rotX(rotY(p, bseq[1] * wa), bseq[0] * wa), bseq[2] * wa);
    }
    float pul = P.pulse * sinf((float)n * P.pulseRate + P.anim * 0.7f);
    if (P.pulse > 0.001f) p = rotY(p, pul * 0.6f);
    if (P.wave > 0.001f) {
      p = p + (P.wave * 0.06f) * v3(
        sinf(p.y * P.waveF + P.anim + (float)n * 1.7f),
        sinf(p.z * P.waveF * 1.13f + P.anim),
        sinf(p.x * P.waveF * 0.91f + P.anim));
    }
    float taperF = clampf(1.0f + P.taper * 0.38f * ((float)n / fmaxf((float)(P.iter - 1), 1.0f)), 0.62f, 1.65f);
    float effScale = P.scale * (1.0f + 0.28f * pul) * taperF;
    float3 offsEff = offs * (1.0f + 0.3f * P.pulse * sinf((float)n * P.pulseRate + 2.1f + P.anim * 0.7f));
    if (P.leader > 0.001f) offsEff.y *= (1.0f - 0.10f * P.leader);
    if (P.wobble > 0.001f) offsEff = offsEff * (1.0f + bseq[3] * P.wobble * 0.18f);
    p = p * effScale - offsEff * (effScale - 1.0f);
    invDr /= effScale;
    float l = len3(p);
    if (l < trap) { trap = l; level = (float)n; }
  }
  return (len3(p) - 0.35f - P.bulge) * invDr;
}

__device__ float deBulb(const Params& P, float3 p, float& trap) {
  float3 z = p;
  float dr = 1.0f, r = 0.0f;
  trap = 1e9f;
  float power = 6.0f + P.scale * 1.5f;
  for (int i = 0; i < 16; i++) {
    r = len3(z);
    trap = fminf(trap, r);
    if (r > 2.0f) break;
    float th = acosf(clampf(z.z / fmaxf(r, 1e-6f), -1.0f, 1.0f));
    float ph = atan2f(z.y, z.x);
    dr = powf(r, power - 1.0f) * power * dr + 1.0f;
    float zr = powf(r, power);
    th *= power; ph *= power;
    z = zr * v3(sinf(th) * cosf(ph), sinf(ph) * sinf(th), cosf(th));
    z = z + p;
  }
  return 0.5f * logf(fmaxf(r, 1e-4f)) * r / fmaxf(dr, 1e-4f);
}

__device__ float deJulia(const Params& P, float3 p, float& trap) {
  Quat z{p.x, p.y, p.z, P.juliaW};
  Quat dz{1.0f, 0.0f, 0.0f, 0.0f};
  Quat c{-0.18f, 0.62f, 0.12f, 0.05f};
  auto qlen = [](Quat q) { return sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); };
  float r = qlen(z);
  trap = r;
  for (int i = 0; i < 14; i++) {
    Quat dzNew = qmul(z, dz);
    dz = {2.0f * dzNew.x, 2.0f * dzNew.y, 2.0f * dzNew.z, 2.0f * dzNew.w};
    Quat z2 = qmul(z, z);
    z = {z2.x + c.x, z2.y + c.y, z2.z + c.z, z2.w + c.w};
    r = qlen(z);
    trap = fminf(trap, r);
    if (r > 4.0f) break;
  }
  return 0.5f * logf(fmaxf(r, 1e-4f)) * r / fmaxf(qlen(dz), 1e-4f);
}

__device__ float deAny(const Params& P, float3 p, int type, float& trap, float& level) {
  if (type == 4) { float d = deBulb(P, p, trap); level = clampf(trap * 4.0f, 0.0f, 20.0f); return d; }
  if (type == 5) { float d = deJulia(P, p, trap); level = clampf(trap * 4.0f, 0.0f, 20.0f); return d; }
  return deTyped(P, p, type, trap, level);
}

__device__ float de(const Params& P, float3 p, EvalCtx& ctx) {
  float3 q = p;
  q.y *= P.squash;
  ctx.overlay = 0.0f;
  if (P.arc != 0.0f) {
    float ba = P.arc * 0.55f * q.y;
    float3 r = rotZ(v3(q.x, q.y, 0.0f), ba);
    q.x = r.x; q.y = r.y;
    q.x -= P.arc * 0.12f * q.y * q.y;
  }
  if (P.twistGrad != 0.0f) {
    q = rotY(q, P.twistGrad * q.y * 0.65f);
  }
  if (P.radial > 1.5f) {
    float ang = atan2f(q.z, q.x);
    float sec = 6.28318f / P.radial;
    ang = glslMod(ang, sec) - sec * 0.5f;
    float rr = sqrtf(q.x * q.x + q.z * q.z);
    q.x = cosf(ang) * rr;
    q.z = sinf(ang) * rr;
  }
  if (P.invert > 0.001f) {
    float3 qi = q * (2.56f / fmaxf(dot3(q, q), 0.05f));
    q = mix3(q, qi, P.invert);
  }
  if (P.repeat > 0.01f) {
    float rp = P.repeat;
    q = v3(glslMod(q.x + 0.5f * rp, rp) - 0.5f * rp,
           glslMod(q.y + 0.5f * rp, rp) - 0.5f * rp,
           glslMod(q.z + 0.5f * rp, rp) - 0.5f * rp);
  }
  if (P.noiseB > 0.001f) {
    float3 nb = v3(fbm3(q * P.noiseS + 11.1f), fbm3(q * P.noiseS + 27.7f), fbm3(q * P.noiseS + 43.3f)) - 0.47f;
    q = q + nb * (P.noiseB * 1.5f);
  }
  if (P.wave > 0.001f) {
    q = q + (P.wave * 0.22f) * v3(
      sinf(q.y * P.waveF + P.anim),
      sinf(q.z * P.waveF * 1.13f + P.anim * 1.3f),
      sinf(q.x * P.waveF * 0.91f + P.anim * 0.7f));
  }
  if (P.voxel > 0.001f) {
    float cell = 4.4f / fmaxf(P.voxelGrid, 1.0f);
    float3 snapped = v3((floorf(q.x / cell) + 0.5f) * cell,
                        (floorf(q.y / cell) + 0.5f) * cell,
                        (floorf(q.z / cell) + 0.5f) * cell);
    q = mix3(q, snapped, P.voxel);
  }
  float trapA, levelA;
  float dA = deAny(P, q, P.type, trapA, levelA);
  float m = P.morph;
  if (P.morphGrad != 0.0f) m = clampf(m + p.y * 0.42f * P.morphGrad, 0.0f, 1.0f);
  if (P.smorph > 0.001f) {
    m = clampf(m + P.smorph * sinf(dot3(p, v3(0.7f, 1.0f, 0.45f)) * 2.6f + P.anim), 0.0f, 1.0f);
  }
  float d = dA, trap = trapA, level = levelA;
  if (m > 0.001f) {
    float trapB, levelB;
    float dB = deAny(P, q, P.typeB, trapB, levelB);
    d = mixf(dA, dB, m);
    trap = mixf(trapA, trapB, m);
    level = mixf(levelA, levelB, m);
  }
  if (P.morphY > 0.001f) {
    float trapC, levelC;
    float dC = deAny(P, q, P.typeC, trapC, levelC);
    d = mixf(d, dC, P.morphY);
    trap = mixf(trap, trapC, P.morphY);
    level = mixf(level, levelC, P.morphY);
  }
  if (P.flute > 0.001f) {
    d -= P.flute * 0.055f * sinf(p.y * P.fluteF + sqrtf(p.x * p.x + p.z * p.z) * P.fluteF * 0.35f + P.anim);
  }
  if (P.groove > 0.001f) {
    float s = 0.5f + 0.5f * sinf(p.y * P.grooveF + P.anim * 0.5f);
    float gg = powf(s, 8.0f);
    d -= P.groove * 0.055f * gg;
    ctx.overlay = fmaxf(ctx.overlay, gg);
  }
  if (P.gyroid > 0.001f) {
    float gf = P.gyroS * 1.35f;
    float G = sinf(q.x * gf) * cosf(q.y * gf) + sinf(q.y * gf) * cosf(q.z * gf) + sinf(q.z * gf) * cosf(q.x * gf);
    float gd = (fabsf(G) - 0.85f) / gf;
    d = mixf(d, fmaxf(d, gd), P.gyroid);
  }
  if (P.worley > 0.001f) {
    float2 w = worley2(q * P.worlS);
    d += P.worley * (w.y - w.x - 0.3f) * 0.10f;
  }
  if (P.patB > 0.5f && P.patBAmt > 0.001f) {
    float pd = 1e5f;
    if (P.patB < 1.5f) {
      float gf = P.patBScale * 1.35f;
      float G = sinf(q.x * gf) * cosf(q.y * gf) + sinf(q.y * gf) * cosf(q.z * gf) + sinf(q.z * gf) * cosf(q.x * gf);
      pd = (fabsf(G) - 0.85f) / gf;
    } else if (P.patB < 2.5f) {
      float2 w2 = worley2(q * P.patBScale);
      pd = (w2.y - w2.x - 0.10f) / fmaxf(P.patBScale, 0.2f) * 0.42f;
      ctx.overlay = fmaxf(ctx.overlay, clampf(1.0f - fabsf(w2.y - w2.x) * 3.0f, 0.0f, 1.0f));
    } else {
      float yy = fractf(q.y * P.patBScale) - 0.5f;
      pd = fabsf(yy) / fmaxf(P.patBScale, 0.2f);
    }
    ctx.overlay = fmaxf(ctx.overlay, clampf(1.0f - fabsf(pd) * 9.0f, 0.0f, 1.0f));
    if (P.patBBlend < 0.5f) d = mixf(d, fminf(d, pd), P.patBAmt);
    else if (P.patBBlend < 1.5f) d = mixf(d, fmaxf(d, -pd), P.patBAmt);
    else d = mixf(d, fmaxf(d, pd), P.patBAmt);
  }
  if (P.panel > 0.001f) {
    float3 gp = v3(fabsf(fractf(q.x * P.panelS) - 0.5f),
                   fabsf(fractf(q.y * P.panelS) - 0.5f),
                   fabsf(fractf(q.z * P.panelS) - 0.5f));
    float line = fminf(gp.x, fminf(gp.y, gp.z));
    float pmask = smoothstepf(0.10f, 0.015f, line);
    d -= P.panel * 0.05f * pmask;
    ctx.overlay = fmaxf(ctx.overlay, pmask);
  }
  if (P.helix > 0.001f) {
    float RR = 1.22f;
    float ang = atan2f(q.z, q.x);
    float ph1 = ang - q.y * P.helixTurns;
    float ph2 = ph1 + 3.14159f;
    float r_xz = sqrtf(q.x * q.x + q.z * q.z);
    float dr1 = sqrtf((r_xz - RR) * (r_xz - RR) + (sinf(ph1) * RR) * (sinf(ph1) * RR)) - 0.07f;
    float dr2 = sqrtf((r_xz - RR) * (r_xz - RR) + (sinf(ph2) * RR) * (sinf(ph2) * RR)) - 0.07f;
    float dr = fminf(dr1, dr2);
    d = mixf(d, fminf(d, dr), P.helix);
    ctx.overlay = fmaxf(ctx.overlay, clampf(1.0f - dr * 7.0f, 0.0f, 1.0f));
  }
  if (P.csg > 0.5f) {
    float3 cc = v3(0.0f, P.csgY, 0.0f);
    float ds = len3(p - cc) - P.csgSize;
    if (P.csg < 1.5f) d = fminf(d, ds);
    else if (P.csg < 2.5f) d = fmaxf(d, -ds);
    else if (P.csg < 3.5f) d = fmaxf(d, ds);
    else d = fmaxf(d, P.csgY - p.y);
  }
  if (P.onion > 0.5f) {
    float spacing = 0.24f;
    float thick = fmaxf(P.shell, 0.025f);
    d = fabsf(fractf(d / spacing + 0.5f) - 0.5f) * spacing - thick;
  } else if (P.shell > 0.001f) {
    d = fabsf(d) - P.shell;
  }
  if (P.pruneMode > 0.5f) {
    float allowed;
    if (P.pruneMode < 1.5f) {
      float rad = mixf(1.25f, 0.18f, P.pruneTight) + fmaxf(p.y - P.pruneH, 0.0f) * 0.85f + fmaxf(P.pruneH - p.y, 0.0f) * 0.20f;
      allowed = sqrtf(p.x * p.x + p.z * p.z) - rad;
    } else if (P.pruneMode < 2.5f) {
      allowed = p.y - P.pruneH;
    } else {
      allowed = sqrtf(p.x * p.x + p.z * p.z) - mixf(1.5f, 0.22f, P.pruneTight);
    }
    d = fmaxf(d, allowed);
  }
  ctx.trap = trap; ctx.level = level;
  return d / fmaxf(P.squash, 1.0f);
}

__device__ float3 calcNormal(const Params& P, float3 p, EvalCtx& ctx) {
  const float e = 0.0012f;
  float3 k1 = v3(1, -1, -1), k2 = v3(-1, 1, -1), k3 = v3(-1, -1, 1), k4 = v3(1, 1, 1);
  float3 g = k1 * de(P, p + k1 * e, ctx) + k2 * de(P, p + k2 * e, ctx) +
             k3 * de(P, p + k3 * e, ctx) + k4 * de(P, p + k4 * e, ctx);
  return norm3(g);
}

__device__ float3 palette(const Params& P, float t) {
  float3 a, b;
  if (P.pal == 0)      { a = v3(1, 1, 1);       b = v3(0.0f, 0.33f, 0.67f); }
  else if (P.pal == 1) { a = v3(1, 0.7f, 0.4f); b = v3(0.0f, 0.12f, 0.25f); }
  else if (P.pal == 2) { a = v3(0.6f, 0.9f, 1); b = v3(0.45f, 0.35f, 0.15f); }
  else                 { a = v3(1, 0.5f, 1);    b = v3(0.6f, 0.1f, 0.75f); }
  // 0.5 + 0.5 * cos(6.28318 * (t * a + b))
  float3 ph = (t * a + b) * 6.28318f;
  return v3(0.5f + 0.5f * cosf(ph.x), 0.5f + 0.5f * cosf(ph.y), 0.5f + 0.5f * cosf(ph.z));
}

// Full per-pixel shading: the shader's main(), given a ray.
__device__ float3 shadeRay(const Params& P, float3 ro, float3 rd) {
  EvalCtx ctx;
  float3 col = v3(0.012f, 0.016f, 0.028f);
  float bg = 0.5f + 0.5f * rd.y;
  col = col + v3(0.010f, 0.020f, 0.030f) * bg;

  float t = 0.0f;
  const float tmax = 14.0f;
  float d = 0.0f;
  int steps = 0;
  for (int i = 0; i < 110; i++) {
    d = de(P, ro + rd * t, ctx);
    steps = i;
    if (d < 0.0007f * (1.0f + t) / P.detail) break;
    t += d * P.step;
    if (t > tmax) break;
  }
  ctx.steps = (float)steps;

  if (t < tmax) {
    float3 pos = ro + rd * t;
    float3 nor = calcNormal(P, pos, ctx);
    float trap = ctx.trap;
    float3 smoothCol = palette(P, trap * P.spread * 0.55f + P.phase);
    float3 bandCol = palette(P, ctx.level / fmaxf((float)P.iter, 1.0f) * 0.85f + P.phase);
    float3 base = mix3(smoothCol, bandCol, P.band);
    if (P.patColor > 0.001f)
      base = mix3(base, palette(P, trap * P.spread * 0.55f + P.phase + 0.37f), P.patColor * clampf(ctx.overlay, 0.0f, 1.0f));
    float st = 0.5f + 0.5f * sinf(pos.y * 16.0f);
    float stripeMul = mixf(1.0f, 0.45f + 0.9f * st, P.stripe);
    base = base * stripeMul;
    float3 lig = norm3(v3(cosf(P.lightAng), 0.75f, sinf(P.lightAng)));
    float dif = clampf(dot3(nor, lig), 0.0f, 1.0f);
    float sky = clampf(0.5f + 0.5f * nor.y, 0.0f, 1.0f);
    float rim = powf(1.0f - clampf(dot3(nor, -rd), 0.0f, 1.0f), 3.0f);
    float ao = clampf(1.0f - ctx.steps / 110.0f, 0.35f, 1.0f);
    col = base * ((0.22f + 0.85f * dif + 0.25f * sky) * ao);
    col = col + v3(0.45f, 0.95f, 0.85f) * (rim * P.glowAmt * (0.35f + 0.65f * dif));
    float thin = clampf(1.15f - trap * 0.85f, 0.0f, 1.0f);
    col = col + base * (thin * P.edgeGlow * 1.8f);
    float fogF = smoothstepf(4.5f, 13.5f, t) * P.fog;
    col = mix3(col, v3(0.012f, 0.016f, 0.028f), fogF);
  }
  col = v3(powf(col.x, 0.85f), powf(col.y, 0.85f), powf(col.z, 0.85f));
  return col;
}
