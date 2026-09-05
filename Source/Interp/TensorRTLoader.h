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

#include <cstdint>
#include <string>

// Runtime loading of nvinfer_10.dll / nvonnxparser_10.dll. The TensorRT headers
// only reference a handful of extern "C" entry points; TensorRTLoader.cpp
// defines them as forwarders into the loaded DLLs, so no import libraries are
// needed and the renderer keeps working on machines without TensorRT.
namespace trtload {

// Directories are tried in this order: 'preferredDir' (if not empty),
// "<filter dir>\TensorRT", the SVP 4 vs-mlrt directory, then the normal DLL
// search path. Loading happens once per process; later calls return the
// cached result. Thread-safe.
bool Load(const std::wstring& preferredDir, std::wstring& error);

bool IsLoaded();

// nvinfer loads nvinfer_builder_resource_10.dll by bare name when a builder is
// created, which fails for a directory outside the default search path. Call
// before nvinfer1::createInferBuilder(); loads the DLL from the directory the
// runtime came from. Returns false with an error when it cannot be loaded.
bool LoadBuilderResource(std::wstring& error);

// getInferLibVersion() of the loaded DLL, e.g. 100800; 0 when not loaded.
int32_t LibVersion();

// Directory the DLLs were loaded from (empty when found through the search path).
std::wstring LoadedDir();

// Human readable "TensorRT 10.8.0" (+ directory) for status output.
std::wstring Describe();

// Directory of the module that contains this code (the filter .ax or the test exe), with trailing backslash.
std::wstring GetModuleDir();

} // namespace trtload
