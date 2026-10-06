#include "audio.h"

#include <initguid.h>
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace {

constexpr int kFftSize = 2048;
constexpr int kBinCount = kFftSize / 2;
constexpr double kPi = 3.14159265358979323846;
// Web Audio analyser defaults, mirrored so band levels feel the same.
constexpr float kMinDb = -100.0f;
constexpr float kMaxDb = -30.0f;
constexpr float kSpectrumSmoothing = 0.55f; // analyser smoothingTimeConstant
constexpr size_t kHistorySamples = 16384;

std::string hrError(const char* what, HRESULT hr) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%s failed (hr=0x%08lX)", what, (unsigned long)hr);
  return std::string(buf);
}

std::string wideToUtf8(const wchar_t* w) {
  if (!w) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return std::string();
  std::string out((size_t)n - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
  return out;
}

template <typename T>
void safeRelease(T*& p) {
  if (p) { p->Release(); p = nullptr; }
}

// One enumerated capture endpoint.
struct DeviceInfo {
  std::wstring id;
  std::string name;
};

} // namespace

struct AudioEngine::Impl {
  // COM / WASAPI state
  bool comStarted = false;   // we called CoInitializeEx successfully
  bool comReady = false;     // COM usable in this process
  IMMDeviceEnumerator* enumerator = nullptr;
  IAudioClient* client = nullptr;
  IAudioCaptureClient* capture = nullptr;

  // Stream format (from GetMixFormat)
  int sampleRate = 48000;
  int channels = 2;
  int bytesPerSample = 4;
  int blockAlign = 8;
  int validBits = 32;
  bool isFloat = true;

  // Capture plumbing
  std::thread thread;
  std::atomic<bool> stopFlag{false};
  std::atomic<bool> isRunning{false};
  std::mutex historyMutex;
  std::deque<float> history;        // latest mono samples, oldest first
  std::string error;

  // Analysis state
  AudioLevels lv;
  float bassAvg = 0.25f;            // web build initial value
  float beatEnv = 0.0f;
  std::array<float, kBinCount> spectrum{}; // smoothed magnitudes
  std::array<float, kFftSize> window{};
  std::array<float, kFftSize> windowed{};
  std::vector<std::complex<float>> fftBuf;

  Impl() {
    spectrum.fill(0.0f);
    // Blackman window, the same family Web Audio's analyser uses.
    for (int n = 0; n < kFftSize; n++) {
      double t = (double)n / (double)(kFftSize - 1);
      window[n] = (float)(0.42 - 0.5 * std::cos(2.0 * kPi * t) + 0.08 * std::cos(4.0 * kPi * t));
    }
    fftBuf.resize(kFftSize);
  }

  std::vector<DeviceInfo> enumerate() {
    std::vector<DeviceInfo> out;
    if (!enumerator) return out;
    IMMDeviceCollection* coll = nullptr;
    HRESULT hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &coll);
    if (FAILED(hr) || !coll) { safeRelease(coll); return out; }
    UINT count = 0;
    coll->GetCount(&count);
    for (UINT i = 0; i < count; i++) {
      IMMDevice* dev = nullptr;
      if (FAILED(coll->Item(i, &dev)) || !dev) continue;
      DeviceInfo info;
      LPWSTR id = nullptr;
      if (SUCCEEDED(dev->GetId(&id)) && id) {
        info.id = id;
        CoTaskMemFree(id);
      }
      info.name = "Microphone";
      IPropertyStore* props = nullptr;
      if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props) {
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var)) && var.vt == VT_LPWSTR && var.pwszVal) {
          info.name = wideToUtf8(var.pwszVal);
        }
        PropVariantClear(&var);
        props->Release();
      }
      if (!info.id.empty()) out.push_back(std::move(info));
      dev->Release();
    }
    coll->Release();
    return out;
  }

  void pushSamples(const float* mono, size_t n) {
    if (!n) return;
    std::lock_guard<std::mutex> lock(historyMutex);
    for (size_t i = 0; i < n; i++) history.push_back(mono[i]);
    while (history.size() > kHistorySamples) history.pop_front();
  }

  // Convert one interleaved frame (blockAlign bytes) to mono float.
  float readMono(const uint8_t* frame) const {
    double acc = 0.0;
    for (int c = 0; c < channels; c++) {
      const uint8_t* p = frame + (size_t)c * (size_t)bytesPerSample;
      double v = 0.0;
      if (isFloat) {
        float f;
        std::memcpy(&f, p, sizeof(float));
        v = (double)f;
      } else if (bytesPerSample == 1) {
        v = ((double)(*p) - 128.0) / 128.0;
      } else if (bytesPerSample == 2) {
        int16_t s;
        std::memcpy(&s, p, sizeof(s));
        v = (double)s / 32768.0;
      } else if (bytesPerSample == 3) {
        int32_t s = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
        if (s & 0x800000) s |= ~0xFFFFFF;
        v = (double)s / 8388608.0;
      } else {
        int32_t s;
        std::memcpy(&s, p, sizeof(s));
        int shift = (validBits >= 32) ? 0 : (32 - validBits);
        if (shift > 0) s >>= shift;
        double denom = (validBits >= 32) ? 2147483648.0
                       : (validBits == 24) ? 8388608.0
                       : (validBits == 20) ? 524288.0
                       : 32768.0;
        v = (double)s / denom;
      }
      acc += v;
    }
    return channels > 0 ? (float)(acc / (double)channels) : 0.0f;
  }

  void captureLoop() {
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool coInit = SUCCEEDED(coHr);
    std::vector<float> mono;
    while (!stopFlag.load(std::memory_order_relaxed)) {
      UINT32 packetFrames = 0;
      HRESULT hr = capture->GetNextPacketSize(&packetFrames);
      if (FAILED(hr)) break;
      bool gotAny = false;
      while (packetFrames > 0) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr) || frames == 0) break;
        gotAny = true;
        mono.assign(frames, 0.0f);
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
          const uint8_t* base = data;
          for (UINT32 f = 0; f < frames; f++) {
            mono[f] = readMono(base + (size_t)f * (size_t)blockAlign);
          }
        }
        pushSamples(mono.data(), mono.size());
        capture->ReleaseBuffer(frames);
        if (FAILED(capture->GetNextPacketSize(&packetFrames))) { packetFrames = 0; break; }
      }
      if (!gotAny) Sleep(5);
    }
    if (coInit) CoUninitialize();
  }

  // Iterative radix-2 FFT, in place, forward transform.
  void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
      size_t bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
      double ang = -2.0 * kPi / (double)len;
      std::complex<float> wlen((float)std::cos(ang), (float)std::sin(ang));
      for (size_t i = 0; i < n; i += len) {
        std::complex<float> w(1.0f, 0.0f);
        for (size_t k = 0; k < len / 2; k++) {
          std::complex<float> u = a[i + k];
          std::complex<float> v = a[i + k + len / 2] * w;
          a[i + k] = u + v;
          a[i + k + len / 2] = u - v;
          w *= wlen;
        }
      }
    }
  }

  float bandLevel(const std::array<float, kBinCount>& bytes, double binHz, double lo, double hi) const {
    int a = std::max(0, (int)std::floor(lo / binHz));
    int b = std::min(kBinCount - 1, (int)std::ceil(hi / binHz));
    if (b <= a) return 0.0f;
    double sum = 0.0;
    for (int i = a; i <= b; i++) sum += bytes[(size_t)i];
    return (float)(sum / (double)(b - a + 1) / 255.0);
  }
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() {
  stop();
  if (impl_) {
    safeRelease(impl_->enumerator);
    if (impl_->comStarted) CoUninitialize();
  }
}

bool AudioEngine::init() {
  Impl& im = *impl_;
  if (im.enumerator) return true;
  if (!im.comReady) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
      // COM already lives in another apartment in this process; usable.
      im.comReady = true;
    } else if (SUCCEEDED(hr)) {
      im.comReady = true;
      im.comStarted = true;
    } else {
      im.error = hrError("CoInitializeEx", hr);
      return false;
    }
  }
  HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                IID_IMMDeviceEnumerator, (void**)&im.enumerator);
  if (FAILED(hr) || !im.enumerator) {
    im.error = hrError("Create device enumerator", hr);
    safeRelease(im.enumerator);
    return false;
  }
  im.error.clear();
  return true;
}

std::vector<std::string> AudioEngine::deviceNames() {
  std::vector<std::string> names;
  if (!impl_->enumerator && !init()) return names;
  auto devs = impl_->enumerate();
  names.reserve(devs.size());
  for (auto& d : devs) names.push_back(d.name);
  return names;
}

bool AudioEngine::start(int deviceIndex) {
  Impl& im = *impl_;
  stop();
  if (!im.enumerator && !init()) return false;

  IMMDevice* device = nullptr;
  HRESULT hr;
  if (deviceIndex < 0) {
    hr = im.enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    if (FAILED(hr) || !device) {
      im.error = hrError("Get default input device", hr);
      safeRelease(device);
      return false;
    }
  } else {
    auto devs = im.enumerate();
    if (deviceIndex >= (int)devs.size()) {
      im.error = "Microphone index out of range";
      return false;
    }
    hr = im.enumerator->GetDevice(devs[(size_t)deviceIndex].id.c_str(), &device);
    if (FAILED(hr) || !device) {
      im.error = hrError("Open input device", hr);
      safeRelease(device);
      return false;
    }
  }

  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&im.client);
  device->Release();
  device = nullptr;
  if (FAILED(hr) || !im.client) {
    im.error = hrError("Activate audio client", hr);
    safeRelease(im.client);
    return false;
  }

  // Best effort raw capture (skips Windows sound enhancements) where
  // supported. A failure here just means the plain shared path.
  {
    IAudioClient2* client2 = nullptr;
    if (SUCCEEDED(im.client->QueryInterface(__uuidof(IAudioClient2), (void**)&client2)) && client2) {
      AudioClientProperties props{};
      props.cbSize = sizeof(props);
      props.bIsOffload = FALSE;
      props.eCategory = AudioCategory_Other;
      props.Options = AUDCLNT_STREAMOPTIONS_RAW;
      client2->SetClientProperties(&props);
      client2->Release();
    }
  }

  WAVEFORMATEX* mix = nullptr;
  hr = im.client->GetMixFormat(&mix);
  if (FAILED(hr) || !mix) {
    im.error = hrError("Get mix format", hr);
    if (mix) CoTaskMemFree(mix);
    safeRelease(im.client);
    return false;
  }
  im.sampleRate = (int)mix->nSamplesPerSec;
  im.channels = (int)mix->nChannels;
  im.blockAlign = (int)mix->nBlockAlign;
  im.bytesPerSample = (mix->nChannels > 0 && mix->nBlockAlign > 0)
                          ? (int)(mix->nBlockAlign / mix->nChannels)
                          : (int)(mix->wBitsPerSample / 8);
  if (im.bytesPerSample <= 0) im.bytesPerSample = 2;
  im.validBits = (int)mix->wBitsPerSample;
  im.isFloat = false;
  if (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    im.isFloat = true;
  } else if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    const WAVEFORMATEXTENSIBLE* ext = (const WAVEFORMATEXTENSIBLE*)mix;
    if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) im.isFloat = true;
    if (ext->Samples.wValidBitsPerSample > 0) im.validBits = (int)ext->Samples.wValidBitsPerSample;
  }

  // One second of shared-mode buffering keeps the capture thread relaxed.
  const REFERENCE_TIME kBufferDuration = 10000000; // 1 s in 100 ns units
  hr = im.client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBufferDuration, 0, mix, nullptr);
  CoTaskMemFree(mix);
  mix = nullptr;
  if (FAILED(hr)) {
    im.error = hrError("Initialize audio client", hr);
    safeRelease(im.client);
    return false;
  }

  hr = im.client->GetService(__uuidof(IAudioCaptureClient), (void**)&im.capture);
  if (FAILED(hr) || !im.capture) {
    im.error = hrError("Get capture client", hr);
    safeRelease(im.capture);
    safeRelease(im.client);
    return false;
  }

  hr = im.client->Start();
  if (FAILED(hr)) {
    im.error = hrError("Start capture", hr);
    safeRelease(im.capture);
    safeRelease(im.client);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(im.historyMutex);
    im.history.clear();
  }
  im.spectrum.fill(0.0f);
  im.stopFlag.store(false);
  im.isRunning.store(true);
  im.thread = std::thread([this]() { impl_->captureLoop(); });
  im.error.clear();
  return true;
}

void AudioEngine::stop() {
  Impl& im = *impl_;
  if (im.isRunning.load() || im.thread.joinable()) {
    im.stopFlag.store(true);
    if (im.client) im.client->Stop();
    if (im.thread.joinable()) im.thread.join();
    im.isRunning.store(false);
  }
  safeRelease(im.capture);
  safeRelease(im.client);
}

bool AudioEngine::running() const { return impl_->isRunning.load(); }

void AudioEngine::poll(float dt, float gain, float smoothAmt) {
  Impl& im = *impl_;
  if (!im.isRunning.load()) return;
  if (dt < 0.0f) dt = 0.0f;
  if (dt > 0.05f) dt = 0.05f;
  smoothAmt = std::min(0.95f, std::max(0.0f, smoothAmt));

  // Latest analysis window: the most recent 2048 mono samples,
  // zero padded at the front before enough audio has arrived,
  // exactly like the web analyser's first frames.
  std::array<float, kFftSize> samples{};
  samples.fill(0.0f);
  {
    std::lock_guard<std::mutex> lock(im.historyMutex);
    size_t avail = im.history.size();
    size_t take = std::min(avail, (size_t)kFftSize);
    size_t start = avail - take;
    size_t dst = (size_t)kFftSize - take;
    size_t idx = 0;
    for (float v : im.history) {
      if (idx >= start) samples[dst + (idx - start)] = v;
      idx++;
    }
  }

  // Time-domain volume, RMS over the window times 2.2 (web build).
  double sq = 0.0;
  for (int i = 0; i < kFftSize; i++) sq += (double)samples[i] * (double)samples[i];
  float rawVol = (float)(std::sqrt(sq / (double)kFftSize) * 2.2);

  // Windowed FFT and smoothed magnitude spectrum.
  for (int i = 0; i < kFftSize; i++) {
    im.windowed[i] = samples[i] * im.window[i];
    im.fftBuf[(size_t)i] = std::complex<float>(im.windowed[i], 0.0f);
  }
  im.fft(im.fftBuf);
  const double binHz = (double)im.sampleRate / (double)kFftSize;
  // Normalize so a full-scale sine reads magnitude about 1 at its bin:
  // the DFT of a windowed sine peaks at amplitude * sum(window) / 2.
  double winSum = 0.0;
  for (int i = 0; i < kFftSize; i++) winSum += im.window[i];
  const float norm = (float)(2.0 / winSum);
  std::array<float, kBinCount> bytes{};
  for (int k = 0; k < kBinCount; k++) {
    float mag = std::abs(im.fftBuf[(size_t)k]) * norm;
    float sm = kSpectrumSmoothing * im.spectrum[(size_t)k] + (1.0f - kSpectrumSmoothing) * mag;
    im.spectrum[(size_t)k] = sm;
    float db = (sm > 1e-12f) ? 20.0f * std::log10(sm) : kMinDb;
    float byteVal = (db - kMinDb) / (kMaxDb - kMinDb);
    byteVal = std::min(1.0f, std::max(0.0f, byteVal));
    bytes[(size_t)k] = byteVal * 255.0f;
  }

  float rawBass = im.bandLevel(bytes, binHz, 25.0, 160.0);
  float rawMid = im.bandLevel(bytes, binHz, 160.0, 2000.0);
  float rawHigh = im.bandLevel(bytes, binHz, 4000.0, 12000.0);

  // Beat: rolling bass average, spike trigger, decaying envelope.
  im.bassAvg += (rawBass - im.bassAvg) * std::min(1.0f, dt * 1.5f);
  if (rawBass > std::max(0.32f, im.bassAvg * 1.3f)) im.beatEnv = 1.0f;
  im.beatEnv = std::max(0.0f, im.beatEnv - dt * 2.6f);

  const float alpha = std::max(0.04f, 1.0f - smoothAmt * 0.96f);
  auto smooth = [&](float current, float raw) {
    float target = std::min(1.0f, raw * gain);
    return current + (target - current) * alpha;
  };
  im.lv.bass = smooth(im.lv.bass, rawBass);
  im.lv.mid = smooth(im.lv.mid, rawMid);
  im.lv.high = smooth(im.lv.high, rawHigh);
  im.lv.vol = smooth(im.lv.vol, rawVol);
  im.lv.beat = im.beatEnv;
}

const AudioLevels& AudioEngine::levels() const { return impl_->lv; }
std::string AudioEngine::lastError() const { return impl_->error; }
