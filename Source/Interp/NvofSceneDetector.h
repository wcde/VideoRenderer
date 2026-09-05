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

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include "CudaDriver.h"

// CUDA front end for the NVIDIA Optical Flow Accelerator. The driver owns the
// actual NVOFA implementation (nvofapi64.dll); no SDK or CUDA toolkit DLL is
// redistributed with the renderer.
class CNvofSceneDetector
{
public:
	CNvofSceneDetector();
	~CNvofSceneDetector();

	CNvofSceneDetector(const CNvofSceneDetector&) = delete;
	CNvofSceneDetector& operator=(const CNvofSceneDetector&) = delete;

	bool Init(const cudrv::Api* cu, cudrv::CUcontext context, cudrv::CUstream stream,
		unsigned width, unsigned height, std::wstring& error);

	// luma contains two tightly described 8-bit planes. flow and cost receive
	// forward and backward maps in two consecutive planes. All pointers are
	// CUDA mappings of D3D11 buffers owned by the caller.
	bool Execute(cudrv::CUdeviceptr luma, size_t lumaPitch, size_t lumaPlaneBytes,
		cudrv::CUdeviceptr flow, size_t flowPitch, size_t flowPlaneBytes,
		cudrv::CUdeviceptr cost, size_t costPitch, size_t costPlaneBytes,
		std::wstring& error);

	unsigned FlowWidth() const;
	unsigned FlowHeight() const;
	void Release();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
