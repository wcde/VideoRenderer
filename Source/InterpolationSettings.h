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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>

namespace interp_settings {

inline constexpr wchar_t ENABLED[] = L"Interpolation";
inline constexpr wchar_t DEFAULT_OUTPUT[] = L"InterpolationDefaultOutput";
inline constexpr wchar_t PROFILE_COUNT[] = L"InterpolationProfileCount";
inline constexpr wchar_t SCENE_THRESHOLD[] = L"InterpolationSceneThreshold";
inline constexpr DWORD PROFILE_COUNT_MAX = 64;
inline constexpr DWORD PROFILE_DIMENSION_MAX = 16384;
inline constexpr int SCENE_THRESHOLD_DEFAULT = 25;
inline constexpr int SCENE_THRESHOLD_MAX = 100;

inline std::wstring ProfilePrefix(const DWORD index)
{
	return std::format(L"InterpolationOverride{}", index);
}

template <class Registry, class Settings, class OutputValidator>
void Load(Registry& key, Settings& settings, OutputValidator&& isValidOutput)
{
	DWORD value = 0;
	if (ERROR_SUCCESS == key.QueryDWORDValue(ENABLED, value)) {
		settings.bInterp = !!value;
	}
	if (ERROR_SUCCESS == key.QueryDWORDValue(DEFAULT_OUTPUT, value)
			&& isValidOutput(static_cast<int>(value))) {
		settings.iInterpDefaultOutput = static_cast<int>(value);
	}

	DWORD profileCount = 0;
	if (ERROR_SUCCESS == key.QueryDWORDValue(PROFILE_COUNT, profileCount)) {
		settings.interpProfiles.clear();
		profileCount = (std::min)(profileCount, PROFILE_COUNT_MAX);
		for (DWORD i = 0; i < profileCount; ++i) {
			const auto prefix = ProfilePrefix(i);
			DWORD width = 0, height = 0, output = 0;
			if (ERROR_SUCCESS != key.QueryDWORDValue((prefix + L"Width").c_str(), width)
					|| ERROR_SUCCESS != key.QueryDWORDValue((prefix + L"Height").c_str(), height)
					|| ERROR_SUCCESS != key.QueryDWORDValue((prefix + L"Output").c_str(), output)
					|| width == 0 || width > PROFILE_DIMENSION_MAX
					|| height == 0 || height > PROFILE_DIMENSION_MAX
					|| !isValidOutput(static_cast<int>(output))) {
				continue;
			}
			const bool duplicate = std::ranges::any_of(settings.interpProfiles, [width, height](const auto& profile) {
				return profile.width == width && profile.height == height;
			});
			if (duplicate) {
				continue;
			}

			using Profiles = std::remove_reference_t<decltype(settings.interpProfiles)>;
			typename Profiles::value_type profile = { width, height, static_cast<int>(output), {} };
			wchar_t model[32768] = {};
			ULONG modelLength = static_cast<ULONG>(std::size(model));
			if (ERROR_SUCCESS == key.QueryStringValue((prefix + L"Model").c_str(), model, &modelLength)) {
				profile.model = model;
			}
			settings.interpProfiles.emplace_back(std::move(profile));
		}
	}

	if (ERROR_SUCCESS == key.QueryDWORDValue(SCENE_THRESHOLD, value)) {
		settings.iInterpSceneThreshold = value <= SCENE_THRESHOLD_MAX
			? static_cast<int>(value) : SCENE_THRESHOLD_DEFAULT;
	}
}

template <class Registry, class Settings>
void Save(Registry& key, const Settings& settings)
{
	key.SetDWORDValue(ENABLED, settings.bInterp);

	DWORD storedProfileCount = 0;
	key.QueryDWORDValue(PROFILE_COUNT, storedProfileCount);
	storedProfileCount = (std::min)(storedProfileCount, PROFILE_COUNT_MAX);
	const DWORD profileCount = static_cast<DWORD>((std::min)(
		settings.interpProfiles.size(), static_cast<size_t>(PROFILE_COUNT_MAX)));
	key.SetDWORDValue(DEFAULT_OUTPUT, settings.iInterpDefaultOutput);
	key.SetDWORDValue(PROFILE_COUNT, profileCount);
	for (DWORD i = 0; i < profileCount; ++i) {
		const auto prefix = ProfilePrefix(i);
		const auto& profile = settings.interpProfiles[i];
		key.SetDWORDValue((prefix + L"Width").c_str(), profile.width);
		key.SetDWORDValue((prefix + L"Height").c_str(), profile.height);
		key.SetDWORDValue((prefix + L"Output").c_str(), profile.output);
		key.SetStringValue((prefix + L"Model").c_str(), profile.model.c_str());
	}
	for (DWORD i = profileCount; i < storedProfileCount; ++i) {
		const auto prefix = ProfilePrefix(i);
		key.DeleteValue((prefix + L"Width").c_str());
		key.DeleteValue((prefix + L"Height").c_str());
		key.DeleteValue((prefix + L"Output").c_str());
		key.DeleteValue((prefix + L"Model").c_str());
	}

	key.SetDWORDValue(SCENE_THRESHOLD, settings.iInterpSceneThreshold);
}

} // namespace interp_settings
