# MPC Video Renderer — RIFE / TensorRT

Unofficial [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer) fork with real-time RIFE frame interpolation for Direct3D 11 (x64, NVIDIA). Supports [MPC-BE](https://github.com/Aleksoid1978/MPC-BE) and other DirectShow players.

* Fixed x2/x3/x4 interpolation, capped by display refresh, or display-refresh output (e.g. 24 fps → 60 Hz).
* Resolution profiles with separate output modes and model overrides.
* GPU selection, NVOFA scene-cut detection and on-screen statistics.
* Background TensorRT engine builds with disk caching.

## Requirements

* Windows 10+, x64 renderer in Direct3D 11 mode, NVIDIA Turing-or-newer GPU with NVOFA and a current driver.
* TensorRT 10.x: `nvinfer_10.dll`, `nvonnxparser_10.dll`, `nvinfer_builder_resource_10.dll`.
* A [vs-mlrt RIFE ONNX model](https://github.com/AmusementClub/vs-mlrt/releases) (`rife v1` interface).

Runtime DLLs and models are not bundled. The CUDA Toolkit is not required.

## Setup

Open **Frame interpolation** in renderer settings. Select the GPU, TensorRT DLL directory, ONNX model and output mode. Alternatively, put DLLs in `TensorRT` next to `MpcVideoRenderer64.ax`. Reopen playback after changing the GPU.

Resolution profiles match the nearest source pixel count; an empty model path inherits the default model. Initial profiles use x2 for 720p/1080p/1440p and disable interpolation for 2160p.

The first engine build at each frame size may take several minutes; playback continues without interpolation. Cache: `%LOCALAPPDATA%\MPC-VR\trt`.

## Build

Requires Visual Studio C++ tools with ATL and a Windows SDK with `fxc.exe`. From a developer command prompt in the repository:

```bat
git submodule update --init --recursive
MSBuild.exe MpcVideoRenderer.sln -p:Configuration=Release -p:Platform=x64 -p:UseOfMfc=false
```

Output: `_bin/Filter_x64/MpcVideoRenderer64.ax`. Use `Platform=x86` for the 32-bit renderer.

## License

[GPL v3](LICENSE.txt), based on MPC Video Renderer. Third-party notices: [TensorRT](external/tensorrt) (Apache-2.0) and [NVIDIA Optical Flow API](external/nvofapi). Runtime DLLs and model weights have their own licenses.
