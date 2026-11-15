// Pegaso: country flags, see PegasoGeo.h
#include "stdafx.h"
#include <algorithm>
#include <vector>
#include "emule.h"
#include "PegasoGeo.h"
#include "Preferences.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const int FLAG_W = 16, FLAG_H = 11;

	bool				s_bTried;
	std::vector<uint32>	s_starts;		// first address of each range, ascending, host order
	std::vector<uint8>	s_owners;		// country index of each range
	std::vector<CString> s_codes;		// by country index; 0 = unknown
	std::vector<CString> s_names;		// by country index, filled when asked
	HIMAGELIST			s_hFlags;

	void Load()
	{
		if (s_bTried)
			return;
		s_bTried = true;
		const CString sDir(thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR) + _T("geo\\"));
		FILE *fp = _tfsopen(sDir + _T("ip-country.dat"), _T("rb"), _SH_DENYWR);
		if (fp == NULL)
			return;
		uint32 header[4] = {};
		bool bOK = fread(header, sizeof header, 1, fp) == 1 && memcmp(header, "PGEO", 4) == 0 && header[1] == 1
			&& header[2] > 0 && header[2] < 4000000 && header[3] > 0 && header[3] <= 255;
		if (bOK) {
			std::vector<char> codes(header[3] * 2);
			s_starts.resize(header[2]);
			s_owners.resize(header[2]);
			bOK = fread(codes.data(), 1, codes.size(), fp) == codes.size()
				&& fread(s_starts.data(), sizeof(uint32), s_starts.size(), fp) == s_starts.size()
				&& fread(s_owners.data(), 1, s_owners.size(), fp) == s_owners.size()
				&& std::is_sorted(s_starts.begin(), s_starts.end());
			for (size_t i = 0; bOK && i < s_owners.size(); ++i)
				bOK = s_owners[i] < header[3];
			if (bOK)
				for (uint32 i = 0; i < header[3]; ++i)
					s_codes.push_back(CString(CStringA(&codes[i * 2], 2)));
		}
		fclose(fp);
		if (!bOK) {
			s_starts.clear();
			s_owners.clear();
			s_codes.clear();
			DebugLogWarning(_T("geo\\ip-country.dat is damaged: no country flags"));
			return;
		}
		s_names.resize(s_codes.size());

		const HBITMAP hStrip = (HBITMAP)::LoadImage(NULL, sDir + _T("flags.bmp"), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
		if (hStrip != NULL) {
			s_hFlags = ::ImageList_Create(FLAG_W, FLAG_H, ILC_COLOR24 | ILC_MASK, (int)s_codes.size(), 0);
			if (s_hFlags != NULL && ::ImageList_AddMasked(s_hFlags, hStrip, RGB(255, 0, 255)) < 0) {
				::ImageList_Destroy(s_hFlags);
				s_hFlags = NULL;
			}
			::DeleteObject(hStrip);
		}
		DebugLog(_T("Country data loaded: %u address ranges, %u countries, flags: %s"), (unsigned)s_starts.size(), (unsigned)s_codes.size() - 1
			, s_hFlags != NULL ? _T("yes") : _T("no"));
	}

	// uIP as eMule keeps it (network byte order). 0 = not known.
	int CountryIndex(uint32 uIP)
	{
		Load();
		if (s_starts.empty() || uIP == 0)
			return 0;
		const uint32 uHost = ntohl(uIP);
		const auto it = std::upper_bound(s_starts.begin(), s_starts.end(), uHost);
		if (it == s_starts.begin())
			return 0;
		return s_owners[(size_t)(it - s_starts.begin()) - 1];
	}
}

void PegasoDrawFlag(CDC *pDC, RECT &rRect, uint32 uIP, int iGapBefore)
{
	const int iIndex = CountryIndex(uIP);
	if (s_hFlags == NULL)
		return;
	rRect.left += iGapBefore;
	if (iIndex > 0 && rRect.left + FLAG_W <= rRect.right)
		::ImageList_Draw(s_hFlags, iIndex, pDC->GetSafeHdc(), rRect.left, rRect.top + max(0, (rRect.bottom - rRect.top - FLAG_H) / 2), ILD_TRANSPARENT);
	rRect.left += FLAG_W + 4;
}

CString PegasoCountryOf(uint32 uIP)
{
	const int iIndex = CountryIndex(uIP);
	if (iIndex <= 0)
		return CString();
	CString &rsName = s_names[(size_t)iIndex];
	if (rsName.IsEmpty()) {
		// the name in the language of the user, as Windows knows it (Windows 10 1709 and later)
		typedef int (WINAPI *GETGEOINFOEX)(PWSTR, GEOTYPE, PWSTR, int);
		static const GETGEOINFOEX pfnGetGeoInfoEx = (GETGEOINFOEX)::GetProcAddress(::GetModuleHandle(_T("kernel32.dll")), "GetGeoInfoEx");
		WCHAR szName[128] = {};
		CStringW sCode(s_codes[(size_t)iIndex]);
		if (pfnGetGeoInfoEx != NULL && pfnGetGeoInfoEx(sCode.GetBuffer(), GEO_FRIENDLYNAME, szName, _countof(szName)) > 0 && szName[0] != L'\0')
			rsName.Format(_T("%s (%s)"), (LPCTSTR)CString(szName), (LPCTSTR)s_codes[(size_t)iIndex]);
		else
			rsName = s_codes[(size_t)iIndex];
	}
	return rsName;
}
