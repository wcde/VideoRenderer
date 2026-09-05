/*
 * (C) 2026 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

// Minimal binding of the CUDA driver API (nvcuda.dll, shipped with the display
// driver). Declared here instead of including cuda.h so that no CUDA toolkit is
// needed to build the renderer. Signatures match cuda.h / cudaD3D11.h.

#include <cstdint>
#include <string>

struct IDXGIAdapter;
struct ID3D11Resource;
struct CUctx_st;
struct CUstream_st;
struct CUevent_st;
struct CUarray_st;
struct CUgraphicsResource_st;

namespace cudrv {

using CUresult = int;
using CUdevice = int;
using CUdeviceptr = unsigned long long;
using CUcontext = ::CUctx_st*;
using CUstream = ::CUstream_st*;
using CUevent = ::CUevent_st*;
using CUarray = ::CUarray_st*;
using CUgraphicsResource = ::CUgraphicsResource_st*;

constexpr CUresult CUDA_SUCCESS = 0;
constexpr CUresult CUDA_ERROR_NOT_READY = 600;

constexpr unsigned CU_STREAM_NON_BLOCKING = 0x1;
constexpr unsigned CU_EVENT_DEFAULT = 0x0;
constexpr unsigned CU_EVENT_DISABLE_TIMING = 0x2;
constexpr unsigned CU_GRAPHICS_REGISTER_FLAGS_NONE = 0x0;
constexpr unsigned CU_GRAPHICS_MAP_RESOURCE_FLAGS_NONE = 0x0;
constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75;
constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76;

enum CUmemorytype : unsigned {
	CU_MEMORYTYPE_HOST = 1,
	CU_MEMORYTYPE_DEVICE = 2,
	CU_MEMORYTYPE_ARRAY = 3,
	CU_MEMORYTYPE_UNIFIED = 4,
};

// ABI-compatible subset of CUDA_MEMCPY2D from cuda.h.
struct CUDA_MEMCPY2D {
	size_t srcXInBytes;
	size_t srcY;
	CUmemorytype srcMemoryType;
	const void* srcHost;
	CUdeviceptr srcDevice;
	CUarray srcArray;
	size_t srcPitch;
	size_t dstXInBytes;
	size_t dstY;
	CUmemorytype dstMemoryType;
	void* dstHost;
	CUdeviceptr dstDevice;
	CUarray dstArray;
	size_t dstPitch;
	size_t WidthInBytes;
	size_t Height;
};

struct Api {
	CUresult (__stdcall* cuInit)(unsigned flags);
	CUresult (__stdcall* cuDriverGetVersion)(int* version);
	CUresult (__stdcall* cuGetErrorString)(CUresult error, const char** str);
	CUresult (__stdcall* cuDeviceGetName)(char* name, int len, CUdevice dev);
	CUresult (__stdcall* cuDeviceGetAttribute)(int* value, int attrib, CUdevice dev);
	CUresult (__stdcall* cuD3D11GetDevice)(CUdevice* dev, IDXGIAdapter* adapter);
	CUresult (__stdcall* cuDevicePrimaryCtxRetain)(CUcontext* ctx, CUdevice dev);
	CUresult (__stdcall* cuDevicePrimaryCtxRelease)(CUdevice dev);
	CUresult (__stdcall* cuCtxSetCurrent)(CUcontext ctx);
	CUresult (__stdcall* cuCtxGetCurrent)(CUcontext* ctx);
	CUresult (__stdcall* cuMemGetInfo)(size_t* free, size_t* total);
	CUresult (__stdcall* cuStreamCreate)(CUstream* stream, unsigned flags);
	CUresult (__stdcall* cuStreamDestroy)(CUstream stream);
	CUresult (__stdcall* cuStreamSynchronize)(CUstream stream);
	CUresult (__stdcall* cuEventCreate)(CUevent* event, unsigned flags);
	CUresult (__stdcall* cuEventDestroy)(CUevent event);
	CUresult (__stdcall* cuEventRecord)(CUevent event, CUstream stream);
	CUresult (__stdcall* cuEventQuery)(CUevent event);
	CUresult (__stdcall* cuEventSynchronize)(CUevent event);
	CUresult (__stdcall* cuEventElapsedTime)(float* ms, CUevent start, CUevent end);
	CUresult (__stdcall* cuMemcpy2DAsync)(const CUDA_MEMCPY2D* copy, CUstream stream);
	CUresult (__stdcall* cuGraphicsD3D11RegisterResource)(CUgraphicsResource* resource, ID3D11Resource* d3dResource, unsigned flags);
	CUresult (__stdcall* cuGraphicsUnregisterResource)(CUgraphicsResource resource);
	CUresult (__stdcall* cuGraphicsResourceSetMapFlags)(CUgraphicsResource resource, unsigned flags);
	CUresult (__stdcall* cuGraphicsMapResources)(unsigned count, CUgraphicsResource* resources, CUstream stream);
	CUresult (__stdcall* cuGraphicsUnmapResources)(unsigned count, CUgraphicsResource* resources, CUstream stream);
	CUresult (__stdcall* cuGraphicsResourceGetMappedPointer)(CUdeviceptr* ptr, size_t* size, CUgraphicsResource resource);
};

// Loads nvcuda.dll once per process and calls cuInit(). Returns nullptr and
// fills 'error' when the driver is missing or too old. Thread-safe.
const Api* Load(std::wstring& error);

std::wstring ErrStr(CUresult result);

} // namespace cudrv
