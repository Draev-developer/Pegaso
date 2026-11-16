// Pegaso: what protects the user of this computer.
//
//  - Secrets of preferences.ini (the token of the local API, the keys of the video databases
//    and of the search sites, the password of the proxy) are kept encrypted by Windows for
//    the user account (DPAPI): "dpapi:<base64>". A value in clear is still read, and written
//    encrypted at the next start. Copied to another account or computer they cannot be read
//    and count as empty.
//  - "Check my connection": by which way each protocol leaves this computer right now.
//  - "Clear local traces": the search history, the finished and cancelled downloads, the
//    memory of files which are no longer shared, the logs.
//    preferences.ini, [eMule]: ClearTracesOnExit (0).
//  - The check for a new version, off unless the user turns it on (Preferences - General):
//    one request to the releases of the project, by the same way as every other web request.
//    preferences.ini, [eMule]: PegasoUpdateCheck (0), UpdateCheckUrl (for tests).
#pragma once
#include "resource.h"

CString PegasoProtect(const CString &sSecret);
CString PegasoUnprotect(const CString &sStored);
void	PegasoMigrateSecrets();		// at startup: whatever is still in clear is written encrypted

CString PegasoConnectionReport();	// lines ending in \r\n; main thread

void	PegasoClearTraces();
void	PegasoClearTracesAsk(CWnd *pParent);	// says what goes, asks, clears, says so
bool	PegasoClearTracesOnExit();
void	PegasoSetClearTracesOnExit(bool bOn);

void	PegasoUpdateCheck();		// main thread; does nothing until the days of the preferences have passed

class CPegasoConnDlg : public CDialog
{
	DECLARE_DYNAMIC(CPegasoConnDlg)

	enum
	{
		IDD = IDD_PEGASO_CONN
	};

public:
	explicit CPegasoConnDlg(CWnd *pParent = NULL);

protected:
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedRefresh();
	afx_msg void OnBnClickedCopy();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
};
