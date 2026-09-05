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

#include "stdafx.h"
#include <mutex>
#include "Utils/Util.h"
#include "Utils/StringUtil.h"
#include "CudaDriver.h"

namespace cudrv {

namespace {

struct Loader {
	Api api = {};
	bool ok = false;
	std::wstring error;

	template <typename T>
	bool Resolve(HMODULE hModule, T& fn, const char* name, const char* fallbackName = nullptr)
	{
		fn = reinterpret_cast<T>(GetProcAddress(hModule, name));
		if (!fn && fallbackName) {
			fn = reinterpret_cast<T>(GetProcAddress(hModule, fallbackName));
		}
		if (!fn) {
			error = std::format(L"nvcuda.dll does not export {}", A2WStr(name));
			return false;
		}
		return true;
	}

	Loader()
	{
		const HMODULE hModule = LoadLibraryW(L"nvcuda.dll");
		if (!hModule) {
			error = L"nvcuda.dll not found (NVIDIA display driver is required)";
			return;
		}

		// Versioned export names follow the #define mapping in cuda.h.
		ok = Resolve(hModule, api.cuInit, "cuInit")
			&& Resolve(hModule, api.cuDriverGetVersion, "cuDriverGetVersion")
			&& Resolve(hModule, api.cuGetErrorString, "cuGetErrorString")
			&& Resolve(hModule, api.cuDeviceGetName, "cuDeviceGetName")
			&& Resolve(hModule, api.cuDeviceGetAttribute, "cuDeviceGetAttribute")
			&& Resolve(hModule, api.cuD3D11GetDevice, "cuD3D11GetDevice")
			&& Resolve(hModule, api.cuDevicePrimaryCtxRetain, "cuDevicePrimaryCtxRetain")
			&& Resolve(hModule, api.cuDevicePrimaryCtxRelease, "cuDevicePrimaryCtxRelease_v2")
			&& Resolve(hModule, api.cuCtxSetCurrent, "cuCtxSetCurrent")
			&& Resolve(hModule, api.cuCtxGetCurrent, "cuCtxGetCurrent")
			&& Resolve(hModule, api.cuMemGetInfo, "cuMemGetInfo_v2")
			&& Resolve(hModule, api.cuStreamCreate, "cuStreamCreate")
			&& Resolve(hModule, api.cuStreamDestroy, "cuStreamDestroy_v2")
			&& Resolve(hModule, api.cuStreamSynchronize, "cuStreamSynchronize")
			&& Resolve(hModule, api.cuEventCreate, "cuEventCreate")
			&& Resolve(hModule, api.cuEventDestroy, "cuEventDestroy_v2")
			&& Resolve(hModule, api.cuEventRecord, "cuEventRecord")
			&& Resolve(hModule, api.cuEventQuery, "cuEventQuery")
			&& Resolve(hModule, api.cuEventSynchronize, "cuEventSynchronize")
			// cuEventElapsedTime_v2 appeared in CUDA 12.8 drivers; older drivers only have the original export.
			&& Resolve(hModule, api.cuEventElapsedTime, "cuEventElapsedTime_v2", "cuEventElapsedTime")
			&& Resolve(hModule, api.cuMemcpy2DAsync, "cuMemcpy2DAsync_v2")
			&& Resolve(hModule, api.cuGraphicsD3D11RegisterResource, "cuGraphicsD3D11RegisterResource")
			&& Resolve(hModule, api.cuGraphicsUnregisterResource, "cuGraphicsUnregisterResource")
			&& Resolve(hModule, api.cuGraphicsResourceSetMapFlags, "cuGraphicsResourceSetMapFlags_v2")
			&& Resolve(hModule, api.cuGraphicsMapResources, "cuGraphicsMapResources")
			&& Resolve(hModule, api.cuGraphicsUnmapResources, "cuGraphicsUnmapResources")
			&& Resolve(hModule, api.cuGraphicsResourceGetMappedPointer, "cuGraphicsResourceGetMappedPointer_v2");
		if (!ok) {
			return;
		}

		const CUresult res = api.cuInit(0);
		if (res != CUDA_SUCCESS) {
			ok = false;
			error = L"cuInit() failed: " + ErrStr(res);
		}
	}
};

Loader& GetLoader()
{
	static Loader loader;
	return loader;
}

} // namespace

const Api* Load(std::wstring& error)
{
	Loader& loader = GetLoader();
	if (!loader.ok) {
		error = loader.error;
		return nullptr;
	}
	return &loader.api;
}

std::wstring ErrStr(CUresult result)
{
	Loader& loader = GetLoader();
	const char* str = nullptr;
	if (loader.api.cuGetErrorString && loader.api.cuGetErrorString(result, &str) == CUDA_SUCCESS && str) {
		return std::format(L"{} (CUDA error {})", A2WStr(str), result);
	}
	return std::format(L"CUDA error {}", result);
}

} // namespace cudrv
