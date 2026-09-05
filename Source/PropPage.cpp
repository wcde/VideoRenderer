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

#include "stdafx.h"
#include "resource.h"
#include "Helper.h"
#include "DisplayConfig.h"
#include "PropPage.h"
#ifdef _WIN64
#include <shobjidl.h>
#include <dxgi1_2.h>
#endif

void SetCursor(HWND hWnd, LPCWSTR lpCursorName)
{
	SetClassLongPtrW(hWnd, GCLP_HCURSOR, (LONG_PTR)::LoadCursorW(nullptr, lpCursorName));
}

void SetCursor(HWND hWnd, UINT nID, LPCWSTR lpCursorName)
{
	SetCursor(::GetDlgItem(hWnd, nID), lpCursorName);
}

inline void ComboBox_AddStringData(HWND hWnd, int nIDComboBox, LPCWSTR str, LONG_PTR data)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_ADDSTRING, 0, (LPARAM)str);
	if (lValue != CB_ERR) {
		SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETITEMDATA, lValue, data);
	}
}

inline LONG_PTR ComboBox_GetCurItemData(HWND hWnd, int nIDComboBox)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCURSEL, 0, 0);
	if (lValue != CB_ERR) {
		lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, lValue, 0);
	}
	return lValue;
}

void ComboBox_SelectByItemData(HWND hWnd, int nIDComboBox, LONG_PTR data)
{
	LRESULT lCount = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCOUNT, 0, 0);
	if (lCount != CB_ERR) {
		for (int idx = 0; idx < lCount; idx++) {
			const LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, idx, 0);
			if (data == lValue) {
				SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETCURSEL, idx, 0);
				break;
			}
		}
	}
}


// CVRMainPPage

// https://msdn.microsoft.com/ru-ru/library/windows/desktop/dd375010(v=vs.85).aspx

CVRMainPPage::CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"MainProp", lpunk, IDD_MAINPROPPAGE, IDS_MAINPROPPAGE_TITLE)
{
	DLog(L"CVRMainPPage()");
}

CVRMainPPage::~CVRMainPPage()
{
	DLog(L"~CVRMainPPage()");
}

void CVRMainPPage::SetControls()
{
	CheckDlgButton(IDC_CHECK1, m_SetsPP.bUseD3D11             ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK2, m_SetsPP.bShowStats            ? BST_CHECKED : BST_UNCHECKED);

	ComboBox_SelectByItemData(m_hWnd, IDC_COMBO1, m_SetsPP.iTexFormat);

	CheckDlgButton(IDC_CHECK7, m_SetsPP.VPFmts.bNV12          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK8, m_SetsPP.VPFmts.bP01x          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK9, m_SetsPP.VPFmts.bYUY2          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK4, m_SetsPP.VPFmts.bOther         ? BST_CHECKED : BST_UNCHECKED);
	SendDlgItemMessageW(IDC_COMBO9, CB_SETCURSEL, m_SetsPP.iVPDeinterlacing, 0);
	CheckDlgButton(IDC_CHECK3, m_SetsPP.bDeintDouble          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK5, m_SetsPP.bVPScaling            ? BST_CHECKED : BST_UNCHECKED);
	SendDlgItemMessageW(IDC_COMBO8, CB_SETCURSEL, m_SetsPP.iVPSuperRes, 0);
	CheckDlgButton(IDC_CHECK19, m_SetsPP.bVPRTXVideoHDR       ? BST_CHECKED : BST_UNCHECKED);

	if (m_SetsPP.bHdrPassthrough) {
		ComboBox_SelectByItemData(m_hWnd, IDC_COMBO10, 0);
	} else if (m_SetsPP.bHdrLocalToneMapping) {
		ComboBox_SelectByItemData(m_hWnd, IDC_COMBO10, m_SetsPP.iHdrLocalToneMappingType);
	} else {
		ComboBox_SelectByItemData(m_hWnd, IDC_COMBO10, -1);
	}

	CheckDlgButton(IDC_CHECK18, m_SetsPP.bHdrPreferDoVi       ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK14, m_SetsPP.bConvertToSdr        ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO7, CB_SETCURSEL, m_SetsPP.iHdrToggleDisplay, 0);
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETPOS, 1, m_SetsPP.iHdrOsdBrightness);

	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETPOS, 1, m_SetsPP.iSDRDisplayNits / SDR_NITS_STEP);
	GetDlgItem(IDC_EDIT1).SetWindowTextW(std::to_wstring(m_SetsPP.iSDRDisplayNits).c_str());

	CheckDlgButton(IDC_CHECK6, m_SetsPP.bInterpolateAt50pct   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK10, m_SetsPP.bUseDither           ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK17, m_SetsPP.bDeintBlend          ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(IDC_CHECK11, m_SetsPP.bExclusiveFS         ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK15, m_SetsPP.bVBlankBeforePresent ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK13, m_SetsPP.bAdjustPresentTime   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK16, m_SetsPP.bReinitByDisplay     ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO6, CB_SETCURSEL, m_SetsPP.iResizeStats, 0);

	SendDlgItemMessageW(IDC_COMBO5, CB_SETCURSEL, m_SetsPP.iChromaScaling, 0);
	SendDlgItemMessageW(IDC_COMBO2, CB_SETCURSEL, m_SetsPP.iUpscaling, 0);
	SendDlgItemMessageW(IDC_COMBO3, CB_SETCURSEL, m_SetsPP.iDownscaling, 0);
	SendDlgItemMessageW(IDC_COMBO4, CB_SETCURSEL, m_SetsPP.iSwapEffect, 0);

	m_SetsPP.iHdrDisplayMaxNits = discard<int>(m_SetsPP.iHdrDisplayMaxNits, HDR_NITS_DEF, HDR_NITS_MIN, HDR_NITS_MAX);
	SetDlgItemTextW(IDC_EDIT_DISPLAYMAX, std::to_wstring(m_SetsPP.iHdrDisplayMaxNits).c_str());
}

void CVRMainPPage::EnableControls()
{
	if (!IsWindows8OrGreater()) { // Windows 7
		const BOOL bEnable = !m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_STATIC1).EnableWindow(bEnable); // not working for GROUPBOX
		GetDlgItem(IDC_STATIC2).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK7).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK8).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK9).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK4).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK3).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK5).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC3).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO4).EnableWindow(bEnable);
	}
	else if (IsWindows10OrGreater()) {
		const BOOL bEnable = m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_COMBO10).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC5).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO7).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC6).EnableWindow(bEnable);
		GetDlgItem(IDC_SLIDER1).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC7).EnableWindow(bEnable && m_SetsPP.bVPScaling);
		GetDlgItem(IDC_COMBO8).EnableWindow(bEnable && m_SetsPP.bVPScaling);
#ifdef _WIN64
		GetDlgItem(IDC_CHECK19).EnableWindow(bEnable && m_SetsPP.bHdrPassthrough);
#endif
	}

	GetDlgItem(IDC_STATIC8).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_EDIT1).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_SLIDER2).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_EDIT_DISPLAYMAX).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
}

HRESULT CVRMainPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRMainPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	if (m_SetsPP.iSDRDisplayNits != m_oldSDRDisplayNits) {
		// OK or Apply buttons were not pressed. cancel the settings.
		m_pVideoRenderer->GetSettings(m_SetsPP);
		m_SetsPP.iSDRDisplayNits = m_oldSDRDisplayNits;
		m_pVideoRenderer->SetSettings(m_SetsPP);
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HRESULT CVRMainPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;

	m_pVideoRenderer->GetSettings(m_SetsPP);
	m_oldSDRDisplayNits = m_SetsPP.iSDRDisplayNits;

	if (!IsWindows7SP1OrGreater()) {
		GetDlgItem(IDC_CHECK1).EnableWindow(FALSE);
		m_SetsPP.bUseD3D11 = false;
	}
	if (!IsWindows10OrGreater()) {
		GetDlgItem(IDC_COMBO10).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC5).EnableWindow(FALSE);
		GetDlgItem(IDC_COMBO7).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC6).EnableWindow(FALSE);
		GetDlgItem(IDC_SLIDER1).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC7).EnableWindow(FALSE);
		GetDlgItem(IDC_COMBO8).EnableWindow(FALSE);
		GetDlgItem(IDC_CHECK19).EnableWindow(FALSE);
	}

#ifndef _WIN64
	GetDlgItem(IDC_STATIC7).EnableWindow(FALSE);
	GetDlgItem(IDC_COMBO8).EnableWindow(FALSE);
	GetDlgItem(IDC_CHECK19).EnableWindow(FALSE);
#endif

	EnableControls();

	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Fixed font size");
	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Increase font by window");

	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"Auto 8/10-bit Integer",  0);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"8-bit Integer",          8);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"10-bit Integer",        10);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"16-bit Floating Point", 16);

	SendDlgItemMessageW(IDC_COMBO9, CB_ADDSTRING, 0, (LPARAM)L"Disable");
	SendDlgItemMessageW(IDC_COMBO9, CB_ADDSTRING, 0, (LPARAM)L"Enable");
	SendDlgItemMessageW(IDC_COMBO9, CB_ADDSTRING, 0, (LPARAM)L"HACK future frames");

	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"Disable");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for SD");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 720p");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 1080p");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 1440p");

	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Do not change");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on (fullscreen)");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on/off (fullscreen)");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on/off");

	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");

	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Mitchell-Netravali");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos2");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos3");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Jinc2m");

	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Box");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Hamming");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic sharp");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Lanczos");

	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Discard");
	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Flip");

	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETRANGE, 0, MAKELONG(0, 2));
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETTIC, 0, 1);

	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETRANGE, 0, MAKELONG(SDR_NITS_MIN / SDR_NITS_STEP, SDR_NITS_MAX / SDR_NITS_STEP));
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETTIC, 0, SDR_NITS_DEF / SDR_NITS_STEP);
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETLINESIZE, 0, 1); // arrow keys
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETPAGESIZE, 0, 5); // clicks on trackbar's channel

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Ignore", -1);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Passthrough", 0);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"ACES", 1);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Reinhard", 2);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Hable", 3);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Mobius", 4);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"BT2390/ST 2094-10", 5);

	SetControls();

	SetCursor(m_hWnd, IDC_ARROW);
	SetCursor(m_hWnd, IDC_COMBO1, IDC_HAND);

	AddHint(IDC_CHECK5,
		L"It works fast, but it's not always good.\n"
		"Disable it if you want to use shaders for resizing.");
	AddHint(IDC_COMBO8,
		L"Available for Direct3D 11.\n"
		"Requires hardware and driver support:\n"
		"- Intel Graphics UHD 610 or later\n"
		"- Nvidia RTX (x64 only)");
	AddHint(IDC_CHECK19,
		L"Available for Direct3D 11.\n"
		"Requires hardware and driver support:\n"
		"- Nvidia RTX (x64 only)");
	AddHint(IDC_COMBO5,
		L"Used for YUV 4:2:0/4:2:2 input formats\n"
		"when the DVXA2/D3D11 Video Processor is not active.");
	AddHint(IDC_COMBO2,
		L"Used to increase image size when the\n"
		"DVXA2/D3D11 Video Processor is not used for resizing.");
	AddHint(IDC_COMBO3,
		L"Used to reduce image size when the\n"
		"DVXA2/D3D11 Video Processor is not used for resizing.");
	AddHint(IDC_COMBO4,
		L"'Flip' is more efficient, but 'Discard' may work\n"
		"more correctly in some rare situations.");

	m_bActivated = true;

	return S_OK;
}

INT_PTR CVRMainPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_COMMAND) {
		LRESULT lValue;
		const int nID = LOWORD(wParam);
		int action = HIWORD(wParam);

		if (action == BN_CLICKED) {
			if (nID == IDC_CHECK1) {
				m_SetsPP.bUseD3D11 = IsDlgButtonChecked(IDC_CHECK1) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK2) {
				m_SetsPP.bShowStats = IsDlgButtonChecked(IDC_CHECK2) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK3) {
				m_SetsPP.bDeintDouble = IsDlgButtonChecked(IDC_CHECK3) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK5) {
				m_SetsPP.bVPScaling = IsDlgButtonChecked(IDC_CHECK5) == BST_CHECKED;
				SetDirty();
				GetDlgItem(IDC_STATIC7).EnableWindow(m_SetsPP.bVPScaling && m_SetsPP.bUseD3D11 && IsWindows10OrGreater());
				GetDlgItem(IDC_COMBO8).EnableWindow(m_SetsPP.bVPScaling && m_SetsPP.bUseD3D11 && IsWindows10OrGreater());
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK6) {
				m_SetsPP.bInterpolateAt50pct = IsDlgButtonChecked(IDC_CHECK6) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK7) {
				m_SetsPP.VPFmts.bNV12 = IsDlgButtonChecked(IDC_CHECK7) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK8) {
				m_SetsPP.VPFmts.bP01x = IsDlgButtonChecked(IDC_CHECK8) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK9) {
				m_SetsPP.VPFmts.bYUY2 = IsDlgButtonChecked(IDC_CHECK9) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK4) {
				m_SetsPP.VPFmts.bOther = IsDlgButtonChecked(IDC_CHECK4) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK10) {
				m_SetsPP.bUseDither = IsDlgButtonChecked(IDC_CHECK10) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK17) {
				m_SetsPP.bDeintBlend = IsDlgButtonChecked(IDC_CHECK17) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK11) {
				m_SetsPP.bExclusiveFS = IsDlgButtonChecked(IDC_CHECK11) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK15) {
				m_SetsPP.bVBlankBeforePresent = IsDlgButtonChecked(IDC_CHECK15) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK13) {
				m_SetsPP.bAdjustPresentTime = IsDlgButtonChecked(IDC_CHECK13) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK16) {
				m_SetsPP.bReinitByDisplay = IsDlgButtonChecked(IDC_CHECK16) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK18) {
				m_SetsPP.bHdrPreferDoVi = IsDlgButtonChecked(IDC_CHECK18) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK14) {
				m_SetsPP.bConvertToSdr = IsDlgButtonChecked(IDC_CHECK14) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK19) {
				m_SetsPP.bVPRTXVideoHDR = IsDlgButtonChecked(IDC_CHECK19) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}

			if (nID == IDC_BUTTON1) {
				m_SetsPP.SetDefault();
				SetControls();
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
		}

		if (action == CBN_SELCHANGE) {
			if (nID == IDC_COMBO6) {
				lValue = SendDlgItemMessageW(IDC_COMBO6, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iResizeStats) {
					m_SetsPP.iResizeStats = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO1) {
				lValue = ComboBox_GetCurItemData(m_hWnd, IDC_COMBO1);
				if (lValue != m_SetsPP.iTexFormat) {
					m_SetsPP.iTexFormat = lValue;
					SetDirty();
#ifdef _WIN64
					GetDlgItem(IDC_CHECK19).EnableWindow(m_SetsPP.bUseD3D11 && m_SetsPP.bHdrPassthrough && m_SetsPP.iTexFormat != TEXFMT_8INT);
#endif
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO9) {
				lValue = SendDlgItemMessageW(IDC_COMBO9, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iVPDeinterlacing) {
					m_SetsPP.iVPDeinterlacing = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO8) {
				lValue = SendDlgItemMessageW(IDC_COMBO8, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iVPSuperRes) {
					m_SetsPP.iVPSuperRes = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO7) {
				lValue = SendDlgItemMessageW(IDC_COMBO7, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iHdrToggleDisplay) {
					m_SetsPP.iHdrToggleDisplay = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO5) {
				lValue = SendDlgItemMessageW(IDC_COMBO5, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iChromaScaling) {
					m_SetsPP.iChromaScaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO2) {
				lValue = SendDlgItemMessageW(IDC_COMBO2, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iUpscaling) {
					m_SetsPP.iUpscaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO3) {
				lValue = SendDlgItemMessageW(IDC_COMBO3, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iDownscaling) {
					m_SetsPP.iDownscaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO4) {
				lValue = SendDlgItemMessageW(IDC_COMBO4, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iSwapEffect) {
					m_SetsPP.iSwapEffect = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO10) {
				lValue = SendDlgItemMessageW(IDC_COMBO10, CB_GETCURSEL, 0, 0);
				switch (lValue) {
					case 0:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = false;
						break;
					case 1:
						m_SetsPP.bHdrPassthrough = true;
						m_SetsPP.bHdrLocalToneMapping = false;
						break;
					case 2:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 1;
						break;
					case 3:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 2;
						break;
					case 4:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 3;
						break;
					case 5:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 4;
						break;
					case 6:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 5;
						break;
					default:
						break;
				}
				SetDirty();
				EnableControls();
				return (LRESULT)1;
			}
		}
		if (action == EN_CHANGE) {
			if (nID == IDC_EDIT_DISPLAYMAX) {
				SetDirty();
			}
		}
	}
	else if (uMsg == WM_HSCROLL) {
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER1)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER1, TBM_GETPOS, 0, 0);
			if (lValue != m_SetsPP.iHdrOsdBrightness) {
				m_SetsPP.iHdrOsdBrightness = lValue;
				SetDirty();
			}
			return (LRESULT)1;
		}
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER2)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER2, TBM_GETPOS, 0, 0);
			lValue *= SDR_NITS_STEP;
			if (lValue != m_SetsPP.iSDRDisplayNits) {
				m_SetsPP.iSDRDisplayNits = lValue;
				GetDlgItem(IDC_EDIT1).SetWindowTextW(std::to_wstring(m_SetsPP.iSDRDisplayNits).c_str());
				SetDirty();
				{
					// apply only SDRDisplayNits
					Settings_t sets;
					m_pVideoRenderer->GetSettings(sets);
					sets.iSDRDisplayNits = m_SetsPP.iSDRDisplayNits;
					m_pVideoRenderer->SetSettings(sets);
				}
			}
			return (LRESULT)1;
		}
	}

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRMainPPage::OnApplyChanges()
{
	BOOL translated = FALSE;
	int displayMaxNits = GetDlgItemInt(IDC_EDIT_DISPLAYMAX, &translated, FALSE);
	if (!translated) {
		MessageBoxW(L"Invalid HDR Brightness. Please enter a valid number from 100 to 10000.", L"Error", MB_OK | MB_ICONERROR);
	}
	else if (displayMaxNits <= HDR_NITS_MIN || displayMaxNits > HDR_NITS_MAX) {
		MessageBoxW(L"Invalid HDR Brightness. Please enter a valid number from 100 to 10000.", L"Error", MB_OK | MB_ICONERROR);
	}
	else {
		m_SetsPP.iHdrDisplayMaxNits = displayMaxNits;
	}

	m_pVideoRenderer->SetSettings(m_SetsPP);
	m_pVideoRenderer->SaveSettings();

	m_oldSDRDisplayNits = m_SetsPP.iSDRDisplayNits;

	return S_OK;
}

HWND CVRMainPPage::CreateHintWindow(HWND parent, int timePop, int timeInit, int timeReshow)
{
	HWND hhint = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, nullptr, nullptr);

	SetWindowPos(hhint, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELONG(timePop, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELONG(timeInit, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELONG(timeReshow, 0));
	SendMessageW(hhint, TTM_SETMAXTIPWIDTH, 0, 470);
	return hhint;
}

void CVRMainPPage::AddHint(int id, const LPCWSTR text)
{
	if (!m_hHint) {
		m_hHint = CreateHintWindow(m_Dlg, 15000);
	}
	TOOLINFOW ti;
	ti.cbSize = sizeof(TOOLINFOW);
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = m_Dlg;
	ti.uId = (LPARAM)GetDlgItem(id).m_hWnd;
	ti.lpszText = const_cast<LPWSTR>(text);
	SendMessageW(m_hHint, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// CVRInfoPPage

CVRInfoPPage::CVRInfoPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"InfoProp", lpunk, IDD_INFOPROPPAGE, IDS_INFOPROPPAGE_TITLE)
{
	DLog(L"CVRInfoPPage()");
}

CVRInfoPPage::~CVRInfoPPage()
{
	DLog(L"~CVRInfoPPage()");

	if (m_hMonoFont) {
		DeleteObject(m_hMonoFont);
		m_hMonoFont = 0;
	}
}

HRESULT CVRInfoPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRInfoPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HWND GetParentOwner(HWND hwnd)
{
	HWND hWndParent = hwnd;
	HWND hWndT;
	while ((::GetWindowLongPtrW(hWndParent, GWL_STYLE) & WS_CHILD) &&
		(hWndT = ::GetParent(hWndParent)) != NULL) {
		hWndParent = hWndT;
	}

	return hWndParent;
}

static WNDPROC OldControlProc;
static LRESULT CALLBACK ControlProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_KEYDOWN && LOWORD(wParam) == VK_ESCAPE) {
		// fixed Esc handling when EDITTEXT control has ES_MULTILINE property and is in focus
		HWND parentOwner = GetParentOwner(control);
		if (parentOwner) {
			::PostMessageW(parentOwner, WM_COMMAND, IDCANCEL, 0);
		}
		return TRUE;
	}

	return CallWindowProcW(OldControlProc, control, message, wParam, lParam); // call edit control's own windowproc
}

HRESULT CVRInfoPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	// init monospace font
	LOGFONTW lf = {};
	HDC hdc = GetWindowDC();
	lf.lfHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(hdc);
	lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
	wcscpy_s(lf.lfFaceName, L"Consolas");
	m_hMonoFont = CreateFontIndirectW(&lf);

	GetDlgItem(IDC_EDIT1).SetFont(m_hMonoFont);
	ASSERT(m_pVideoRenderer);

	if (!m_pVideoRenderer->GetActive()) {
		SetDlgItemTextW(IDC_EDIT1, L"filter is not active");
		return S_OK;
	}

	std::wstring strInfo(L"Windows ");
	strInfo.append(GetWindowsVersion());
	strInfo.append(L"\r\n");

	std::wstring strVP;
	if (S_OK == m_pVideoRenderer->GetVideoProcessorInfo(strVP)) {
		str_replace(strVP, L"\n", L"\r\n");
		strInfo.append(strVP);
	}

#ifdef _DEBUG
	{
		std::vector<DisplayConfig_t> displayConfigs;

		bool ret = GetDisplayConfigs(displayConfigs);

		strInfo.append(L"\r\n");

		for (const auto& dc : displayConfigs) {
			double freq = (double)dc.refreshRate.Numerator / (double)dc.refreshRate.Denominator;
			strInfo += std::format(L"\r\n{} - {:.3f} Hz", dc.displayName, freq);

			if (dc.bitsPerChannel) { // if bitsPerChannel is not set then colorEncoding and other values are invalid
				const wchar_t* colenc = ColorEncodingToString(dc.colorEncoding);
				if (colenc) {
					strInfo += std::format(L" {}", colenc);
				}
				strInfo += std::format(L" {}-bit", dc.bitsPerChannel);
			}

			const wchar_t* output = OutputTechnologyToString(dc.outputTechnology);
			if (output) {
				strInfo += std::format(L" {}", output);
			}
		}
	}
#endif

	SetDlgItemTextW(IDC_EDIT1, strInfo.c_str());

	OldControlProc = (WNDPROC)::SetWindowLongPtrW(::GetDlgItem(m_hWnd, IDC_EDIT1), GWLP_WNDPROC, (LONG_PTR)ControlProc);

	return S_OK;
}

#ifdef _WIN64

// CVRInterpPPage

namespace {

constexpr UINT_PTR INTERP_STATUS_TIMER = 1;

void PopulateInterpProfile(HWND hwnd, const int controlId)
{
	ComboBox_AddStringData(hwnd, controlId, L"Disable", INTERP_PROFILE_DISABLE);
	ComboBox_AddStringData(hwnd, controlId, L"x2", INTERP_PROFILE_X2);
	ComboBox_AddStringData(hwnd, controlId, L"x3", INTERP_PROFILE_X3);
	ComboBox_AddStringData(hwnd, controlId, L"x4", INTERP_PROFILE_X4);
	ComboBox_AddStringData(hwnd, controlId, L"Display refresh rate", INTERP_PROFILE_DISPLAY_REFRESH);
}

const wchar_t* InterpProfileOutputName(const int value)
{
	switch (value) {
	case INTERP_PROFILE_DISABLE: return L"Disable";
	case INTERP_PROFILE_X2: return L"x2";
	case INTERP_PROFILE_X3: return L"x3";
	case INTERP_PROFILE_X4: return L"x4";
	case INTERP_PROFILE_DISPLAY_REFRESH: return L"Display refresh rate";
	default: return L"Invalid";
	}
}

void FitInterpProfileColumns(const HWND list)
{
	RECT rect = {};
	if (!GetClientRect(list, &rect)) {
		return;
	}
	const int width = rect.right - rect.left;
	if (width <= 0) {
		return;
	}
	const int resolutionWidth = width * 28 / 100;
	const int outputWidth = width * 34 / 100;
	ListView_SetColumnWidth(list, 0, resolutionWidth);
	ListView_SetColumnWidth(list, 1, outputWidth);
	ListView_SetColumnWidth(list, 2, width - resolutionWidth - outputWidth);
}

// Shows the common file/folder dialog and returns the selected path.
bool PickPath(HWND hwndOwner, const bool folder, std::wstring& path)
{
	CComPtr<IFileOpenDialog> pDialog;
	if (FAILED(pDialog.CoCreateInstance(CLSID_FileOpenDialog))) {
		return false;
	}
	DWORD options = 0;
	pDialog->GetOptions(&options);
	options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST;
	if (folder) {
		options |= FOS_PICKFOLDERS;
	}
	pDialog->SetOptions(options);
	if (!folder) {
		const COMDLG_FILTERSPEC filters[] = {
			{ L"ONNX models (*.onnx)", L"*.onnx" },
			{ L"All files (*.*)", L"*.*" },
		};
		pDialog->SetFileTypes((UINT)std::size(filters), filters);
	}
	if (FAILED(pDialog->Show(hwndOwner))) {
		return false;
	}
	CComPtr<IShellItem> pItem;
	if (FAILED(pDialog->GetResult(&pItem))) {
		return false;
	}
	PWSTR pszPath = nullptr;
	if (FAILED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
		return false;
	}
	path = pszPath;
	CoTaskMemFree(pszPath);
	return true;
}

std::wstring GetDlgItemString(CWindow& wnd, const int id)
{
	wchar_t buf[1024] = {};
	wnd.GetDlgItemTextW(id, buf, (int)std::size(buf));
	return buf;
}

std::wstring GetDlgItemString(HWND hwnd, const int id)
{
	const int length = GetWindowTextLengthW(GetDlgItem(hwnd, id));
	std::wstring value(length + 1, L'\0');
	if (length) {
		GetDlgItemTextW(hwnd, id, value.data(), length + 1);
	}
	value.resize(length);
	return value;
}

struct InterpProfileDialogData {
	InterpProfile_t profile = { 1920, 1080, INTERP_PROFILE_X2, {} };
	bool editing = false;
};

INT_PTR CALLBACK InterpProfileDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto* data = reinterpret_cast<InterpProfileDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (message == WM_INITDIALOG) {
		data = reinterpret_cast<InterpProfileDialogData*>(lParam);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
		if (data->editing) {
			SetWindowTextW(hwnd, L"Edit interpolation profile");
		}
		SetDlgItemInt(hwnd, IDC_EDIT6, data->profile.width, FALSE);
		SetDlgItemInt(hwnd, IDC_EDIT7, data->profile.height, FALSE);
		PopulateInterpProfile(hwnd, IDC_COMBO16);
		ComboBox_SelectByItemData(hwnd, IDC_COMBO16, data->profile.output);
		SetDlgItemTextW(hwnd, IDC_EDIT8, data->profile.model.c_str());
		return TRUE;
	}
	if (message != WM_COMMAND || !data) {
		return FALSE;
	}
	const int id = LOWORD(wParam);
	if (id == IDC_BUTTON6 && HIWORD(wParam) == BN_CLICKED) {
		std::wstring path;
		if (PickPath(hwnd, false, path)) {
			SetDlgItemTextW(hwnd, IDC_EDIT8, path.c_str());
		}
		return TRUE;
	}
	if (id == IDOK) {
		BOOL widthValid = FALSE, heightValid = FALSE;
		const UINT width = GetDlgItemInt(hwnd, IDC_EDIT6, &widthValid, FALSE);
		const UINT height = GetDlgItemInt(hwnd, IDC_EDIT7, &heightValid, FALSE);
		const LRESULT selection = SendDlgItemMessageW(hwnd, IDC_COMBO16, CB_GETCURSEL, 0, 0);
		const int output = selection == CB_ERR ? -1
			: static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO16, CB_GETITEMDATA, selection, 0));
		if (!widthValid || !heightValid || width == 0 || height == 0 || width > 16384 || height > 16384
				|| !IsValidInterpProfileValue(output)) {
			MessageBoxW(hwnd, L"Enter a resolution from 1x1 to 16384x16384 and select an output mode.",
				L"Invalid interpolation profile", MB_OK | MB_ICONWARNING);
			return TRUE;
		}
		data->profile = { width, height, output, GetDlgItemString(hwnd, IDC_EDIT8) };
		EndDialog(hwnd, IDOK);
		return TRUE;
	}
	if (id == IDCANCEL) {
		EndDialog(hwnd, IDCANCEL);
		return TRUE;
	}
	return FALSE;
}

} // namespace

CVRInterpPPage::CVRInterpPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"InterpProp", lpunk, IDD_INTERPPROPPAGE, IDS_INTERPPROPPAGE_TITLE)
{
	DLog(L"CVRInterpPPage()");
}

CVRInterpPPage::~CVRInterpPPage()
{
	DLog(L"~CVRInterpPPage()");
}

HRESULT CVRInterpPPage::OnConnect(IUnknown* pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRInterpPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HRESULT CVRInterpPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;

	m_pVideoRenderer->GetSettings(m_SetsPP);
	PopulateGpuAdapters();

	PopulateInterpProfile(m_hWnd, IDC_COMBO11);
	const HWND list = GetDlgItem(IDC_LIST1);
	ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	LVCOLUMNW column = { LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM };
	column.pszText = const_cast<LPWSTR>(L"Resolution");
	column.cx = 0;
	ListView_InsertColumn(list, 0, &column);
	column.pszText = const_cast<LPWSTR>(L"Output");
	column.iSubItem = 1;
	ListView_InsertColumn(list, 1, &column);
	column.pszText = const_cast<LPWSTR>(L"Model");
	column.iSubItem = 2;
	ListView_InsertColumn(list, 2, &column);

	SendDlgItemMessageW(IDC_SLIDER3, TBM_SETRANGE, 0, MAKELONG(0, INTERP_SCENE_THRESHOLD_UI_MAX));
	SendDlgItemMessageW(IDC_SLIDER3, TBM_SETTIC, 0, 10);
	SendDlgItemMessageW(IDC_SLIDER3, TBM_SETLINESIZE, 0, 1);
	SendDlgItemMessageW(IDC_SLIDER3, TBM_SETPAGESIZE, 0, 10);

	SetControls();
	UpdateStatus();

	SetCursor(m_hWnd, IDC_ARROW);
	SetTimer(INTERP_STATUS_TIMER, 1000);

	m_bActivated = true;

	return S_OK;
}

HRESULT CVRInterpPPage::OnDeactivate()
{
	m_bActivated = false;
	KillTimer(INTERP_STATUS_TIMER);

	return S_OK;
}

void CVRInterpPPage::SetControls()
{
	CheckDlgButton(IDC_CHECK20, m_SetsPP.bInterp ? BST_CHECKED : BST_UNCHECKED);
	ComboBox_SelectByItemData(m_hWnd, IDC_COMBO13, static_cast<LONG_PTR>(m_SetsPP.iGpuAdapter) + 1);
	ComboBox_SelectByItemData(m_hWnd, IDC_COMBO11, m_SetsPP.iInterpDefaultOutput);
	UpdateProfileList();
	SetDlgItemTextW(IDC_EDIT3, m_SetsPP.strInterpModel.c_str());
	SetDlgItemTextW(IDC_EDIT4, m_SetsPP.strInterpTrtDir.c_str());
	CheckDlgButton(IDC_CHECK21, m_SetsPP.bInterpFP16 ? BST_CHECKED : BST_UNCHECKED);
	SendDlgItemMessageW(IDC_SLIDER3, TBM_SETPOS, 1, m_SetsPP.iInterpSceneThreshold);
	SetDlgItemTextW(IDC_STATIC9, std::format(L"{}", m_SetsPP.iInterpSceneThreshold).c_str());

	EnableControls();
}

void CVRInterpPPage::UpdateProfileList()
{
	const HWND list = GetDlgItem(IDC_LIST1);
	ListView_DeleteAllItems(list);
	for (size_t i = 0; i < m_SetsPP.interpProfiles.size(); ++i) {
		const auto& profile = m_SetsPP.interpProfiles[i];
		const std::wstring resolution = std::format(L"{} x {}", profile.width, profile.height);
		LVITEMW item = {};
		item.mask = LVIF_TEXT | LVIF_PARAM;
		item.iItem = static_cast<int>(i);
		item.pszText = const_cast<LPWSTR>(resolution.c_str());
		item.lParam = static_cast<LPARAM>(i);
		const int row = ListView_InsertItem(list, &item);
		ListView_SetItemText(list, row, 1, const_cast<LPWSTR>(InterpProfileOutputName(profile.output)));
		const size_t separator = profile.model.find_last_of(L"\\/");
		const wchar_t* model = profile.model.empty() ? L"(default)"
			: profile.model.c_str() + (separator == std::wstring::npos ? 0 : separator + 1);
		ListView_SetItemText(list, row, 2, const_cast<LPWSTR>(model));
	}
	FitInterpProfileColumns(list);
	EnableControls();
}

void CVRInterpPPage::PopulateGpuAdapters()
{
	ComboBox_AddStringData(m_hWnd, IDC_COMBO13, L"Automatic (GPU driving the display)", 0);
	bool selectedAvailable = m_SetsPP.iGpuAdapter == GPU_ADAPTER_AUTO;

	CComPtr<IDXGIFactory1> factory;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
		CComPtr<IDXGIAdapter1> adapter;
		for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
			DXGI_ADAPTER_DESC1 desc = {};
			if (SUCCEEDED(adapter->GetDesc1(&desc))
					&& desc.VendorId == PCIV_NVIDIA
					&& !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
				ComboBox_AddStringData(m_hWnd, IDC_COMBO13,
					std::format(L"DXGI {}: {}", index, desc.Description).c_str(), static_cast<LONG_PTR>(index) + 1);
				selectedAvailable = selectedAvailable || m_SetsPP.iGpuAdapter == static_cast<int>(index);
			}
			adapter.Release();
		}
	}

	if (!selectedAvailable) {
		ComboBox_AddStringData(m_hWnd, IDC_COMBO13,
			std::format(L"DXGI {}: unavailable", m_SetsPP.iGpuAdapter).c_str(),
			static_cast<LONG_PTR>(m_SetsPP.iGpuAdapter) + 1);
	}
}

void CVRInterpPPage::EnableControls()
{
	const BOOL bEnable = m_SetsPP.bInterp;
	for (const int id : { IDC_LIST1, IDC_BUTTON4, IDC_EDIT3, IDC_BUTTON2, IDC_EDIT4, IDC_BUTTON3, IDC_CHECK21, IDC_SLIDER3, IDC_STATIC9 }) {
		GetDlgItem(id).EnableWindow(bEnable);
	}
	GetDlgItem(IDC_COMBO11).EnableWindow(bEnable && m_SetsPP.interpProfiles.empty());
	const int selected = ListView_GetNextItem(GetDlgItem(IDC_LIST1), -1, LVNI_SELECTED);
	GetDlgItem(IDC_BUTTON7).EnableWindow(bEnable && selected >= 0);
	GetDlgItem(IDC_BUTTON5).EnableWindow(bEnable && selected >= 0);
}

void CVRInterpPPage::UpdateStatus()
{
	std::wstring str;
	if (m_pVideoRenderer) {
		m_pVideoRenderer->GetInterpolationStatus(str);
	}
	str_replace(str, L"\n", L"\r\n");
	if (str != m_strStatus) {
		m_strStatus = str;
		SetDlgItemTextW(IDC_EDIT5, str.c_str());
	}
}

void CVRInterpPPage::BrowseModel()
{
	std::wstring path;
	if (PickPath(m_hWnd, false, path)) {
		SetDlgItemTextW(IDC_EDIT3, path.c_str()); // EN_CHANGE updates the settings
	}
}

void CVRInterpPPage::BrowseTrtDir()
{
	std::wstring path;
	if (PickPath(m_hWnd, true, path)) {
		SetDlgItemTextW(IDC_EDIT4, path.c_str());
	}
}

void CVRInterpPPage::AddProfile()
{
	InterpProfileDialogData data;
	if (DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_INTERPPROFILE_DIALOG), m_hWnd,
			InterpProfileDialogProc, reinterpret_cast<LPARAM>(&data)) != IDOK) {
		return;
	}
	const bool duplicate = std::ranges::any_of(m_SetsPP.interpProfiles, [&data](const auto& profile) {
		return profile.width == data.profile.width && profile.height == data.profile.height;
	});
	if (duplicate) {
		MessageBoxW(std::format(L"An override for {}x{} already exists.", data.profile.width, data.profile.height).c_str(),
			L"Duplicate interpolation profile", MB_OK | MB_ICONWARNING);
		return;
	}
	m_SetsPP.interpProfiles.emplace_back(std::move(data.profile));
	UpdateProfileList();
	SetDirty();
}

void CVRInterpPPage::EditProfile()
{
	const HWND list = GetDlgItem(IDC_LIST1);
	const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
	if (selected < 0 || static_cast<size_t>(selected) >= m_SetsPP.interpProfiles.size()) {
		return;
	}
	InterpProfileDialogData data = { m_SetsPP.interpProfiles[selected], true };
	if (DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_INTERPPROFILE_DIALOG), m_hWnd,
			InterpProfileDialogProc, reinterpret_cast<LPARAM>(&data)) != IDOK) {
		return;
	}
	bool duplicate = false;
	for (size_t i = 0; i < m_SetsPP.interpProfiles.size(); ++i) {
		const auto& profile = m_SetsPP.interpProfiles[i];
		if (i != static_cast<size_t>(selected)
				&& profile.width == data.profile.width && profile.height == data.profile.height) {
			duplicate = true;
			break;
		}
	}
	if (duplicate) {
		MessageBoxW(std::format(L"An override for {}x{} already exists.", data.profile.width, data.profile.height).c_str(),
			L"Duplicate interpolation profile", MB_OK | MB_ICONWARNING);
		return;
	}
	m_SetsPP.interpProfiles[selected] = std::move(data.profile);
	UpdateProfileList();
	ListView_SetItemState(list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	SetDirty();
}

void CVRInterpPPage::DeleteProfile()
{
	const HWND list = GetDlgItem(IDC_LIST1);
	const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
	if (selected < 0 || static_cast<size_t>(selected) >= m_SetsPP.interpProfiles.size()) {
		return;
	}
	m_SetsPP.interpProfiles.erase(m_SetsPP.interpProfiles.begin() + selected);
	UpdateProfileList();
	SetDirty();
}

INT_PTR CVRInterpPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_COMMAND) {
		const int nID = LOWORD(wParam);
		const int action = HIWORD(wParam);

		if (action == BN_CLICKED) {
			if (nID == IDC_CHECK20) {
				m_SetsPP.bInterp = IsDlgButtonChecked(IDC_CHECK20) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK21) {
				m_SetsPP.bInterpFP16 = IsDlgButtonChecked(IDC_CHECK21) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_BUTTON2) {
				BrowseModel();
				return (LRESULT)1;
			}
			if (nID == IDC_BUTTON3) {
				BrowseTrtDir();
				return (LRESULT)1;
			}
			if (nID == IDC_BUTTON4) {
				AddProfile();
				return (LRESULT)1;
			}
			if (nID == IDC_BUTTON5) {
				DeleteProfile();
				return (LRESULT)1;
			}
			if (nID == IDC_BUTTON7) {
				EditProfile();
				return (LRESULT)1;
			}
		}
		else if (action == CBN_SELCHANGE) {
			if (nID == IDC_COMBO13) {
				const LRESULT selection = SendDlgItemMessageW(IDC_COMBO13, CB_GETCURSEL, 0, 0);
				if (selection != CB_ERR) {
					const int adapter = static_cast<int>(SendDlgItemMessageW(IDC_COMBO13, CB_GETITEMDATA, selection, 0)) - 1;
					if (adapter != m_SetsPP.iGpuAdapter) {
						m_SetsPP.iGpuAdapter = adapter;
						SetDirty();
					}
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO11) {
				const LRESULT selection = SendDlgItemMessageW(IDC_COMBO11, CB_GETCURSEL, 0, 0);
				if (selection != CB_ERR) {
					const int value = static_cast<int>(SendDlgItemMessageW(IDC_COMBO11, CB_GETITEMDATA, selection, 0));
					if (IsValidInterpProfileValue(value) && value != m_SetsPP.iInterpDefaultOutput) {
						m_SetsPP.iInterpDefaultOutput = value;
						SetDirty();
					}
				}
				return (LRESULT)1;
			}
		}
		else if (action == EN_CHANGE && m_bActivated) {
			if (nID == IDC_EDIT3) {
				m_SetsPP.strInterpModel = GetDlgItemString(*this, IDC_EDIT3);
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_EDIT4) {
				m_SetsPP.strInterpTrtDir = GetDlgItemString(*this, IDC_EDIT4);
				SetDirty();
				return (LRESULT)1;
			}
		}
	}
	else if (uMsg == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_LIST1
			&& reinterpret_cast<NMHDR*>(lParam)->code == LVN_ITEMCHANGED) {
		EnableControls();
		return (LRESULT)1;
	}
	else if (uMsg == WM_HSCROLL) {
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER3)) {
			const LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER3, TBM_GETPOS, 0, 0);
			if (lValue != m_SetsPP.iInterpSceneThreshold) {
				m_SetsPP.iInterpSceneThreshold = (int)lValue;
				SetDlgItemTextW(IDC_STATIC9, std::format(L"{}", lValue).c_str());
				SetDirty();
			}
			return (LRESULT)1;
		}
	}
	else if (uMsg == WM_TIMER && wParam == INTERP_STATUS_TIMER) {
		UpdateStatus();
		return (LRESULT)1;
	}

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRInterpPPage::OnApplyChanges()
{
	m_pVideoRenderer->SetSettings(m_SetsPP);
	m_pVideoRenderer->SaveSettings();
	UpdateStatus();

	return S_OK;
}

#endif // _WIN64
