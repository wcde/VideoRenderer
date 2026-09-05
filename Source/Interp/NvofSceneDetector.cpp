/*
 * (C) 2026 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include <algorithm>
#include <vector>
#include "Utils/StringUtil.h"
#include "nvOpticalFlowCommon.h"
#include "NvofSceneDetector.h"

namespace {

enum NV_OF_CUDA_BUFFER_TYPE {
	NV_OF_CUDA_BUFFER_TYPE_UNDEFINED,
	NV_OF_CUDA_BUFFER_TYPE_CUARRAY,
	NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR,
};

struct NV_OF_BUFFER_STRIDE {
	uint32_t strideXInBytes;
	uint32_t strideYInBytes;
};

struct NV_OF_CUDA_BUFFER_STRIDE_INFO {
	NV_OF_BUFFER_STRIDE strideInfo[3];
	uint32_t numPlanes;
};

using CreateOpticalFlowCudaFn = NV_OF_STATUS(NVOFAPI*)(cudrv::CUcontext, NvOFHandle*);
using CreateGpuBufferCudaFn = NV_OF_STATUS(NVOFAPI*)(NvOFHandle, const NV_OF_BUFFER_DESCRIPTOR*,
	NV_OF_CUDA_BUFFER_TYPE, NvOFGPUBufferHandle*);
using GetGpuBufferDevicePtrFn = cudrv::CUdeviceptr(NVOFAPI*)(NvOFGPUBufferHandle);
using GetGpuBufferStrideFn = NV_OF_STATUS(NVOFAPI*)(NvOFGPUBufferHandle, NV_OF_CUDA_BUFFER_STRIDE_INFO*);
using SetIoCudaStreamsFn = NV_OF_STATUS(NVOFAPI*)(NvOFHandle, cudrv::CUstream, cudrv::CUstream);
using DestroyGpuBufferCudaFn = NV_OF_STATUS(NVOFAPI*)(NvOFGPUBufferHandle);

struct NvofCudaApi {
	CreateOpticalFlowCudaFn createOpticalFlow;
	PFNNVOFINIT init;
	CreateGpuBufferCudaFn createBuffer;
	void* getArray;
	GetGpuBufferDevicePtrFn getDevicePtr;
	GetGpuBufferStrideFn getStride;
	SetIoCudaStreamsFn setStreams;
	PFNNVOFEXECUTE execute;
	DestroyGpuBufferCudaFn destroyBuffer;
	PFNNVOFDESTROY destroy;
	PFNNVOFGETLASTERROR getLastError;
	PFNNVOFGETCAPS getCaps;
};

using CreateApiFn = NV_OF_STATUS(NVOFAPI*)(uint32_t, NvofCudaApi*);
using MaxApiFn = NV_OF_STATUS(NVOFAPI*)(uint32_t*);

std::wstring StatusText(NV_OF_STATUS status)
{
	return std::format(L"NVOF error {}", static_cast<int>(status));
}

} // namespace

struct CNvofSceneDetector::Impl {
	const cudrv::Api* cu = nullptr;
	cudrv::CUcontext context = nullptr;
	cudrv::CUstream stream = nullptr;
	HMODULE module = nullptr;
	NvofCudaApi api = {};
	NvOFHandle handle = nullptr;
	NvOFGPUBufferHandle input[2] = {};
	NvOFGPUBufferHandle output[2] = {};
	NvOFGPUBufferHandle cost[2] = {};
	NV_OF_CUDA_BUFFER_STRIDE_INFO inputStride[2] = {};
	NV_OF_CUDA_BUFFER_STRIDE_INFO outputStride[2] = {};
	NV_OF_CUDA_BUFFER_STRIDE_INFO costStride[2] = {};
	unsigned width = 0;
	unsigned height = 0;
	unsigned flowWidth = 0;
	unsigned flowHeight = 0;
	~Impl() { Release(); }

	void Release()
	{
		if (context && cu) {
			cu->cuCtxSetCurrent(context);
			if (stream) {
				cu->cuStreamSynchronize(stream);
			}
		}
		if (api.destroyBuffer) {
			for (auto& buffer : input) {
				if (buffer) { api.destroyBuffer(buffer); buffer = nullptr; }
			}
			for (auto& buffer : output) {
				if (buffer) { api.destroyBuffer(buffer); buffer = nullptr; }
			}
			for (auto& buffer : cost) {
				if (buffer) { api.destroyBuffer(buffer); buffer = nullptr; }
			}
		}
		if (handle && api.destroy) {
			api.destroy(handle);
			handle = nullptr;
		}
		if (module) {
			FreeLibrary(module);
			module = nullptr;
		}
		api = {};
		cu = nullptr;
		context = nullptr;
		stream = nullptr;
		width = height = flowWidth = flowHeight = 0;
	}

	std::wstring LastError(NV_OF_STATUS status) const
	{
		char message[256] = {};
		uint32_t messageSize = sizeof(message);
		if (handle && api.getLastError && api.getLastError(handle, message, &messageSize) == NV_OF_SUCCESS && message[0]) {
			return std::format(L"{}: {}", StatusText(status), A2WStr(message));
		}
		return StatusText(status);
	}

	bool CreateBuffer(const NV_OF_BUFFER_DESCRIPTOR& desc, NvOFGPUBufferHandle& buffer,
		NV_OF_CUDA_BUFFER_STRIDE_INFO& stride, std::wstring& error)
	{
		NV_OF_STATUS status = api.createBuffer(handle, &desc, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &buffer);
		if (status == NV_OF_SUCCESS) {
			status = api.getStride(buffer, &stride);
		}
		if (status != NV_OF_SUCCESS || !api.getDevicePtr(buffer) || stride.numPlanes != 1) {
			error = status == NV_OF_SUCCESS ? L"NVOF returned an invalid CUDA buffer" : LastError(status);
			return false;
		}
		return true;
	}

	bool Copy2D(cudrv::CUdeviceptr src, size_t srcPitch, cudrv::CUdeviceptr dst, size_t dstPitch,
		size_t rowBytes, size_t rows, std::wstring& error) const
	{
		cudrv::CUDA_MEMCPY2D copy = {};
		copy.srcMemoryType = cudrv::CU_MEMORYTYPE_DEVICE;
		copy.srcDevice = src;
		copy.srcPitch = srcPitch;
		copy.dstMemoryType = cudrv::CU_MEMORYTYPE_DEVICE;
		copy.dstDevice = dst;
		copy.dstPitch = dstPitch;
		copy.WidthInBytes = rowBytes;
		copy.Height = rows;
		const cudrv::CUresult result = cu->cuMemcpy2DAsync(&copy, stream);
		if (result != cudrv::CUDA_SUCCESS) {
			error = L"cuMemcpy2DAsync() failed: " + cudrv::ErrStr(result);
			return false;
		}
		return true;
	}
};

CNvofSceneDetector::CNvofSceneDetector() = default;
CNvofSceneDetector::~CNvofSceneDetector() = default;

bool CNvofSceneDetector::Init(const cudrv::Api* cu, cudrv::CUcontext context, cudrv::CUstream stream,
	unsigned width, unsigned height, std::wstring& error)
{
	Release();
	if (!cu || !context || !stream || width < 32 || height < 32 || width > 8192 || height > 8192) {
		error = L"NVOFA requires a 32..8192 pixel frame and an initialized CUDA context";
		return false;
	}

	auto impl = std::make_unique<Impl>();
	impl->cu = cu;
	impl->context = context;
	impl->stream = stream;
	impl->width = width;
	impl->height = height;
	impl->flowWidth = (width + 3) / 4;
	impl->flowHeight = (height + 3) / 4;
	cu->cuCtxSetCurrent(context);

	impl->module = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!impl->module) {
		error = L"nvofapi64.dll not found (a current NVIDIA display driver is required)";
		return false;
	}
	const auto createApi = reinterpret_cast<CreateApiFn>(GetProcAddress(impl->module, "NvOFAPICreateInstanceCuda"));
	const auto maxApi = reinterpret_cast<MaxApiFn>(GetProcAddress(impl->module, "NvOFGetMaxSupportedApiVersion"));
	uint32_t driverVersion = 0;
	if (!createApi || !maxApi || maxApi(&driverVersion) != NV_OF_SUCCESS || driverVersion < NV_OF_API_VERSION) {
		error = L"NVIDIA Optical Flow API 5.0 is not supported by the installed driver";
		return false;
	}

	NV_OF_STATUS status = createApi(NV_OF_API_VERSION, &impl->api);
	if (status == NV_OF_SUCCESS) {
		status = impl->api.createOpticalFlow(context, &impl->handle);
	}
	if (status != NV_OF_SUCCESS) {
		error = impl->LastError(status);
		return false;
	}

	uint32_t capCount = 0;
	status = impl->api.getCaps(impl->handle, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, nullptr, &capCount);
	std::vector<uint32_t> grids(capCount);
	if (status == NV_OF_SUCCESS) {
		status = impl->api.getCaps(impl->handle, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, grids.data(), &capCount);
	}
	if (status != NV_OF_SUCCESS || std::find(grids.begin(), grids.end(), 4u) == grids.end()) {
		error = status == NV_OF_SUCCESS ? L"NVOFA 4x4 flow vectors are not supported" : impl->LastError(status);
		return false;
	}

	NV_OF_INIT_PARAMS init = {};
	init.width = width;
	init.height = height;
	init.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
	init.mode = NV_OF_MODE_OPTICALFLOW;
	init.perfLevel = NV_OF_PERF_LEVEL_MEDIUM;
	init.enableOutputCost = NV_OF_TRUE;
	init.predDirection = NV_OF_PRED_DIRECTION_BOTH;
	init.inputBufferFormat = NV_OF_BUFFER_FORMAT_GRAYSCALE8;
	status = impl->api.init(impl->handle, &init);
	if (status == NV_OF_SUCCESS) {
		status = impl->api.setStreams(impl->handle, stream, stream);
	}
	if (status != NV_OF_SUCCESS) {
		error = impl->LastError(status);
		return false;
	}

	NV_OF_BUFFER_DESCRIPTOR inputDesc = { width, height, NV_OF_BUFFER_USAGE_INPUT, NV_OF_BUFFER_FORMAT_GRAYSCALE8 };
	NV_OF_BUFFER_DESCRIPTOR flowDesc = { impl->flowWidth, impl->flowHeight, NV_OF_BUFFER_USAGE_OUTPUT, NV_OF_BUFFER_FORMAT_SHORT2 };
	NV_OF_BUFFER_DESCRIPTOR costDesc = { impl->flowWidth, impl->flowHeight, NV_OF_BUFFER_USAGE_COST, NV_OF_BUFFER_FORMAT_UINT8 };
	for (unsigned i = 0; i < 2; i++) {
		if (!impl->CreateBuffer(inputDesc, impl->input[i], impl->inputStride[i], error)
				|| !impl->CreateBuffer(flowDesc, impl->output[i], impl->outputStride[i], error)
				|| !impl->CreateBuffer(costDesc, impl->cost[i], impl->costStride[i], error)) {
			return false;
		}
	}

	m_impl = std::move(impl);
	return true;
}

bool CNvofSceneDetector::Execute(cudrv::CUdeviceptr luma, size_t lumaPitch, size_t lumaPlaneBytes,
	cudrv::CUdeviceptr flow, size_t flowPitch, size_t flowPlaneBytes,
	cudrv::CUdeviceptr cost, size_t costPitch, size_t costPlaneBytes, std::wstring& error)
{
	if (!m_impl) {
		error = L"NVOFA is not initialized";
		return false;
	}
	Impl& d = *m_impl;
	d.cu->cuCtxSetCurrent(d.context);

	for (unsigned i = 0; i < 2; i++) {
		if (!d.Copy2D(luma + i * lumaPlaneBytes, lumaPitch, d.api.getDevicePtr(d.input[i]),
				d.inputStride[i].strideInfo[0].strideXInBytes, d.width, d.height, error)) {
			error += std::format(L" (input {}, src pitch {}, dst strides {}:{}, {}x{})", i, lumaPitch,
				d.inputStride[i].strideInfo[0].strideXInBytes, d.inputStride[i].strideInfo[0].strideYInBytes,
				d.width, d.height);
			return false;
		}
	}

	NV_OF_EXECUTE_INPUT_PARAMS in = {};
	in.inputFrame = d.input[0];
	in.referenceFrame = d.input[1];
	// This detector evaluates each pair independently so a cut cannot poison the
	// following pair through NVOFA's temporal hint state.
	in.disableTemporalHints = NV_OF_TRUE;
	NV_OF_EXECUTE_OUTPUT_PARAMS out = {};
	out.outputBuffer = d.output[0];
	out.outputCostBuffer = d.cost[0];
	out.bwdOutputBuffer = d.output[1];
	out.bwdOutputCostBuffer = d.cost[1];
	const NV_OF_STATUS status = d.api.execute(d.handle, &in, &out);
	if (status != NV_OF_SUCCESS) {
		error = d.LastError(status);
		return false;
	}

	for (unsigned i = 0; i < 2; i++) {
		if (!d.Copy2D(d.api.getDevicePtr(d.output[i]), d.outputStride[i].strideInfo[0].strideXInBytes,
				flow + i * flowPlaneBytes, flowPitch, d.flowWidth * 4ull, d.flowHeight, error)
				|| !d.Copy2D(d.api.getDevicePtr(d.cost[i]), d.costStride[i].strideInfo[0].strideXInBytes,
				cost + i * costPlaneBytes, costPitch, d.flowWidth, d.flowHeight, error)) {
			error += std::format(L" (output {}, flow strides {}:{}, cost strides {}:{})", i,
				d.outputStride[i].strideInfo[0].strideXInBytes, d.outputStride[i].strideInfo[0].strideYInBytes,
				d.costStride[i].strideInfo[0].strideXInBytes, d.costStride[i].strideInfo[0].strideYInBytes);
			return false;
		}
	}
	return true;
}

unsigned CNvofSceneDetector::FlowWidth() const
{
	return m_impl ? m_impl->flowWidth : 0;
}

unsigned CNvofSceneDetector::FlowHeight() const
{
	return m_impl ? m_impl->flowHeight : 0;
}

void CNvofSceneDetector::Release()
{
	if (m_impl) {
		m_impl->Release();
		m_impl.reset();
	}
}
