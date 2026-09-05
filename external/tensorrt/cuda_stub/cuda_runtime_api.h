// Minimal stand-in for the CUDA runtime header.
// The public TensorRT headers only need these two opaque handle types, so the
// renderer can be built without a CUDA toolkit. The typedefs are identical to
// the ones in the real cuda_runtime_api.h / cuda.h (CUstream / CUevent).
#pragma once

typedef struct CUstream_st* cudaStream_t;
typedef struct CUevent_st* cudaEvent_t;
