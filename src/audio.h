#pragma once
// WASAPI microphone engine for the native CUDA build of
// Reverend Ricky Rose's Dance Party Visuals.
//
// Mirrors the web build's audio reactive section: a 2048 point FFT with
// a Blackman window, byte-style band levels for bass (25-160 Hz), mids
// (160-2000 Hz) and treble (4000-12000 Hz), time-domain RMS volume, a
// bass-driven beat detector, and per-call smoothing. All microphone
// audio stays on this machine; nothing is recorded or uploaded.

#include <memory>
#include <string>
#include <vector>

struct AudioLevels {
  float bass = 0, mid = 0, high = 0, vol = 0, beat = 0;
};

class AudioEngine {
 public:
  AudioEngine();
  ~AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  // COM and the device enumerator. Safe to call more than once.
  bool init();
  // Friendly names of active capture endpoints, UTF-8 encoded.
  std::vector<std::string> deviceNames();
  // Start capturing. deviceIndex -1 means the system default input,
  // otherwise an index into deviceNames(). Stops any current capture.
  bool start(int deviceIndex);
  void stop();
  bool running() const;
  // Call once per frame. Consumes whatever the capture thread has
  // gathered, refreshes the FFT and levels. gain ~1.4 (sensitivity),
  // smoothAmt 0..0.95 (the web build's smoothing slider fraction).
  // dt is used only for the beat envelope and bass rolling average.
  void poll(float dt, float gain, float smoothAmt);
  const AudioLevels& levels() const;
  std::string lastError() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
