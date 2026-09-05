# TensorRT public headers (vendored)

`include/` contains the public TensorRT 10.0.1 API headers, copied unmodified from
https://github.com/NVIDIA/TensorRT (tag `v10.0.1`, directory `include/`) and
`NvOnnxParser.h` from https://github.com/onnx/onnx-tensorrt (branch `release/10.0-GA`).
Both are licensed under the Apache License 2.0, see `LICENSE`.

The oldest 10.x headers are used on purpose: TensorRT keeps applications built
against older 10.x headers working with any newer `nvinfer_10.dll`, but not the
other way round. Only API that exists since 10.0 may be used from the renderer.

`cuda_stub/cuda_runtime_api.h` replaces the CUDA toolkit header that
`NvInferRuntimeBase.h` includes; the TensorRT headers only need the
`cudaStream_t` / `cudaEvent_t` typedefs from it.

The DLLs (`nvinfer_10.dll`, `nvinfer_builder_resource_10.dll`, `nvonnxparser_10.dll`)
are not part of this repository. They are loaded at runtime, see
`Source/Interp/TensorRTLoader.cpp`.
