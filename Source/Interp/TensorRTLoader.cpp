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
#include <NvInfer.h>
#include <NvOnnxParser.h>
#include "Utils/Util.h"
#include "Utils/StringUtil.h"
#include "TensorRTLoader.h"

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace trtload {

namespace {

using PFN_createInferRuntime  = void* (*)(void*, int32_t) noexcept;
using PFN_createInferBuilder  = void* (*)(void*, int32_t) noexcept;
using PFN_createInferRefitter = void* (*)(void*, void*, int32_t) noexcept;
using PFN_getInt32            = int32_t (*)() noexcept;
using PFN_getPluginRegistry   = nvinfer1::IPluginRegistry* (*)() noexcept;
using PFN_getBuilderPluginRegistry = nvinfer1::IPluginRegistry* (*)(nvinfer1::EngineCapability) noexcept;
using PFN_getLogger           = nvinfer1::ILogger* (*)() noexcept;
using PFN_createNvOnnxParser  = void* (*)(void*, void*, int);
using PFN_createNvOnnxParserRefitter = void* (*)(void*, void*, int32_t);
using PFN_getNvOnnxParserVersion = int (*)();

struct State {
	std::mutex mutex;
	bool loaded = false;
	std::wstring dir;
	int32_t libVersion = 0;

	HMODULE hInfer = nullptr;
	HMODULE hParser = nullptr;
	HMODULE hBuilderResource = nullptr;

	PFN_createInferRuntime  createInferRuntime = nullptr;
	PFN_createInferBuilder  createInferBuilder = nullptr;
	PFN_createInferRefitter createInferRefitter = nullptr;
	PFN_getInt32            getInferLibVersion = nullptr;
	PFN_getInt32            getInferLibMajorVersion = nullptr;
	PFN_getInt32            getInferLibMinorVersion = nullptr;
	PFN_getInt32            getInferLibPatchVersion = nullptr;
	PFN_getInt32            getInferLibBuildVersion = nullptr;
	PFN_getPluginRegistry   getPluginRegistry = nullptr;
	PFN_getBuilderPluginRegistry getBuilderPluginRegistry = nullptr;
	PFN_getLogger           getLogger = nullptr;
	PFN_createNvOnnxParser  createNvOnnxParser = nullptr;
	PFN_createNvOnnxParserRefitter createNvOnnxParserRefitter = nullptr;
	PFN_getNvOnnxParserVersion getNvOnnxParserVersion = nullptr;
};

State& GetState()
{
	static State state;
	return state;
}

template <typename T>
bool Resolve(HMODULE hModule, T& fn, const char* name, const wchar_t* dllName, std::wstring& error)
{
	fn = reinterpret_cast<T>(GetProcAddress(hModule, name));
	if (!fn) {
		error = std::format(L"{} does not export {}", dllName, A2WStr(name));
		return false;
	}
	return true;
}

HMODULE LoadFrom(const std::wstring& dir, const wchar_t* dllName)
{
	if (dir.empty()) {
		return LoadLibraryW(dllName);
	}
	std::wstring path = dir;
	if (path.back() != L'\\') {
		path += L'\\';
	}
	path += dllName;
	// nvinfer loads nvinfer_builder_resource_10.dll and cudart lazily from its own directory,
	// so the directory must stay in the search list for dependent loads.
	AddDllDirectory(path.substr(0, path.size() - wcslen(dllName)).c_str());
	return LoadLibraryExW(path.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
}

bool DirHasDll(const std::wstring& dir, const wchar_t* dllName)
{
	if (dir.empty()) {
		return false;
	}
	std::wstring path = dir;
	if (path.back() != L'\\') {
		path += L'\\';
	}
	path += dllName;
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool TryLoad(State& s, const std::wstring& dir, std::wstring& error)
{
	const wchar_t* inferName = L"nvinfer_10.dll";
	const wchar_t* parserName = L"nvonnxparser_10.dll";

	HMODULE hInfer = LoadFrom(dir, inferName);
	if (!hInfer) {
		error = std::format(L"{} not found or failed to load (error {}){}", inferName, GetLastError(),
			dir.empty() ? L"" : L" in " + dir);
		return false;
	}
	HMODULE hParser = LoadFrom(dir, parserName);
	if (!hParser) {
		error = std::format(L"{} not found or failed to load (error {}){}", parserName, GetLastError(),
			dir.empty() ? L"" : L" in " + dir);
		FreeLibrary(hInfer);
		return false;
	}

	const bool ok = Resolve(hInfer, s.createInferRuntime, "createInferRuntime_INTERNAL", inferName, error)
		&& Resolve(hInfer, s.createInferBuilder, "createInferBuilder_INTERNAL", inferName, error)
		&& Resolve(hInfer, s.createInferRefitter, "createInferRefitter_INTERNAL", inferName, error)
		&& Resolve(hInfer, s.getInferLibVersion, "getInferLibVersion", inferName, error)
		&& Resolve(hInfer, s.getInferLibMajorVersion, "getInferLibMajorVersion", inferName, error)
		&& Resolve(hInfer, s.getInferLibMinorVersion, "getInferLibMinorVersion", inferName, error)
		&& Resolve(hInfer, s.getInferLibPatchVersion, "getInferLibPatchVersion", inferName, error)
		&& Resolve(hInfer, s.getInferLibBuildVersion, "getInferLibBuildVersion", inferName, error)
		&& Resolve(hInfer, s.getPluginRegistry, "getPluginRegistry", inferName, error)
		&& Resolve(hInfer, s.getBuilderPluginRegistry, "getBuilderPluginRegistry", inferName, error)
		&& Resolve(hInfer, s.getLogger, "getLogger", inferName, error)
		&& Resolve(hParser, s.createNvOnnxParser, "createNvOnnxParser_INTERNAL", parserName, error)
		&& Resolve(hParser, s.createNvOnnxParserRefitter, "createNvOnnxParserRefitter_INTERNAL", parserName, error)
		&& Resolve(hParser, s.getNvOnnxParserVersion, "getNvOnnxParserVersion", parserName, error);
	if (!ok) {
		FreeLibrary(hParser);
		FreeLibrary(hInfer);
		return false;
	}

	const int32_t version = s.getInferLibVersion();
	if (version / 10000 != NV_TENSORRT_MAJOR || version < NV_TENSORRT_VERSION) {
		error = std::format(L"unsupported TensorRT version {}.{}.{} (need {}.{}.{} or newer with the same major version)",
			version / 10000, (version / 100) % 100, version % 100,
			NV_TENSORRT_MAJOR, NV_TENSORRT_MINOR, NV_TENSORRT_PATCH);
		FreeLibrary(hParser);
		FreeLibrary(hInfer);
		return false;
	}

	s.hInfer = hInfer;
	s.hParser = hParser;
	s.libVersion = version;
	s.dir = dir;
	return true;
}

} // namespace

std::wstring GetModuleDir()
{
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
	std::wstring dir(path);
	const auto pos = dir.find_last_of(L'\\');
	return (pos == std::wstring::npos) ? std::wstring() : dir.substr(0, pos + 1);
}

bool Load(const std::wstring& preferredDir, std::wstring& error)
{
	State& s = GetState();
	std::lock_guard<std::mutex> lock(s.mutex);

	if (s.loaded) {
		error.clear();
		return true;
	}
	error.clear();

	std::wstring candidates[] = {
		preferredDir,
		GetModuleDir() + L"TensorRT",
	};

	std::wstring firstError;
	for (const auto& dir : candidates) {
		if (!DirHasDll(dir, L"nvinfer_10.dll")) {
			continue;
		}
		if (TryLoad(s, dir, error)) {
			s.loaded = true;
			break;
		}
		if (firstError.empty()) {
			firstError = error;
		}
	}
	if (!s.loaded) {
		// last resort: the normal DLL search order (PATH etc.)
		s.loaded = TryLoad(s, L"", error);
	}
	if (!s.loaded && !firstError.empty()) {
		error = firstError;
	}

	DLog(L"trtload::Load() : {}", s.loaded ? Describe() : error);
	return s.loaded;
}

bool IsLoaded()
{
	return GetState().loaded;
}

bool LoadBuilderResource(std::wstring& error)
{
	State& s = GetState();
	std::lock_guard<std::mutex> lock(s.mutex);
	if (!s.loaded) {
		error = L"TensorRT is not loaded";
		return false;
	}
	if (s.hBuilderResource) {
		return true;
	}
	const wchar_t* name = L"nvinfer_builder_resource_10.dll";
	s.hBuilderResource = LoadFrom(s.dir, name);
	if (!s.hBuilderResource) {
		error = std::format(L"{} not found or failed to load (error {}){}", name, GetLastError(),
			s.dir.empty() ? L"" : L" in " + s.dir);
		return false;
	}
	return true;
}

int32_t LibVersion()
{
	return GetState().libVersion;
}

std::wstring LoadedDir()
{
	return GetState().dir;
}

std::wstring Describe()
{
	State& s = GetState();
	if (!s.loaded) {
		return L"TensorRT not loaded";
	}
	std::wstring str = std::format(L"TensorRT {}.{}.{}", s.libVersion / 10000, (s.libVersion / 100) % 100, s.libVersion % 100);
	if (!s.dir.empty()) {
		str += L" (" + s.dir + L")";
	}
	return str;
}

} // namespace trtload

// Entry points referenced by the inline helpers in the TensorRT headers
// (nvinfer1::createInferRuntime() etc.). TENSORRTAPI is empty for consumers,
// so these definitions take the place of the import library.

extern "C" void* createInferRuntime_INTERNAL(void* logger, int32_t version) noexcept
{
	auto& s = trtload::GetState();
	return s.createInferRuntime ? s.createInferRuntime(logger, version) : nullptr;
}

extern "C" void* createInferBuilder_INTERNAL(void* logger, int32_t version) noexcept
{
	auto& s = trtload::GetState();
	return s.createInferBuilder ? s.createInferBuilder(logger, version) : nullptr;
}

extern "C" void* createInferRefitter_INTERNAL(void* engine, void* logger, int32_t version) noexcept
{
	auto& s = trtload::GetState();
	return s.createInferRefitter ? s.createInferRefitter(engine, logger, version) : nullptr;
}

extern "C" int32_t getInferLibVersion() noexcept
{
	auto& s = trtload::GetState();
	return s.getInferLibVersion ? s.getInferLibVersion() : 0;
}

extern "C" int32_t getInferLibMajorVersion() noexcept
{
	auto& s = trtload::GetState();
	return s.getInferLibMajorVersion ? s.getInferLibMajorVersion() : 0;
}

extern "C" int32_t getInferLibMinorVersion() noexcept
{
	auto& s = trtload::GetState();
	return s.getInferLibMinorVersion ? s.getInferLibMinorVersion() : 0;
}

extern "C" int32_t getInferLibPatchVersion() noexcept
{
	auto& s = trtload::GetState();
	return s.getInferLibPatchVersion ? s.getInferLibPatchVersion() : 0;
}

extern "C" int32_t getInferLibBuildVersion() noexcept
{
	auto& s = trtload::GetState();
	return s.getInferLibBuildVersion ? s.getInferLibBuildVersion() : 0;
}

extern "C" nvinfer1::IPluginRegistry* getPluginRegistry() noexcept
{
	auto& s = trtload::GetState();
	return s.getPluginRegistry ? s.getPluginRegistry() : nullptr;
}

extern "C" nvinfer1::IPluginRegistry* getBuilderPluginRegistry(nvinfer1::EngineCapability capability) noexcept
{
	auto& s = trtload::GetState();
	return s.getBuilderPluginRegistry ? s.getBuilderPluginRegistry(capability) : nullptr;
}

extern "C" nvinfer1::ILogger* getLogger() noexcept
{
	auto& s = trtload::GetState();
	return s.getLogger ? s.getLogger() : nullptr;
}

extern "C" void* createNvOnnxParser_INTERNAL(void* network, void* logger, int version)
{
	auto& s = trtload::GetState();
	return s.createNvOnnxParser ? s.createNvOnnxParser(network, logger, version) : nullptr;
}

extern "C" void* createNvOnnxParserRefitter_INTERNAL(void* refitter, void* logger, int32_t version)
{
	auto& s = trtload::GetState();
	return s.createNvOnnxParserRefitter ? s.createNvOnnxParserRefitter(refitter, logger, version) : nullptr;
}

extern "C" int getNvOnnxParserVersion()
{
	auto& s = trtload::GetState();
	return s.getNvOnnxParserVersion ? s.getNvOnnxParserVersion() : 0;
}
