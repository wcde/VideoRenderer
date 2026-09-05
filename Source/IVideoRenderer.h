/*
 * (C) 2018-2026 see Authors.txt
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

#include <dxva2api.h>
#include <vector>

enum :int {
	TEXFMT_AUTOINT = 0,
	TEXFMT_8INT = 8,
	TEXFMT_10INT = 10,
	TEXFMT_16FLOAT = 16,
};

enum :int {
	DEINT_Disable = 0,
	DEINT_Enable = 1,
	DEINT_HackFutureFrames = 2,
};

enum :int {
	SUPERRES_Disable = 0,
	SUPERRES_SD,
	SUPERRES_720p,
	SUPERRES_1080p,
	SUPERRES_1440p,
	SUPERRES_COUNT
};

enum :int {
	CHROMA_Nearest = 0,
	CHROMA_Bilinear,
	CHROMA_CatmullRom,
	CHROMA_COUNT
};

enum :int {
	UPSCALE_Nearest = 0,
	UPSCALE_Mitchell,
	UPSCALE_CatmullRom,
	UPSCALE_Lanczos2,
	UPSCALE_Lanczos3,
	UPSCALE_Jinc2,
	UPSCALE_COUNT
};

enum :int {
	DOWNSCALE_Box = 0,
	DOWNSCALE_Bilinear,
	DOWNSCALE_Hamming,
	DOWNSCALE_Bicubic,
	DOWNSCALE_BicubicSharp,
	DOWNSCALE_Lanczos,
	DOWNSCALE_COUNT
};

enum :int {
	SWAPEFFECT_Discard = 0,
	SWAPEFFECT_Flip,
	SWAPEFFECT_COUNT
};

enum :int {
	HDRTD_Disabled = 0,
	HDRTD_On_Fullscreen,
	HDRTD_On,
	HDRTD_OnOff_Fullscreen,
	HDRTD_OnOff
};

constexpr inline auto INTERP_MULT_MIN = 2;
constexpr inline auto INTERP_MULT_MAX = 4;
enum : int {
	INTERP_PROFILE_DISABLE = 0,
	INTERP_PROFILE_X2 = 2,
	INTERP_PROFILE_X3 = 3,
	INTERP_PROFILE_X4 = 4,
	INTERP_PROFILE_DISPLAY_REFRESH = 5,
};
constexpr bool IsValidInterpProfileValue(const int value)
{
	return value == INTERP_PROFILE_DISABLE
		|| value >= INTERP_PROFILE_X2 && value <= INTERP_PROFILE_X4
		|| value == INTERP_PROFILE_DISPLAY_REFRESH;
}

constexpr inline int INTERP_SCENE_THRESHOLD_UI_MAX = 100;
constexpr inline int INTERP_SCENE_THRESHOLD_UNITS_PER_PERCENT = 20;
constexpr float InterpSceneThresholdRatio(const int value)
{
	const int clamped = value < 0 ? 0 : value > INTERP_SCENE_THRESHOLD_UI_MAX
		? INTERP_SCENE_THRESHOLD_UI_MAX : value;
	return static_cast<float>(clamped)
		/ (100.0f * INTERP_SCENE_THRESHOLD_UNITS_PER_PERCENT);
}

struct InterpProfile_t {
	UINT width = 0;
	UINT height = 0;
	int output = INTERP_PROFILE_X2;
	std::wstring model; // empty = use the default ONNX model

	bool operator==(const InterpProfile_t&) const = default;
};

inline const InterpProfile_t* FindNearestInterpProfile(
		const std::vector<InterpProfile_t>& profiles, const unsigned long long pixels)
{
	const InterpProfile_t* best = nullptr;
	unsigned long long bestPixels = 0;
	unsigned long long bestDistance = 0;
	for (const auto& profile : profiles) {
		const unsigned long long profilePixels = static_cast<unsigned long long>(profile.width) * profile.height;
		const unsigned long long distance = pixels > profilePixels ? pixels - profilePixels : profilePixels - pixels;
		if (!best || distance < bestDistance || (distance == bestDistance && profilePixels < bestPixels)) {
			best = &profile;
			bestPixels = profilePixels;
			bestDistance = distance;
		}
	}
	return best;
}
constexpr inline int GPU_ADAPTER_AUTO = -1;

#define SDR_NITS_DEF 125
#define SDR_NITS_MIN  25
#define SDR_NITS_MAX 400
#define SDR_NITS_STEP  5

constexpr inline auto HDR_NITS_DEF = 1000;
constexpr inline auto HDR_NITS_MIN = 100;
constexpr inline auto HDR_NITS_MAX = 10000;

struct VPEnableFormats_t {
	bool bNV12;
	bool bP01x;
	bool bYUY2;
	bool bOther;
};

struct Settings_t {
	bool bUseD3D11;
	bool bShowStats;
	int  iResizeStats;
	int  iTexFormat;
	VPEnableFormats_t VPFmts;
	int  iVPDeinterlacing;
	bool bDeintDouble;
	bool bVPScaling;
	int iVPSuperRes;
	bool bVPRTXVideoHDR;
	int  iChromaScaling;
	int  iUpscaling;
	int  iDownscaling;
	bool bInterpolateAt50pct;
	bool bUseDither;
	bool bDeintBlend;
	int  iSwapEffect;
	bool bExclusiveFS;
	bool bVBlankBeforePresent;
	bool bAdjustPresentTime;
	bool bReinitByDisplay;
	bool bHdrPreferDoVi;
	bool bHdrPassthrough;
	int  iHdrToggleDisplay;
	int  iHdrOsdBrightness;
	bool bConvertToSdr;
	int  iSDRDisplayNits;
	bool bHdrLocalToneMapping;
	int  iHdrLocalToneMappingType;
	int iHdrDisplayMaxNits;
	// RIFE frame interpolation (Direct3D 11, x64, NVIDIA)
	bool bInterp;
	int  iInterpDefaultOutput;
	std::vector<InterpProfile_t> interpProfiles;
	bool bInterpFP16;
	int  iInterpSceneThreshold; // UI scale 0..100, 20 units = one NVOFA inlier percent, 0 = off
	int  iInterpPadMultiple;    // 0 = auto
	std::wstring strInterpModel;
	std::wstring strInterpTrtDir;
	int  iGpuAdapter;           // DXGI adapter index, -1 = adapter driving the display

	Settings_t() {
		SetDefault();
	}

	void SetDefault() {
		if (IsWindows8OrGreater()) {
			bUseD3D11                   = true;
		} else {
			bUseD3D11                   = false;
		}
		bShowStats                      = false;
		iResizeStats                    = 0;
		iTexFormat                      = TEXFMT_AUTOINT;
		VPFmts.bNV12                    = true;
		VPFmts.bP01x                    = true;
		VPFmts.bYUY2                    = true;
		VPFmts.bOther                   = true;
		iVPDeinterlacing                = DEINT_Enable;
		bDeintDouble                    = true;
		bVPScaling                      = true;
		iVPSuperRes                     = SUPERRES_Disable;
		bVPRTXVideoHDR                  = false;
		iChromaScaling                  = CHROMA_Bilinear;
		iUpscaling                      = UPSCALE_CatmullRom;
		iDownscaling                    = DOWNSCALE_Hamming;
		bInterpolateAt50pct             = true;
		bUseDither                      = true;
		bDeintBlend                     = false;
		iSwapEffect                     = SWAPEFFECT_Flip;
		bExclusiveFS                    = false;
		bVBlankBeforePresent            = false;
		bAdjustPresentTime              = true;
		bReinitByDisplay                = false;
		bHdrPreferDoVi                  = false;
		if (IsWindows10OrGreater()) {
			bHdrPassthrough             = true;
			bHdrLocalToneMapping        = false;
			iHdrLocalToneMappingType    = 1;
			iHdrDisplayMaxNits          = 1000;
		} else {
			bHdrPassthrough             = false;
			bHdrLocalToneMapping        = false;
			iHdrLocalToneMappingType    = 1;
			iHdrDisplayMaxNits          = 1000;
		}
		iHdrToggleDisplay               = HDRTD_Disabled;
		bConvertToSdr                   = true;
		iHdrOsdBrightness               = 0;
		iSDRDisplayNits                 = SDR_NITS_DEF;
		bInterp                         = false;
		iInterpDefaultOutput            = INTERP_PROFILE_X2;
		interpProfiles                  = {
			{ 1280, 720, INTERP_PROFILE_X2, {} }, { 1920, 1080, INTERP_PROFILE_X2, {} },
			{ 2560, 1440, INTERP_PROFILE_X2, {} }, { 3840, 2160, INTERP_PROFILE_DISABLE, {} }
		};
		bInterpFP16                     = true;
		iInterpSceneThreshold           = 25;
		iInterpPadMultiple              = 0;
		strInterpModel.clear();
		strInterpTrtDir.clear();
		iGpuAdapter                     = GPU_ADAPTER_AUTO;
	}
};

interface __declspec(uuid("1AB00F10-5F55-42AC-B53F-38649F11BE3E"))
IVideoRenderer : public IUnknown {
	STDMETHOD(GetVideoProcessorInfo) (std::wstring& str) PURE;
	STDMETHOD_(bool, GetActive()) PURE;

	STDMETHOD_(void, GetSettings(Settings_t& setings)) PURE;
	STDMETHOD_(void, SetSettings(const Settings_t& setings)) PURE;

	STDMETHOD(SaveSettings()) PURE;

	STDMETHOD(GetInterpolationStatus) (std::wstring& str) PURE;
};
