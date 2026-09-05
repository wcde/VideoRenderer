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

#include <memory>
#include <string>
#include <d3d11_1.h>

// RIFE frame interpolation on TensorRT with D3D11 <-> CUDA interop.
//
// Usage on the rendering thread:
//   Init() -> SetFrameSize() -> [IsReady()] -> SetPair(prev, cur) -> Infer(t, out) ...
// The ONNX -> engine conversion (or the load from the on-disk cache) runs on a
// worker thread; until IsReady() returns true the caller presents source frames.
// The class depends only on a D3D11 device, never on the renderer, so the test
// tool can drive it as well.

enum class RifeState {
	Unloaded,  // Init() not called
	Loading,   // loading nvcuda / TensorRT DLLs, creating the CUDA context
	NoEngine,  // libraries ready, no frame size requested yet
	Building,  // engine being built or loaded from cache
	Ready,     // engine for the requested frame size available
	Failed,    // unrecoverable error, see GetStatusText()
};

struct RifeConfig {
	std::wstring onnxPath;
	std::wstring trtDir;             // directory with nvinfer_10.dll, empty = automatic search
	std::wstring cacheDir;           // engine cache, empty = %LOCALAPPDATA%\MPC-VR\trt
	bool fp16 = true;
	int padMultiple = 0;             // tensor size alignment, 0 = automatic (64, 128 for 4.25 lite)
	float sceneThreshold = 0.12f;    // minimum NVOFA motion-inlier ratio, 0 = off
};

class CRifeInterpolator
{
public:
	CRifeInterpolator();
	~CRifeInterpolator();

	CRifeInterpolator(const CRifeInterpolator&) = delete;
	CRifeInterpolator& operator=(const CRifeInterpolator&) = delete;

	// Creates the D3D11 objects and starts the worker thread. Fails only for
	// local problems (bad device, missing shader resources); library and CUDA
	// problems are reported through GetState()/GetStatusText().
	HRESULT Init(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, const RifeConfig& config, std::wstring& error);

	// Visible frame size. Recreates the tensors and requests an engine build
	// for the padded size. Safe to call repeatedly with the same size.
	HRESULT SetFrameSize(UINT width, UINT height);

	// True when an engine for the current frame size is available. Also picks
	// up an engine finished by the worker thread, so call it before SetPair().
	bool IsReady();
	void SetSceneThreshold(float threshold);

	RifeState GetState() const;
	std::wstring GetStatusText() const;
	std::wstring GetModelName() const;

	// Uploads the frame pair (channels 0-5) and evaluates NVOFA flow consistency.
	HRESULT SetPair(ID3D11ShaderResourceView* pPrev, ID3D11ShaderResourceView* pCur);
	bool IsSceneCut() const;

	// Runs the network for 'timestep' in (0,1) and writes the frame into
	// pOutput (render target of the visible frame size).
	HRESULT Infer(float timestep, ID3D11Texture2D* pOutput);

	double GetLastGpuMs() const;      // network time of a recent Infer(), measured with CUDA events
	float GetSceneThreshold() const;
	float GetLastSceneInlierRatio() const;
	float GetLastSceneMeanCost() const;
	std::wstring GetSceneDetectorStatus() const;
	UINT GetSceneCutCount() const;

	void Release();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
