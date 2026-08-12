# Building Vesper

## Requirements

- CMake ≥ 3.24, a C++20 compiler, git
- **Windows:** Visual Studio 2022 Build Tools ("Desktop development with C++")
- **macOS:** Xcode 15+ (`xcode-select --install`)
- **Linux:** gcc 12+/clang 15+ and the JUCE deps:
  `sudo apt-get install libasound2-dev libjack-jackd2-dev libx11-dev
  libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev
  libxrender-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev
  libcurl4-openssl-dev`

JUCE 7.0.12 is pulled by FetchContent on first configure — no manual SDK
setup. (Pinned to 7.0.12: the last line that builds under MinGW-w64; JUCE 8's
Direct2D renderer requires MSVC. The MSVC/Xcode CI path can move to 8.x
independently.)

## Build & test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Artifacts land in `build/Vesper_artefacts/Release/{VST3,AU,Standalone}`.

### Windows quick start (fresh machine)

```powershell
winget install Kitware.CMake Microsoft.VisualStudio.2022.BuildTools
# In the VS installer, add the "Desktop development with C++" workload (needs admin).
```

### Windows without admin rights (portable toolchain)

This repo also builds with a fully portable, no-elevation toolchain in
`tools/` (not committed): a CMake release zip, Ninja, and winlibs MinGW-w64
GCC (UCRT, POSIX threads — from winlibs.com). Then:

```bash
export PATH="$PWD/tools/mingw64/bin:$PWD/tools/ninja:$PWD/tools/cmake-3.31.6-windows-x86_64/bin:$PATH"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel
```

The MinGW build links the runtime statically (`-static`), so the produced
`Vesper.vst3` carries no compiler-DLL dependencies. CI (GitHub Actions)
remains the canonical MSVC/Xcode release path and additionally runs pluginval.

## Installing locally

- **Windows:** copy `Vesper.vst3` → `C:\Program Files\Common Files\VST3\`,
  or build the installer: `iscc installer\windows\Vesper.iss`
- **macOS:** `installer/macos/build_pkg.sh`, or copy to
  `~/Library/Audio/Plug-Ins/{VST3,Components}`
- **Linux:** copy to `~/.vst3/`

Factory presets self-extract from the binary on first run to
`<userAppData>/Sable Audio/Vesper/Presets/Factory`.

## Options

| CMake option | Default | |
|---|---|---|
| `VESPER_BUILD_TESTS` | `ON` | build + register the `VesperTests` ctest target |

## AAX

Requires the Avid SDK (NDA) + PACE signing. Add `AAX` to `FORMATS` in
`CMakeLists.txt` and set `JUCE_AAX_SDK_PATH`. Unsigned AAX loads only in
Pro Tools Developer builds.
