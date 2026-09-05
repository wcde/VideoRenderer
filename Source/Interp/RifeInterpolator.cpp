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
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <NvInfer.h>
#include <NvOnnxParser.h>
#include "Utils/Util.h"
#include "Utils/StringUtil.h"
#include "Times.h"
#include "resource.h"
#include "CudaDriver.h"
#include "NvofSceneDetector.h"
#include "TensorRTLoader.h"
#include "RifeInterpolator.h"

namespace {

// Matches cbuffer PARAMS in cs_rife_pack / cs_rife_scene / ps_rife_unpack.
struct ShaderParams {
	UINT width;
	UINT height;
	UINT paddedWidth;
	UINT paddedHeight;
	float timestep;
	UINT mode;
	UINT flowWidth;
	UINT lumaPitch;
};

constexpr UINT PACK_IMAGES   = 1;
constexpr UINT PACK_TIMESTEP = 2;
constexpr UINT PACK_GRID     = 4;

constexpr UINT INPUT_CHANNELS  = 11;
constexpr UINT OUTPUT_CHANNELS = 3;

constexpr unsigned CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY = 0x1;
constexpr unsigned CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD = 0x2;

class CTrtLogger final : public nvinfer1::ILogger
{
public:
	void log(Severity severity, const char* msg) noexcept override
	{
		if (severity <= Severity::kERROR) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lastError = A2WStr(msg);
		}
		if (severity <= Severity::kINFO) {
			DLog(L"TensorRT: {}", A2WStr(msg));
		}
	}

	std::wstring LastError()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_lastError;
	}

private:
	std::mutex m_mutex;
	std::wstring m_lastError;
};

class CBuildMonitor final : public nvinfer1::IProgressMonitor
{
public:
	explicit CBuildMonitor(std::atomic<bool>& cancel) : m_cancel(cancel) {}

	void phaseStart(const char*, const char*, int32_t) noexcept override {}
	bool stepComplete(const char*, int32_t) noexcept override { return !m_cancel; }
	void phaseFinish(const char*) noexcept override {}

private:
	std::atomic<bool>& m_cancel;
};

struct Engine {
	std::unique_ptr<nvinfer1::ICudaEngine> engine;
	std::unique_ptr<nvinfer1::IExecutionContext> context;
	std::string inputName;
	std::string outputName;
	UINT paddedWidth = 0;
	UINT paddedHeight = 0;
	bool fromCache = false;
};

bool ReadWholeFile(const std::filesystem::path& path, std::vector<char>& data)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		return false;
	}
	const auto size = file.tellg();
	if (size <= 0) {
		return false;
	}
	data.resize(static_cast<size_t>(size));
	file.seekg(0);
	return static_cast<bool>(file.read(data.data(), size));
}

bool WriteFileAtomic(const std::filesystem::path& path, const void* data, size_t size)
{
	std::filesystem::path tmp = path;
	tmp += L".tmp";
	{
		std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
		if (!file || !file.write(static_cast<const char*>(data), size)) {
			return false;
		}
	}
	return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

std::wstring SanitizeName(std::wstring str)
{
	for (auto& ch : str) {
		if (!iswalnum(ch)) {
			ch = L'_';
		}
	}
	return str;
}

uint64_t FileIdentityHash(const std::filesystem::path& path)
{
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	const auto time = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
	uint64_t hash = 14695981039346656037ull;
	for (const uint64_t value : { static_cast<uint64_t>(size), static_cast<uint64_t>(time) }) {
		for (int i = 0; i < 8; i++) {
			hash ^= (value >> (i * 8)) & 0xFF;
			hash *= 1099511628211ull;
		}
	}
	return hash;
}

std::wstring DefaultCacheDir()
{
	wchar_t buf[MAX_PATH] = {};
	const DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
	if (len && len < MAX_PATH) {
		return std::wstring(buf) + L"\\MPC-VR\\trt";
	}
	return trtload::GetModuleDir() + L"trt-cache";
}

std::wstring StateName(RifeState state)
{
	switch (state) {
	case RifeState::Unloaded: return L"not initialized";
	case RifeState::Loading:  return L"loading";
	case RifeState::NoEngine: return L"waiting for frame size";
	case RifeState::Building: return L"building engine";
	case RifeState::Ready:    return L"ready";
	case RifeState::Failed:   return L"failed";
	}
	return L"";
}

} // namespace

struct CRifeInterpolator::Impl
{
	// D3D11
	CComPtr<ID3D11Device> device;
	CComPtr<ID3D11DeviceContext> context;
	CComPtr<ID3D11ComputeShader> csPack;
	CComPtr<ID3D11ComputeShader> csScene;
	CComPtr<ID3D11PixelShader> psUnpack;
	CComPtr<ID3D11VertexShader> vsQuad;
	CComPtr<ID3D11Buffer> cbParams;
	CComPtr<ID3D11Buffer> bufIn;
	CComPtr<ID3D11Buffer> bufOut;
	CComPtr<ID3D11Buffer> bufNvofLuma;
	CComPtr<ID3D11Buffer> bufNvofFlow;
	CComPtr<ID3D11Buffer> bufNvofCost;
	CComPtr<ID3D11Buffer> bufScene;
	CComPtr<ID3D11Buffer> bufSceneStaging;
	CComPtr<ID3D11UnorderedAccessView> uavIn;
	CComPtr<ID3D11UnorderedAccessView> uavNvofLuma;
	CComPtr<ID3D11UnorderedAccessView> uavScene;
	CComPtr<ID3D11ShaderResourceView> srvOut;
	CComPtr<ID3D11ShaderResourceView> srvNvofFlow;
	CComPtr<ID3D11ShaderResourceView> srvNvofCost;
	CComPtr<ID3D11Texture2D> lastOutput;
	CComPtr<ID3D11RenderTargetView> lastOutputRTV;

	RifeConfig config;
	std::wstring modelName;
	UINT padMultiple = 64;
	UINT width = 0;
	UINT height = 0;
	UINT paddedWidth = 0;
	UINT paddedHeight = 0;
	UINT flowWidth = 0;
	UINT flowHeight = 0;
	UINT lumaPitch = 0;
	UINT costPitch = 0;
	bool gridDirty = true;

	// CUDA
	const cudrv::Api* cu = nullptr;
	cudrv::CUdevice cudaDevice = -1;
	cudrv::CUcontext cudaContext = nullptr;
	cudrv::CUstream stream = nullptr;
	cudrv::CUevent evStart[2] = {};
	cudrv::CUevent evEnd[2] = {};
	int evIndex = 0;
	bool evPending[2] = {};
	std::atomic<bool> cudaReady{ false };
	cudrv::CUgraphicsResource resIn = nullptr;
	cudrv::CUgraphicsResource resOut = nullptr;
	cudrv::CUgraphicsResource resNvofLuma = nullptr;
	cudrv::CUgraphicsResource resNvofFlow = nullptr;
	cudrv::CUgraphicsResource resNvofCost = nullptr;
	CNvofSceneDetector nvof;
	bool nvofAttempted = false;
	bool nvofAvailable = false;
	std::wstring nvofStatus = L"not initialized";
	std::wstring gpuName;
	int smMajor = 0;
	int smMinor = 0;

	// TensorRT
	CTrtLogger logger;
	std::unique_ptr<nvinfer1::IRuntime> runtime;
	std::unique_ptr<Engine> engine;        // used by the rendering thread only
	std::unique_ptr<Engine> pendingEngine; // handed over by the worker, guarded by mutex

	// worker thread
	std::thread worker;
	std::mutex mutex;
	std::condition_variable cv;
	bool quit = false;
	bool requestPending = false;
	UINT requestWidth = 0;
	UINT requestHeight = 0;
	std::atomic<bool> cancel{ false };
	std::atomic<RifeState> state{ RifeState::Unloaded };
	std::wstring statusText;
	uint64_t buildStartTick = 0;

	// statistics
	double lastGpuMs = 0.0;
	float lastSceneInlierRatio = 1.0f;
	float lastSceneMeanCost = 0.0f;
	bool lastSceneCut = false;
	UINT sceneCuts = 0;

	~Impl() { Release(); }

	void SetStatus(RifeState newState, std::wstring text)
	{
		std::lock_guard<std::mutex> lock(mutex);
		state = newState;
		statusText = std::move(text);
	}

	HRESULT CreateShaders()
	{
		struct {
			UINT resid;
			HRESULT (Impl::*create)(const void*, SIZE_T);
		} const shaders[] = {
			{ IDF_CS_11_RIFE_PACK,   &Impl::CreatePackShader },
			{ IDF_CS_11_RIFE_SCENE,  &Impl::CreateSceneShader },
			{ IDF_PS_11_RIFE_UNPACK, &Impl::CreateUnpackShader },
			{ IDF_VS_11_RIFE_QUAD,   &Impl::CreateQuadShader },
		};
		for (const auto& shader : shaders) {
			LPVOID data = nullptr;
			DWORD size = 0;
			HRESULT hr = GetDataFromResource(data, size, shader.resid);
			if (SUCCEEDED(hr)) {
				hr = (this->*shader.create)(data, size);
			}
			if (FAILED(hr)) {
				return hr;
			}
		}

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(ShaderParams);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		return device->CreateBuffer(&desc, nullptr, &cbParams);
	}

	HRESULT CreatePackShader(const void* data, SIZE_T size)   { return device->CreateComputeShader(data, size, nullptr, &csPack); }
	HRESULT CreateSceneShader(const void* data, SIZE_T size)  { return device->CreateComputeShader(data, size, nullptr, &csScene); }
	HRESULT CreateUnpackShader(const void* data, SIZE_T size) { return device->CreatePixelShader(data, size, nullptr, &psUnpack); }
	HRESULT CreateQuadShader(const void* data, SIZE_T size)   { return device->CreateVertexShader(data, size, nullptr, &vsQuad); }

	HRESULT CreateRawBuffer(UINT byteWidth, ID3D11Buffer** ppBuffer, ID3D11UnorderedAccessView** ppUAV, ID3D11ShaderResourceView** ppSRV)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = byteWidth;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		HRESULT hr = device->CreateBuffer(&desc, nullptr, ppBuffer);
		if (FAILED(hr)) {
			return hr;
		}

		if (ppUAV) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.NumElements = byteWidth / 4;
			uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			hr = device->CreateUnorderedAccessView(*ppBuffer, &uavDesc, ppUAV);
			if (FAILED(hr)) {
				return hr;
			}
		}
		if (ppSRV) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			srvDesc.BufferEx.NumElements = byteWidth / 4;
			srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
			hr = device->CreateShaderResourceView(*ppBuffer, &srvDesc, ppSRV);
		}
		return hr;
	}

	void UpdateParams(float timestep, UINT mode)
	{
		const ShaderParams params = {
			width, height, paddedWidth, paddedHeight,
			timestep, mode,
			flowWidth,
			lumaPitch,
		};
		context->UpdateSubresource(cbParams, 0, nullptr, &params, 0, 0);
	}

	void ReleaseNvofBuffers()
	{
		nvof.Release();
		if (cu && cudaContext) {
			cu->cuCtxSetCurrent(cudaContext);
			if (resNvofLuma) {
				cu->cuGraphicsUnregisterResource(resNvofLuma);
				resNvofLuma = nullptr;
			}
			if (resNvofFlow) {
				cu->cuGraphicsUnregisterResource(resNvofFlow);
				resNvofFlow = nullptr;
			}
			if (resNvofCost) {
				cu->cuGraphicsUnregisterResource(resNvofCost);
				resNvofCost = nullptr;
			}
		}
		nvofAvailable = false;
	}

	void UnregisterBuffers()
	{
		ReleaseNvofBuffers();
		if (cu && cudaContext) {
			cu->cuCtxSetCurrent(cudaContext);
			if (resIn) {
				cu->cuGraphicsUnregisterResource(resIn);
				resIn = nullptr;
			}
			if (resOut) {
				cu->cuGraphicsUnregisterResource(resOut);
				resOut = nullptr;
			}
		}
		nvofAttempted = false;
	}

	void ReleaseBuffers()
	{
		UnregisterBuffers();
		uavIn.Release();
		uavNvofLuma.Release();
		srvOut.Release();
		uavScene.Release();
		srvNvofFlow.Release();
		srvNvofCost.Release();
		bufIn.Release();
		bufOut.Release();
		bufNvofLuma.Release();
		bufNvofFlow.Release();
		bufNvofCost.Release();
		bufScene.Release();
		bufSceneStaging.Release();
	}

	// Called on the rendering thread once the worker has created the CUDA context.
	bool EnsureRegistered()
	{
		if (!cudaReady || !bufIn || !bufOut) {
			return false;
		}
		cu->cuCtxSetCurrent(cudaContext);
		if (!resIn || !resOut) {
			cudrv::CUresult res = cu->cuGraphicsD3D11RegisterResource(&resIn, bufIn, cudrv::CU_GRAPHICS_REGISTER_FLAGS_NONE);
			if (res == cudrv::CUDA_SUCCESS) {
				cu->cuGraphicsResourceSetMapFlags(resIn, CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY);
				res = cu->cuGraphicsD3D11RegisterResource(&resOut, bufOut, cudrv::CU_GRAPHICS_REGISTER_FLAGS_NONE);
			}
			if (res != cudrv::CUDA_SUCCESS) {
				UnregisterBuffers();
				SetStatus(RifeState::Failed, L"cuGraphicsD3D11RegisterResource() failed: " + cudrv::ErrStr(res));
				return false;
			}
			cu->cuGraphicsResourceSetMapFlags(resOut, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD);
		}

		if (!nvofAttempted && config.sceneThreshold > 0.0f) {
			nvofAttempted = true;
			cudrv::CUresult res = cu->cuGraphicsD3D11RegisterResource(&resNvofLuma, bufNvofLuma,
				cudrv::CU_GRAPHICS_REGISTER_FLAGS_NONE);
			if (res == cudrv::CUDA_SUCCESS) {
				cu->cuGraphicsResourceSetMapFlags(resNvofLuma, CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY);
				res = cu->cuGraphicsD3D11RegisterResource(&resNvofFlow, bufNvofFlow,
					cudrv::CU_GRAPHICS_REGISTER_FLAGS_NONE);
			}
			if (res == cudrv::CUDA_SUCCESS) {
				cu->cuGraphicsResourceSetMapFlags(resNvofFlow, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD);
				res = cu->cuGraphicsD3D11RegisterResource(&resNvofCost, bufNvofCost,
					cudrv::CU_GRAPHICS_REGISTER_FLAGS_NONE);
			}
			if (res == cudrv::CUDA_SUCCESS) {
				cu->cuGraphicsResourceSetMapFlags(resNvofCost, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD);
				std::wstring error;
				nvofAvailable = nvof.Init(cu, cudaContext, stream, width, height, error);
				nvofStatus = nvofAvailable ? L"active" : error;
				if (!nvofAvailable) {
					ReleaseNvofBuffers();
				}
			} else {
				nvofStatus = L"CUDA/D3D11 interop failed: " + cudrv::ErrStr(res);
				ReleaseNvofBuffers();
			}
		}
		return true;
	}

	// ---- worker thread -------------------------------------------------

	bool InitCuda(std::wstring& error)
	{
		cu = cudrv::Load(error);
		if (!cu) {
			return false;
		}

		CComPtr<IDXGIAdapter> adapter;
		{
			CComQIPtr<IDXGIDevice> dxgiDevice(device);
			if (!dxgiDevice || FAILED(dxgiDevice->GetAdapter(&adapter))) {
				error = L"IDXGIDevice::GetAdapter() failed";
				return false;
			}
		}

		cudrv::CUresult res = cu->cuD3D11GetDevice(&cudaDevice, adapter);
		if (res != cudrv::CUDA_SUCCESS) {
			error = L"cuD3D11GetDevice() failed: " + cudrv::ErrStr(res);
			return false;
		}
		res = cu->cuDevicePrimaryCtxRetain(&cudaContext, cudaDevice);
		if (res != cudrv::CUDA_SUCCESS) {
			error = L"cuDevicePrimaryCtxRetain() failed: " + cudrv::ErrStr(res);
			return false;
		}
		cu->cuCtxSetCurrent(cudaContext);

		char name[256] = {};
		cu->cuDeviceGetName(name, sizeof(name), cudaDevice);
		gpuName = A2WStr(name);
		cu->cuDeviceGetAttribute(&smMajor, cudrv::CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, cudaDevice);
		cu->cuDeviceGetAttribute(&smMinor, cudrv::CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, cudaDevice);

		res = cu->cuStreamCreate(&stream, cudrv::CU_STREAM_NON_BLOCKING);
		for (int i = 0; i < 2 && res == cudrv::CUDA_SUCCESS; i++) {
			res = cu->cuEventCreate(&evStart[i], cudrv::CU_EVENT_DEFAULT);
			if (res == cudrv::CUDA_SUCCESS) {
				res = cu->cuEventCreate(&evEnd[i], cudrv::CU_EVENT_DEFAULT);
			}
		}
		if (res != cudrv::CUDA_SUCCESS) {
			error = L"CUDA stream/event creation failed: " + cudrv::ErrStr(res);
			return false;
		}

		cudaReady = true;
		return true;
	}

	bool InitTensorRT(std::wstring& error)
	{
		if (!trtload::Load(config.trtDir, error)) {
			return false;
		}
		runtime.reset(nvinfer1::createInferRuntime(logger));
		if (!runtime) {
			error = L"createInferRuntime() failed: " + logger.LastError();
			return false;
		}
		return true;
	}

	std::filesystem::path CacheDir()
	{
		return config.cacheDir.empty() ? DefaultCacheDir() : config.cacheDir;
	}

	std::wstring EngineKey(UINT w, UINT h)
	{
		return std::format(L"{}_{}x{}_{}_sm{}{}_{}_trt{}_{:016x}",
			SanitizeName(modelName), w, h, config.fp16 ? L"fp16" : L"fp32",
			smMajor, smMinor, SanitizeName(gpuName), trtload::LibVersion(),
			FileIdentityHash(config.onnxPath));
	}

	std::unique_ptr<Engine> MakeEngine(const std::vector<char>& blob, UINT w, UINT h, bool fromCache, std::wstring& error)
	{
		auto result = std::make_unique<Engine>();
		result->engine.reset(runtime->deserializeCudaEngine(blob.data(), blob.size()));
		if (!result->engine) {
			error = L"deserializeCudaEngine() failed: " + logger.LastError();
			return nullptr;
		}

		auto& eng = *result->engine;
		for (int32_t i = 0; i < eng.getNbIOTensors(); i++) {
			const char* name = eng.getIOTensorName(i);
			if (eng.getTensorIOMode(name) == nvinfer1::TensorIOMode::kINPUT) {
				result->inputName = name;
			} else {
				result->outputName = name;
			}
		}
		const auto inDims = eng.getTensorShape(result->inputName.c_str());
		const auto outDims = eng.getTensorShape(result->outputName.c_str());
		const bool dimsOK = inDims.nbDims == 4 && inDims.d[0] == 1 && inDims.d[1] == INPUT_CHANNELS
			&& inDims.d[2] == static_cast<int64_t>(h) && inDims.d[3] == static_cast<int64_t>(w)
			&& outDims.nbDims == 4 && outDims.d[1] == OUTPUT_CHANNELS
			&& outDims.d[2] == static_cast<int64_t>(h) && outDims.d[3] == static_cast<int64_t>(w)
			&& eng.getTensorDataType(result->inputName.c_str()) == nvinfer1::DataType::kFLOAT
			&& eng.getTensorDataType(result->outputName.c_str()) == nvinfer1::DataType::kFLOAT;
		if (!dimsOK) {
			error = std::format(L"engine tensors do not match the expected shapes [1,{},{},{}] -> [1,{},{},{}]",
				INPUT_CHANNELS, h, w, OUTPUT_CHANNELS, h, w);
			return nullptr;
		}

		result->context.reset(eng.createExecutionContext());
		if (!result->context) {
			error = L"createExecutionContext() failed: " + logger.LastError();
			return nullptr;
		}
		result->paddedWidth = w;
		result->paddedHeight = h;
		result->fromCache = fromCache;
		return result;
	}

	bool BuildEngine(UINT w, UINT h, const std::filesystem::path& enginePath, const std::filesystem::path& timingPath,
		std::vector<char>& engineBlob, std::wstring& error)
	{
		std::vector<char> onnx;
		if (!ReadWholeFile(config.onnxPath, onnx)) {
			error = L"cannot read " + config.onnxPath;
			return false;
		}

		if (!trtload::LoadBuilderResource(error)) {
			return false;
		}
		std::unique_ptr<nvinfer1::IBuilder> builder(nvinfer1::createInferBuilder(logger));
		if (!builder) {
			error = L"createInferBuilder() failed: " + logger.LastError();
			return false;
		}
		std::unique_ptr<nvinfer1::INetworkDefinition> network(builder->createNetworkV2(0));
		std::unique_ptr<nvonnxparser::IParser> parser(network ? nvonnxparser::createParser(*network, logger) : nullptr);
		if (!parser) {
			error = L"createParser() failed: " + logger.LastError();
			return false;
		}
		if (!parser->parse(onnx.data(), onnx.size())) {
			error = L"ONNX parse failed";
			if (parser->getNbErrors() > 0) {
				error += L": " + A2WStr(parser->getError(parser->getNbErrors() - 1)->desc());
			}
			return false;
		}

		if (network->getNbInputs() != 1 || network->getNbOutputs() != 1) {
			error = std::format(L"unsupported ONNX: {} inputs, {} outputs (expected 1 and 1)", network->getNbInputs(), network->getNbOutputs());
			return false;
		}
		auto* input = network->getInput(0);
		const auto dims = input->getDimensions();
		if (dims.nbDims != 4 || dims.d[1] != INPUT_CHANNELS) {
			error = L"unsupported ONNX: expected input [1,11,H,W] (vs-mlrt RIFE format)";
			return false;
		}

		std::unique_ptr<nvinfer1::IBuilderConfig> cfg(builder->createBuilderConfig());
		if (!cfg) {
			error = L"createBuilderConfig() failed: " + logger.LastError();
			return false;
		}
		if (config.fp16) {
			cfg->setFlag(nvinfer1::BuilderFlag::kFP16);
		}
		auto* profile = builder->createOptimizationProfile();
		if (!profile) {
			error = L"createOptimizationProfile() failed: " + logger.LastError();
			return false;
		}
		const nvinfer1::Dims4 shape(1, INPUT_CHANNELS, h, w);
		const bool profileOK = profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMIN, shape)
			&& profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kOPT, shape)
			&& profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMAX, shape)
			&& cfg->addOptimizationProfile(profile) >= 0;
		if (!profileOK) {
			error = L"failed to configure the static-shape optimization profile";
			return false;
		}
		uint64_t workspaceSize = 2ull << 30;
		size_t freeMemory = 0;
		size_t totalMemory = 0;
		if (cu->cuMemGetInfo(&freeMemory, &totalMemory) == cudrv::CUDA_SUCCESS && freeMemory > 0) {
			workspaceSize = std::min<uint64_t>(workspaceSize, freeMemory / 2);
		}
		cfg->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE, workspaceSize);

		std::unique_ptr<nvinfer1::ITimingCache> timingCache;
		{
			std::vector<char> blob;
			if (!ReadWholeFile(timingPath, blob)) {
				blob.clear();
			}
			timingCache.reset(cfg->createTimingCache(blob.data(), blob.size()));
			if (timingCache) {
				cfg->setTimingCache(*timingCache, false);
			}
		}

		CBuildMonitor monitor(cancel);
		cfg->setProgressMonitor(&monitor);

		std::unique_ptr<nvinfer1::IHostMemory> serialized(builder->buildSerializedNetwork(*network, *cfg));
		if (cancel) {
			return false;
		}
		if (!serialized || !serialized->size()) {
			error = L"engine build failed: " + logger.LastError();
			return false;
		}
		const auto* serializedData = static_cast<const char*>(serialized->data());
		engineBlob.assign(serializedData, serializedData + serialized->size());

		std::error_code ec;
		std::filesystem::create_directories(enginePath.parent_path(), ec);
		if (!WriteFileAtomic(enginePath, engineBlob.data(), engineBlob.size())) {
			DLog(L"CRifeInterpolator : cannot write engine cache {}", enginePath.wstring());
		}
		if (timingCache) {
			std::unique_ptr<nvinfer1::IHostMemory> timingBlob(timingCache->serialize());
			if (timingBlob && timingBlob->size()) {
				WriteFileAtomic(timingPath, timingBlob->data(), timingBlob->size());
			}
		}
		return true;
	}

	std::unique_ptr<Engine> LoadOrBuild(UINT w, UINT h, std::wstring& error)
	{
		const auto cacheDir = CacheDir();
		const auto enginePath = cacheDir / (EngineKey(w, h) + L".engine");
		const auto timingPath = cacheDir / std::format(L"timing_sm{}{}_{}_trt{}.cache", smMajor, smMinor, SanitizeName(gpuName), trtload::LibVersion());

		std::vector<char> blob;
		bool fromCache = ReadWholeFile(enginePath, blob);
		if (fromCache) {
			std::wstring ignored;
			if (auto eng = MakeEngine(blob, w, h, true, ignored)) {
				return eng;
			}
			DLog(L"CRifeInterpolator : cached engine rejected ({}), rebuilding", ignored);
			blob.clear();
		}

		if (!BuildEngine(w, h, enginePath, timingPath, blob, error)) {
			return nullptr;
		}
		return MakeEngine(blob, w, h, false, error);
	}

	void WorkerThread()
	{
		SetThreadName(static_cast<DWORD>(-1), "RIFE TensorRT");

		std::wstring error;
		if (!InitCuda(error) || !InitTensorRT(error)) {
			SetStatus(RifeState::Failed, error);
			return;
		}
		SetStatus(RifeState::NoEngine, std::format(L"{} on {}", trtload::Describe(), gpuName));

		for (;;) {
			UINT w, h;
			{
				std::unique_lock<std::mutex> lock(mutex);
				cv.wait(lock, [this] { return quit || requestPending; });
				if (quit) {
					break;
				}
				w = requestWidth;
				h = requestHeight;
				requestPending = false;
				cancel = false;
				state = RifeState::Building;
				statusText.clear();
				buildStartTick = GetPreciseTick();
			}

			std::wstring err;
			auto eng = LoadOrBuild(w, h, err);
			if (cancel) {
				continue;
			}

			std::lock_guard<std::mutex> lock(mutex);
			if (eng) {
				statusText = std::format(L"{} on {}, engine {}x{} {} ({})", trtload::Describe(), gpuName, w, h,
					config.fp16 ? L"fp16" : L"fp32", eng->fromCache ? L"cached" : L"built");
				pendingEngine = std::move(eng);
				state = RifeState::Ready;
			} else {
				statusText = err;
				state = RifeState::Failed;
			}
		}
	}

	void Release()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			quit = true;
			cancel = true;
		}
		cv.notify_all();
		if (worker.joinable()) {
			worker.join();
		}

		if (cu && cudaContext) {
			cu->cuCtxSetCurrent(cudaContext);
			cu->cuStreamSynchronize(stream);
		}
		engine.reset();
		pendingEngine.reset();
		ReleaseBuffers();
		runtime.reset();

		if (cu) {
			for (int i = 0; i < 2; i++) {
				if (evStart[i]) { cu->cuEventDestroy(evStart[i]); evStart[i] = nullptr; }
				if (evEnd[i])   { cu->cuEventDestroy(evEnd[i]);   evEnd[i] = nullptr; }
			}
			if (stream) {
				cu->cuStreamDestroy(stream);
				stream = nullptr;
			}
			if (cudaContext) {
				cu->cuDevicePrimaryCtxRelease(cudaDevice);
				cudaContext = nullptr;
			}
		}
		cudaReady = false;

		lastOutputRTV.Release();
		lastOutput.Release();
		cbParams.Release();
		csPack.Release();
		csScene.Release();
		psUnpack.Release();
		vsQuad.Release();
		context.Release();
		device.Release();
		state = RifeState::Unloaded;
	}
};

CRifeInterpolator::CRifeInterpolator() = default;

CRifeInterpolator::~CRifeInterpolator() = default;

HRESULT CRifeInterpolator::Init(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, const RifeConfig& config, std::wstring& error)
{
	Release();
	if (!pDevice || !pContext) {
		error = L"no D3D11 device";
		return E_POINTER;
	}
	if (pDevice->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {
		error = L"Direct3D feature level 11_0 is required";
		return E_NOTIMPL;
	}
	if (config.onnxPath.empty()) {
		error = L"no ONNX model selected";
		return E_INVALIDARG;
	}

	m_impl = std::make_unique<Impl>();
	Impl& d = *m_impl;
	d.device = pDevice;
	d.context = pContext;
	d.config = config;
	d.modelName = std::filesystem::path(config.onnxPath).stem().wstring();

	if (config.padMultiple == 32 || config.padMultiple == 64 || config.padMultiple == 128) {
		d.padMultiple = config.padMultiple;
	} else {
		std::wstring lower = d.modelName;
		str_tolower(lower);
		d.padMultiple = (lower.find(L"4.25") != std::wstring::npos && lower.find(L"lite") != std::wstring::npos) ? 128 : 64;
	}

	HRESULT hr = d.CreateShaders();
	if (FAILED(hr)) {
		error = L"shader creation failed: " + HR2Str(hr);
		m_impl.reset();
		return hr;
	}

	d.state = RifeState::Loading;
	d.worker = std::thread([this] { m_impl->WorkerThread(); });
	return S_OK;
}

HRESULT CRifeInterpolator::SetFrameSize(UINT width, UINT height)
{
	if (!m_impl) {
		return E_ABORT;
	}
	if (!width || !height) {
		return E_INVALIDARG;
	}
	Impl& d = *m_impl;

	const UINT pw = (width + d.padMultiple - 1) / d.padMultiple * d.padMultiple;
	const UINT ph = (height + d.padMultiple - 1) / d.padMultiple * d.padMultiple;
	if (pw == d.paddedWidth && ph == d.paddedHeight && width == d.width && height == d.height && d.bufIn) {
		return S_OK;
	}

	d.engine.reset();
	d.ReleaseBuffers();
	d.width = width;
	d.height = height;
	d.paddedWidth = pw;
	d.paddedHeight = ph;
	d.flowWidth = (width + 3) / 4;
	d.flowHeight = (height + 3) / 4;
	d.lumaPitch = (width + 3) & ~3u;
	d.costPitch = (d.flowWidth + 3) & ~3u;
	d.lastSceneCut = false;
	d.lastSceneInlierRatio = 1.0f;
	d.lastSceneMeanCost = 0.0f;
	d.nvofStatus = L"not initialized";
	d.gridDirty = true;

	HRESULT hr = d.CreateRawBuffer(INPUT_CHANNELS * pw * ph * 4, &d.bufIn, &d.uavIn, nullptr);
	if (SUCCEEDED(hr)) {
		hr = d.CreateRawBuffer(OUTPUT_CHANNELS * pw * ph * 4, &d.bufOut, nullptr, &d.srvOut);
	}
	if (SUCCEEDED(hr)) {
		hr = d.CreateRawBuffer(2 * d.lumaPitch * height, &d.bufNvofLuma, &d.uavNvofLuma, nullptr);
	}
	if (SUCCEEDED(hr)) {
		hr = d.CreateRawBuffer(2 * d.flowWidth * d.flowHeight * 4, &d.bufNvofFlow, nullptr, &d.srvNvofFlow);
	}
	if (SUCCEEDED(hr)) {
		hr = d.CreateRawBuffer(2 * d.costPitch * d.flowHeight, &d.bufNvofCost, nullptr, &d.srvNvofCost);
	}
	if (SUCCEEDED(hr)) {
		hr = d.CreateRawBuffer(16, &d.bufScene, &d.uavScene, nullptr);
	}
	if (SUCCEEDED(hr)) {
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = 16;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		hr = d.device->CreateBuffer(&desc, nullptr, &d.bufSceneStaging);
	}
	if (FAILED(hr)) {
		d.ReleaseBuffers();
		d.SetStatus(RifeState::Failed, std::format(L"tensor buffer creation failed ({}x{}): {}", pw, ph, HR2Str(hr)));
		return hr;
	}

	{
		std::lock_guard<std::mutex> lock(d.mutex);
		d.requestWidth = pw;
		d.requestHeight = ph;
		d.requestPending = true;
		d.cancel = true; // abort a build for another size
		d.pendingEngine.reset();
	}
	d.cv.notify_all();
	return S_OK;
}

bool CRifeInterpolator::IsReady()
{
	if (!m_impl) {
		return false;
	}
	Impl& d = *m_impl;

	{
		std::lock_guard<std::mutex> lock(d.mutex);
		if (d.pendingEngine) {
			if (d.pendingEngine->paddedWidth == d.paddedWidth && d.pendingEngine->paddedHeight == d.paddedHeight) {
				d.engine = std::move(d.pendingEngine);
			} else {
				d.pendingEngine.reset();
			}
		}
	}

	return d.engine && d.engine->paddedWidth == d.paddedWidth && d.engine->paddedHeight == d.paddedHeight
		&& d.EnsureRegistered();
}

void CRifeInterpolator::SetSceneThreshold(float threshold)
{
	if (m_impl) {
		m_impl->config.sceneThreshold = std::clamp(threshold, 0.0f, 1.0f);
	}
}

float CRifeInterpolator::GetSceneThreshold() const
{
	return m_impl ? m_impl->config.sceneThreshold : 0.0f;
}

RifeState CRifeInterpolator::GetState() const
{
	return m_impl ? m_impl->state.load() : RifeState::Unloaded;
}

std::wstring CRifeInterpolator::GetStatusText() const
{
	if (!m_impl) {
		return StateName(RifeState::Unloaded);
	}
	Impl& d = *m_impl;
	std::lock_guard<std::mutex> lock(d.mutex);
	const RifeState state = d.state;
	if (state == RifeState::Building) {
		const double seconds = static_cast<double>(GetPreciseTick() - d.buildStartTick) * GetPreciseSecondsPerTick();
		return std::format(L"building engine {}x{} ({:.0f} s)", d.requestWidth ? d.requestWidth : d.paddedWidth,
			d.requestHeight ? d.requestHeight : d.paddedHeight, seconds);
	}
	if (d.statusText.empty()) {
		return StateName(state);
	}
	return (state == RifeState::Failed) ? L"failed: " + d.statusText : d.statusText;
}

std::wstring CRifeInterpolator::GetModelName() const
{
	return m_impl ? m_impl->modelName : std::wstring();
}

HRESULT CRifeInterpolator::SetPair(ID3D11ShaderResourceView* pPrev, ID3D11ShaderResourceView* pCur)
{
	if (!m_impl || !m_impl->bufIn) {
		return E_ABORT;
	}
	Impl& d = *m_impl;
	ID3D11DeviceContext* ctx = d.context;
	d.lastSceneCut = false;

	ID3D11ShaderResourceView* srvs[2] = { pPrev, pCur };
	ID3D11UnorderedAccessView* uavs[2] = { d.uavIn, d.uavNvofLuma };
	ID3D11Buffer* cb = d.cbParams;

	d.UpdateParams(0.0f, PACK_IMAGES | (d.gridDirty ? PACK_GRID : 0));
	d.gridDirty = false;
	ctx->CSSetShader(d.csPack, nullptr, 0);
	ctx->CSSetConstantBuffers(0, 1, &cb);
	ctx->CSSetShaderResources(0, 2, srvs);
	ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
	ctx->Dispatch((d.paddedWidth + 15) / 16, (d.paddedHeight + 15) / 16, 1);

	ID3D11ShaderResourceView* nullSRVs[2] = {};
	ID3D11UnorderedAccessView* nullUAVs[2] = {};
	ctx->CSSetShaderResources(0, 2, nullSRVs);
	ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
	ctx->CSSetShader(nullptr, nullptr, 0);

	if (d.config.sceneThreshold > 0.0f && d.nvofAvailable) {
		ctx->Flush();
		cudrv::CUgraphicsResource resources[3] = { d.resNvofLuma, d.resNvofFlow, d.resNvofCost };
		cudrv::CUresult result = d.cu->cuGraphicsMapResources(3, resources, d.stream);
		std::wstring error;
		if (result == cudrv::CUDA_SUCCESS) {
			cudrv::CUdeviceptr luma = 0, flow = 0, cost = 0;
			size_t lumaSize = 0, flowSize = 0, costSize = 0;
			result = d.cu->cuGraphicsResourceGetMappedPointer(&luma, &lumaSize, d.resNvofLuma);
			if (result == cudrv::CUDA_SUCCESS) result = d.cu->cuGraphicsResourceGetMappedPointer(&flow, &flowSize, d.resNvofFlow);
			if (result == cudrv::CUDA_SUCCESS) result = d.cu->cuGraphicsResourceGetMappedPointer(&cost, &costSize, d.resNvofCost);
			const size_t lumaPlaneBytes = static_cast<size_t>(d.lumaPitch) * d.height;
			const size_t flowPlaneBytes = static_cast<size_t>(d.flowWidth) * d.flowHeight * 4;
			const size_t costPlaneBytes = static_cast<size_t>(d.costPitch) * d.flowHeight;
			if (result == cudrv::CUDA_SUCCESS && lumaSize >= 2 * lumaPlaneBytes
					&& flowSize >= 2 * flowPlaneBytes && costSize >= 2 * costPlaneBytes) {
				d.nvofAvailable = d.nvof.Execute(luma, d.lumaPitch, lumaPlaneBytes,
					flow, d.flowWidth * 4ull, flowPlaneBytes,
					cost, d.costPitch, costPlaneBytes, error);
			} else if (result != cudrv::CUDA_SUCCESS) {
				error = L"CUDA mapped-pointer query failed: " + cudrv::ErrStr(result);
				d.nvofAvailable = false;
			} else {
				error = L"CUDA/D3D11 interop buffer is smaller than expected";
				d.nvofAvailable = false;
			}
			const cudrv::CUresult unmapResult = d.cu->cuGraphicsUnmapResources(3, resources, d.stream);
			if (unmapResult != cudrv::CUDA_SUCCESS) {
				error = L"cuGraphicsUnmapResources() failed: " + cudrv::ErrStr(unmapResult);
				d.nvofAvailable = false;
			}
		} else {
			error = L"cuGraphicsMapResources() failed: " + cudrv::ErrStr(result);
			d.nvofAvailable = false;
		}

		if (!d.nvofAvailable) {
			d.nvofStatus = error;
			d.ReleaseNvofBuffers();
		} else {
			const UINT zeros[4] = {};
			ctx->ClearUnorderedAccessViewUint(d.uavScene, zeros);
			ID3D11ShaderResourceView* flowSrvs[2] = { d.srvNvofFlow, d.srvNvofCost };
			ID3D11UnorderedAccessView* statsUav = d.uavScene;
			ctx->CSSetShader(d.csScene, nullptr, 0);
			ctx->CSSetConstantBuffers(0, 1, &cb);
			ctx->CSSetShaderResources(0, 2, flowSrvs);
			ctx->CSSetUnorderedAccessViews(0, 1, &statsUav, nullptr);
			ctx->Dispatch((d.flowWidth + 15) / 16, (d.flowHeight + 15) / 16, 1);
			ctx->CSSetShaderResources(0, 2, nullSRVs);
			ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
			ctx->CSSetShader(nullptr, nullptr, 0);
			ctx->CopyResource(d.bufSceneStaging, d.bufScene);

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			const HRESULT mapHr = ctx->Map(d.bufSceneStaging, 0, D3D11_MAP_READ, 0, &mapped);
			if (SUCCEEDED(mapHr)) {
				const UINT* stats = static_cast<const UINT*>(mapped.pData);
				const UINT total = stats[0];
				const UINT inBounds = stats[1];
				d.lastSceneInlierRatio = total ? static_cast<float>(stats[2]) / total : 1.0f;
				d.lastSceneMeanCost = inBounds ? static_cast<float>(stats[3]) / inBounds : 255.0f;
				ctx->Unmap(d.bufSceneStaging, 0);
				d.lastSceneCut = total && d.lastSceneInlierRatio < d.config.sceneThreshold;
				if (d.lastSceneCut) {
					d.sceneCuts++;
				}
			} else {
				d.nvofAvailable = false;
				d.nvofStatus = L"NVOFA result readback failed: " + HR2Str(mapHr);
				d.ReleaseNvofBuffers();
			}
		}
	}
	return S_OK;
}

HRESULT CRifeInterpolator::Infer(float timestep, ID3D11Texture2D* pOutput)
{
	if (!m_impl || !pOutput) {
		return E_POINTER;
	}
	Impl& d = *m_impl;
	if (!d.engine || !d.resIn || !d.resOut) {
		return E_ABORT;
	}
	ID3D11DeviceContext* ctx = d.context;
	const cudrv::Api* cu = d.cu;
	cu->cuCtxSetCurrent(d.cudaContext);

	// timestep plane
	{
		ID3D11UnorderedAccessView* uav = d.uavIn;
		ID3D11Buffer* cb = d.cbParams;
		d.UpdateParams(timestep, PACK_TIMESTEP);
		ctx->CSSetShader(d.csPack, nullptr, 0);
		ctx->CSSetConstantBuffers(0, 1, &cb);
		ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		ctx->Dispatch((d.paddedWidth + 15) / 16, (d.paddedHeight + 15) / 16, 1);
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		ctx->CSSetShader(nullptr, nullptr, 0);
	}
	ctx->Flush();

	// network
	{
		cudrv::CUgraphicsResource resources[2] = { d.resIn, d.resOut };
		cudrv::CUresult res = cu->cuGraphicsMapResources(2, resources, d.stream);
		if (res != cudrv::CUDA_SUCCESS) {
			DLog(L"CRifeInterpolator::Infer() : cuGraphicsMapResources() failed: {}", cudrv::ErrStr(res));
			return E_FAIL;
		}
		cudrv::CUdeviceptr pIn = 0, pOut = 0;
		size_t sizeIn = 0, sizeOut = 0;
		cu->cuGraphicsResourceGetMappedPointer(&pIn, &sizeIn, d.resIn);
		cu->cuGraphicsResourceGetMappedPointer(&pOut, &sizeOut, d.resOut);

		auto& exec = *d.engine->context;
		bool ok = exec.setTensorAddress(d.engine->inputName.c_str(), reinterpret_cast<void*>(pIn))
			&& exec.setTensorAddress(d.engine->outputName.c_str(), reinterpret_cast<void*>(pOut));
		if (ok) {
			cu->cuEventRecord(d.evStart[d.evIndex], d.stream);
			ok = exec.enqueueV3(d.stream);
			cu->cuEventRecord(d.evEnd[d.evIndex], d.stream);
			d.evPending[d.evIndex] = ok;
		}
		cu->cuGraphicsUnmapResources(2, resources, d.stream);
		if (!ok) {
			DLog(L"CRifeInterpolator::Infer() : enqueueV3() failed: {}", d.logger.LastError());
			return E_FAIL;
		}

		// timing of the previous inference, without blocking
		const int prev = d.evIndex ^ 1;
		if (d.evPending[prev] && cu->cuEventQuery(d.evEnd[prev]) == cudrv::CUDA_SUCCESS) {
			float ms = 0.0f;
			if (cu->cuEventElapsedTime(&ms, d.evStart[prev], d.evEnd[prev]) == cudrv::CUDA_SUCCESS) {
				d.lastGpuMs = ms;
			}
			d.evPending[prev] = false;
		}
		d.evIndex = prev;
	}

	// unpack into the output texture
	{
		if (d.lastOutput != pOutput) {
			d.lastOutputRTV.Release();
			HRESULT hr = d.device->CreateRenderTargetView(pOutput, nullptr, &d.lastOutputRTV);
			if (FAILED(hr)) {
				return hr;
			}
			d.lastOutput = pOutput;
		}

		ID3D11RenderTargetView* rtv = d.lastOutputRTV;
		ID3D11ShaderResourceView* srv = d.srvOut;
		ID3D11Buffer* cb = d.cbParams;
		const D3D11_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(d.width), static_cast<float>(d.height), 0.0f, 1.0f };

		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		ctx->RSSetViewports(1, &vp);
		ctx->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(d.vsQuad, nullptr, 0);
		ctx->PSSetShader(d.psUnpack, nullptr, 0);
		ctx->PSSetConstantBuffers(0, 1, &cb);
		ctx->PSSetShaderResources(0, 1, &srv);
		ctx->Draw(3, 0);

		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11RenderTargetView* nullRTV = nullptr;
		ctx->PSSetShaderResources(0, 1, &nullSRV);
		ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
	}

	return S_OK;
}

bool CRifeInterpolator::IsSceneCut() const
{
	return m_impl && m_impl->lastSceneCut;
}

double CRifeInterpolator::GetLastGpuMs() const
{
	return m_impl ? m_impl->lastGpuMs : 0.0;
}

float CRifeInterpolator::GetLastSceneInlierRatio() const
{
	return m_impl ? m_impl->lastSceneInlierRatio : 1.0f;
}

float CRifeInterpolator::GetLastSceneMeanCost() const
{
	return m_impl ? m_impl->lastSceneMeanCost : 0.0f;
}

std::wstring CRifeInterpolator::GetSceneDetectorStatus() const
{
	if (!m_impl) {
		return L"not initialized";
	}
	if (m_impl->config.sceneThreshold <= 0.0f) {
		return L"disabled";
	}
	return m_impl->nvofAvailable ? L"active" : L"unavailable: " + m_impl->nvofStatus;
}

UINT CRifeInterpolator::GetSceneCutCount() const
{
	return m_impl ? m_impl->sceneCuts : 0;
}

void CRifeInterpolator::Release()
{
	m_impl.reset();
}
