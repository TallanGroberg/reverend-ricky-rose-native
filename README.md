# Reverend Ricky Rosè's Dance Party Visuals: Native CUDA Edition

The full-parity native build of the dance party fractal app. The Electron
build draws the fractal with a WebGL shader; this build runs the same
mathematics as a CUDA kernel on the GPU (one thread per pixel, CUDA/OpenGL
interop, no per-frame CPU copies), with the controls in Dear ImGui, the
microphone on WASAPI, and exports carved from a GPU-evaluated field.

Everything cross-loads with the web builds: same slider ids in settings
files, same GLB recipe embedded in exports, same seed growing the same
branching skeleton.

## Requirements

- Windows 10 or 11
- NVIDIA RTX 5090 (Blackwell, sm_120) with a current driver
- That is all: the zip is one exe, no installer, no runtime to install

## Run

Extract the zip, run the exe. F key or the Full screen button fills the
display; Esc comes back. Start microphone, pick an input if the default
is not the one your music plays through, and map bands to sliders.

Useful self-checks on a new machine:

    "Reverend Ricky Rosè's Dance Party Visuals.exe" --carve-test
    "Reverend Ricky Rosè's Dance Party Visuals.exe" --snapshot frame.ppm

The first carves the default fractal on the GPU and prints a triangle
count (expect several thousand). The second writes a 960x540 PPM frame
with default settings, for comparison against the web build.

## Build (developers)

Visual Studio 2022 and CUDA Toolkit 12.8 or newer. CMake fetches GLFW
and Dear ImGui; everything else is in src/.

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release

GitHub Actions (.github/workflows/build.yml) does the same on a Windows
runner for every v* tag and attaches the zip to the release. Hosted
runners have no GPU, so CI proves compilation; running is verified on
the 5090.
