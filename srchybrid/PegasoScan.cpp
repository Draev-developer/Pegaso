// Pegaso: antivirus check of finished downloads. See PegasoScan.h
#include "stdafx.h"
#include <amsi.h>
#include <mutex>
#include <thread>
#include <vector>
#include "emule.h"
#include "emuleDlg.h"
#include "PegasoScan.h"
#include "PegasoStrings.h"
#include "Preferences.h"
#include "Log.h"

#pragma comment(lib, "amsi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	struct SScanResult
	{
		CString	path;
		bool	scanned;
		bool	malware;
		HRESULT	hr;
	};

	std::mutex s_lock;
	std::vector<SScanResult> s_results;

	// A file truncated or locked under the view raises an exception instead of failing a call.
	// (On its own: structured exception handling does not mix with C++ objects.)
	HRESULT SafeScan(HAMSICONTEXT hContext, PVOID pData, ULONG uSize, LPCWSTR pszName, AMSI_RESULT *pResult)
	{
		__try {
			return ::AmsiScanBuffer(hContext, pData, uSize, pszName, NULL, pResult);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return E_ABORT;
		}
	}

	void ScanWorker(CString sPath)
	{
		SScanResult res = {sPath, false, false, S_OK};
		const HRESULT hrCom = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
		HAMSICONTEXT hContext = NULL;
		res.hr = ::AmsiInitialize(L"Pegaso", &hContext);
		if (SUCCEEDED(res.hr)) {
			HANDLE hFile = ::CreateFile(sPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
			if (hFile != INVALID_HANDLE_VALUE) {
				LARGE_INTEGER size;
				if (::GetFileSizeEx(hFile, &size) && size.QuadPart > 0 && size.QuadPart <= 512ll * 1024 * 1024) {
					HANDLE hMap = ::CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
					if (hMap != NULL) {
						LPVOID pView = ::MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
						if (pView != NULL) {
							AMSI_RESULT result = AMSI_RESULT_NOT_DETECTED;
							res.hr = SafeScan(hContext, pView, (ULONG)size.QuadPart, sPath, &result);
							if (SUCCEEDED(res.hr)) {
								res.scanned = true;
								res.malware = AmsiResultIsMalware(result);
							}
							::UnmapViewOfFile(pView);
						} else
							res.hr = HRESULT_FROM_WIN32(::GetLastError());
						::CloseHandle(hMap);
					} else
						res.hr = HRESULT_FROM_WIN32(::GetLastError());
				}
				::CloseHandle(hFile);
			} else
				res.hr = HRESULT_FROM_WIN32(::GetLastError());
			::AmsiUninitialize(hContext);
		}
		if (SUCCEEDED(hrCom))
			::CoUninitialize();
		std::lock_guard<std::mutex> guard(s_lock);
		s_results.push_back(res);
	}
}

void PegasoScanFile(const CString &sPath)
{
	if (!sPath.IsEmpty())
		std::thread(ScanWorker, sPath).detach();
}

void PegasoScanProcess()
{
	std::vector<SScanResult> results;
	{
		std::lock_guard<std::mutex> guard(s_lock);
		results.swap(s_results);
	}
	for (const SScanResult &res : results) {
		if (res.malware) {
			CString sText;
			sText.Format(GetPegasoString(PS_AV_DETECTED), (LPCTSTR)res.path);
			LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)sText);
			if (theApp.emuledlg != NULL)
				theApp.emuledlg->ShowNotifier(sText, TBN_IMPORTANTEVENT);
		} else if (res.scanned)
			AddLogLine(false, _T("Antivirus check passed: %s"), (LPCTSTR)res.path);
		else if (FAILED(res.hr))
			AddDebugLogLine(false, _T("Antivirus check not possible (0x%08X): %s"), (unsigned)res.hr, (LPCTSTR)res.path);
	}
}
