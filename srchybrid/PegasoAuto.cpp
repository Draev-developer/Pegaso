// Pegaso: watched folder, folders by type, import from other programs. See PegasoAuto.h.
#include "stdafx.h"
#include <shlobj.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "emule.h"
#include "emuleDlg.h"
#include "PegasoAuto.h"
#include "PegasoStrings.h"
#include "Preferences.h"
#include "Opcodes.h"
#include "OtherFunctions.h"
#include "TorrentQueue.h"
#include "WebDownload.h"
#include "DownloadQueue.h"
#include "ED2KLink.h"
#include "StringConversion.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const LPCTSTR s_apszTypeKey[PegasoAuto::TYPE_COUNT] = {_T("TypeFolderVideo"), _T("TypeFolderAudio"), _T("TypeFolderImage")
		, _T("TypeFolderDoc"), _T("TypeFolderArchive"), _T("TypeFolderProgram")};
	const UINT s_auTypeLabel[PegasoAuto::TYPE_COUNT] = {IDS_SEARCH_VIDEO, IDS_SEARCH_AUDIO, IDS_SEARCH_PICS, IDS_SEARCH_DOC, IDS_SEARCH_ARC, IDS_SEARCH_PRG};
	const LPCTSTR ADDED_FOLDER = _T("added");
	const DWORD SCAN_MS = 5000;
	const ULONGLONG MAX_LIST_FILE = 1024 * 1024;

	CString Setting(LPCTSTR pszKey)
	{
		CString s(OptUtf8ToStr(URLDecode(theApp.GetProfileString(_T("eMule"), pszKey, _T("")))));
		return s.Trim();
	}

	void SetSetting(LPCTSTR pszKey, CString sValue)
	{
		theApp.WriteProfileString(_T("eMule"), pszKey, EncodeUrlUtf8(sValue.Trim()));
	}

	CString WithSlash(CString sFolder)
	{
		sFolder.Trim();
		if (!sFolder.IsEmpty() && sFolder.Right(1) != _T("\\") && sFolder.Right(1) != _T("/"))
			sFolder += _T('\\');
		return sFolder;
	}

	int TypeOf(LPCTSTR pszFileName)
	{
		const CString sType(GetFileTypeByName(pszFileName));
		if (sType == _T(ED2KFTSTR_VIDEO))
			return 0;
		if (sType == _T(ED2KFTSTR_AUDIO))
			return 1;
		if (sType == _T(ED2KFTSTR_IMAGE))
			return 2;
		if (sType == _T(ED2KFTSTR_DOCUMENT))
			return 3;
		if (sType == _T(ED2KFTSTR_ARCHIVE) || sType == _T(ED2KFTSTR_CDIMAGE))
			return 4;
		if (sType == _T(ED2KFTSTR_PROGRAM))
			return 5;
		return -1;
	}

	// A link of any kind Pegaso takes. false: it is none.
	bool AddAny(CString sLink)
	{
		sLink.Trim();
		if (sLink.IsEmpty())
			return false;
		if (theTorrents.AddLink(sLink))
			return true;
		if (CWebDownloadQueue::IsWebUrl(sLink)) {
			theWebDownloads.AddUrl(sLink, 0);
			return true;
		}
		if (_tcsnicmp(sLink, _T("ed2k://"), 7) != 0)
			return false;
		bool bOk = false;
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(sLink.Right(1) == _T("/") ? sLink : sLink + _T('/'));
			if (pLink != NULL && pLink->GetKind() == CED2KLink::kFile) {
				theApp.downloadqueue->AddFileLinkToDownload(*pLink->GetFileLink(), 0);
				bOk = true;
			}
		} catch (const CString&) {
		}
		delete pLink;
		return bOk;
	}

	// The lines of a text file: UTF-8 if it says so or reads as such, else the code page of Windows.
	bool ReadLines(const CString &sPath, CStringArray &asLines)
	{
		CFile file;
		if (!file.Open(sPath, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary))
			return false;
		const ULONGLONG uSize = file.GetLength();
		if (uSize > MAX_LIST_FILE)
			return false;
		CStringA sRaw;
		const UINT uRead = file.Read(sRaw.GetBuffer((int)uSize), (UINT)uSize);
		sRaw.ReleaseBuffer((int)uRead);
		file.Close();
		if (sRaw.Left(3) == "\xEF\xBB\xBF")
			sRaw.Delete(0, 3);
		const CString sText(OptUtf8ToStr(sRaw));
		for (int iPos = 0; iPos >= 0;) {
			CString sLine(sText.Tokenize(_T("\r\n"), iPos));
			if (iPos < 0)
				break;
			if (!sLine.Trim().IsEmpty())
				asLines.Add(sLine);
		}
		return true;
	}

	struct SSeen
	{
		ULONGLONG	size;
		FILETIME	written;
	};

	// One file of the watched folder which has stopped changing. Gives back how many downloads it brought.
	int Take(const CString &sPath, const CString &sExt)
	{
		if (sExt == _T(".torrent")) {
			theTorrents.AddLink(sPath);		// says in the log what it could not take
			return 1;
		}
		CStringArray asLines;
		if (!ReadLines(sPath, asLines))
			return 0;
		int iAdded = 0;
		for (INT_PTR i = 0; i < asLines.GetCount(); ++i)
			iAdded += static_cast<int>(AddAny(asLines[i]));
		return iAdded;
	}

	CString LocalAppData()
	{
		TCHAR szPath[MAX_PATH] = {};
		if (FAILED(::SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, szPath)))
			return CString();
		return WithSlash(szPath);
	}
}

CString PegasoAuto::TypeLabel(int iType)
{
	CString sLabel(GetResString(s_auTypeLabel[iType]));
	sLabel.Remove(_T('&'));
	return sLabel;
}

CString PegasoAuto::TypeFolderSetting(int iType)
{
	return Setting(s_apszTypeKey[iType]);
}

void PegasoAuto::SetTypeFolderSetting(int iType, const CString &sFolder)
{
	SetSetting(s_apszTypeKey[iType], sFolder);
}

CString PegasoAuto::WatchFolder()
{
	return Setting(_T("WatchFolder"));
}

void PegasoAuto::SetWatchFolder(const CString &sFolder)
{
	SetSetting(_T("WatchFolder"), sFolder);
}

CString PegasoAuto::TypeFolder(LPCTSTR pszFileName)
{
	const int iType = TypeOf(pszFileName);
	if (iType < 0)
		return CString();
	const CString sFolder(WithSlash(Setting(s_apszTypeKey[iType])));
	if (sFolder.IsEmpty())
		return CString();
	const int iMade = ::SHCreateDirectoryEx(NULL, sFolder.Left(sFolder.GetLength() - 1), NULL);
	if (iMade != ERROR_SUCCESS && iMade != ERROR_ALREADY_EXISTS && iMade != ERROR_FILE_EXISTS)
		return CString();	// it cannot be made: the file goes where it always went
	return sFolder;
}

void PegasoAuto::Process()
{
	static DWORD s_dwLast;
	static std::map<CString, SSeen> s_seen;		// files waiting to stop changing
	static std::set<CString> s_left;			// files which could not be moved away: not taken twice
	const DWORD dwNow = ::GetTickCount();
	if (dwNow - s_dwLast < SCAN_MS || !theApp.IsRunning())
		return;
	s_dwLast = dwNow;
	const CString sFolder(WithSlash(WatchFolder()));
	if (sFolder.IsEmpty()) {
		s_seen.clear();
		return;
	}

	std::map<CString, SSeen> now;
	CFileFind finder;
	for (BOOL bFound = finder.FindFile(sFolder + _T("*.*")); bFound;) {
		bFound = finder.FindNextFile();
		if (finder.IsDirectory() || finder.IsDots())
			continue;
		const CString sPath(finder.GetFilePath());
		CString sExt(::PathFindExtension(sPath));
		sExt.MakeLower();
		if ((sExt != _T(".torrent") && sExt != _T(".txt")) || s_left.count(sPath) != 0)
			continue;
		SSeen seen;
		seen.size = finder.GetLength();
		finder.GetLastWriteTime(&seen.written);
		const std::map<CString, SSeen>::const_iterator it = s_seen.find(sPath);
		if (it == s_seen.end() || it->second.size != seen.size || ::CompareFileTime(&it->second.written, &seen.written) != 0) {
			now[sPath] = seen;	// new, or still being written: looked at again in a moment
			continue;
		}
		const int iAdded = Take(sPath, sExt);
		AddLogLine(true, _T("Watched folder: %d download(s) from %s"), iAdded, (LPCTSTR)finder.GetFileName());
		::CreateDirectory(sFolder + ADDED_FOLDER, NULL);
		CString sTarget(sFolder + ADDED_FOLDER + _T('\\') + finder.GetFileName());
		for (int i = 2; ::PathFileExists(sTarget) && i < 1000; ++i)
			sTarget.Format(_T("%s%s\\%s (%d)%s"), (LPCTSTR)sFolder, ADDED_FOLDER, (LPCTSTR)finder.GetFileTitle(), i, (LPCTSTR)sExt);
		if (!::MoveFileEx(sPath, sTarget, 0)) {
			s_left.insert(sPath);
			LogWarning(LOG_STATUSBAR, _T("Watched folder: %s could not be moved to \"%s\"; it is not looked at again"), (LPCTSTR)finder.GetFileName(), ADDED_FOLDER);
		}
	}
	s_seen.swap(now);
}

// ---------------------------------------------------------------- import

CString PegasoAuto::GuessEmuleTemp()
{
	const CString sLocal(LocalAppData());
	if (sLocal.IsEmpty())
		return CString();
	TCHAR szTemp[MAX_PATH] = {};
	::GetPrivateProfileString(_T("eMule"), _T("TempDir"), _T(""), szTemp, _countof(szTemp), sLocal + _T("eMule\\config\\preferences.ini"));
	if (szTemp[0] != _T('\0') && ::PathIsDirectory(szTemp))
		return szTemp;
	const CString sDefault(sLocal + _T("eMule\\Temp"));
	return ::PathIsDirectory(sDefault) ? sDefault : CString();
}

CString PegasoAuto::GuessQBittorrent()
{
	const CString sFolder(LocalAppData() + _T("qBittorrent\\BT_backup"));
	return ::PathIsDirectory(sFolder) ? sFolder : CString();
}

int PegasoAuto::ImportEmule(const CString &sFolderIn, CString &sReport)
{
	const CString sFolder(WithSlash(sFolderIn));
	if (sFolder.IsEmpty() || !::PathIsDirectory(sFolder)) {
		sReport = GetPegasoString(PS_IM_NOFOLDER);
		return 0;
	}
	int iKnown = 0;
	int iFailed = 0;
	const int iAdded = theApp.downloadqueue->ImportTempDir(sFolder, iKnown, iFailed);
	sReport.Format(GetPegasoString(PS_IM_RESULT), iAdded, iKnown, iFailed);
	AddLogLine(true, _T("Import from eMule, %s: %d added, %d already in the list, %d could not be read"), (LPCTSTR)sFolder, iAdded, iKnown, iFailed);
	return iAdded;
}

int PegasoAuto::ImportQBittorrent(const CString &sFolderIn, CString &sReport)
{
	const CString sFolder(WithSlash(sFolderIn));
	if (sFolder.IsEmpty() || !::PathIsDirectory(sFolder)) {
		sReport = GetPegasoString(PS_IM_NOFOLDER);
		return 0;
	}
	if (!theTorrents.IsRunning()) {
		sReport = GetPegasoString(PS_UI_BT_OFF);
		return 0;
	}
	int iAdded = 0;
	int iKnown = 0;
	int iFailed = 0;
	CFileFind finder;
	for (BOOL bFound = finder.FindFile(sFolder + _T("*.torrent")); bFound;) {
		bFound = finder.FindNextFile();
		if (finder.IsDirectory())
			continue;
		const CString sTorrent(finder.GetFilePath());
		CString sResume(sTorrent.Left(sTorrent.GetLength() - 8) + _T(".fastresume"));
		if (!::PathFileExists(sResume))
			sResume.Empty();
		std::wstring error;
		if (theTorrents.Engine().AddFromResume(std::wstring((LPCTSTR)sResume), std::wstring((LPCTSTR)sTorrent), error))
			++iAdded;
		else if (error.find(L"already") != std::wstring::npos)
			++iKnown;
		else {
			++iFailed;
			LogWarning(LOG_DEFAULT, _T("Import from qBittorrent: %s - %s"), (LPCTSTR)finder.GetFileName(), error.c_str());
		}
	}
	sReport.Format(GetPegasoString(PS_IM_RESULT), iAdded, iKnown, iFailed);
	AddLogLine(true, _T("Import from qBittorrent, %s: %d added, %d already in the list, %d could not be read"), (LPCTSTR)sFolder, iAdded, iKnown, iFailed);
	return iAdded;
}

// ---------------------------------------------------------------- CPegasoFoldersDlg

IMPLEMENT_DYNAMIC(CPegasoFoldersDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoFoldersDlg, CDialog)
	ON_BN_CLICKED(IDC_AF_WATCH_BROWSE, OnBnClickedBrowseWatch)
	ON_COMMAND_RANGE(IDC_AF_TYPE_FIRST, IDC_AF_TYPE_FIRST + 3 * PegasoAuto::TYPE_COUNT - 1, OnBnClickedBrowseType)
END_MESSAGE_MAP()

CPegasoFoldersDlg::CPegasoFoldersDlg(CWnd *pParent)
	: CDialog(CPegasoFoldersDlg::IDD, pParent)
{
}

BOOL CPegasoFoldersDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	SetWindowText(GetPegasoString(PS_AF_MENU));
	SetDlgItemText(IDC_AF_WATCH_LBL, GetPegasoString(PS_AF_WATCH));
	SetDlgItemText(IDC_AF_WATCH_HINT, GetPegasoString(PS_AF_WATCH_HINT));
	SetDlgItemText(IDC_AF_TYPE_LBL, GetPegasoString(PS_AF_TYPE));
	SetDlgItemText(IDC_AF_TYPE_HINT, GetPegasoString(PS_AF_TYPE_HINT));
	SetDlgItemText(IDOK, GetResString(IDS_TREEOPTIONS_OK));
	SetDlgItemText(IDCANCEL, GetResString(IDS_CANCEL));
	SetDlgItemText(IDC_AF_WATCH, PegasoAuto::WatchFolder());
	for (int i = 0; i < PegasoAuto::TYPE_COUNT; ++i) {
		SetDlgItemText(IDC_AF_TYPE_FIRST + 3 * i, PegasoAuto::TypeLabel(i));
		SetDlgItemText(IDC_AF_TYPE_FIRST + 3 * i + 1, PegasoAuto::TypeFolderSetting(i));
	}
	return TRUE;
}

void CPegasoFoldersDlg::OnBnClickedBrowseWatch()
{
	TCHAR szPath[MAX_PATH] = {};
	GetDlgItemText(IDC_AF_WATCH, szPath, _countof(szPath));
	if (SelectDir(GetSafeHwnd(), szPath, GetPegasoString(PS_AF_WATCH)))
		SetDlgItemText(IDC_AF_WATCH, szPath);
}

void CPegasoFoldersDlg::OnBnClickedBrowseType(UINT uId)
{
	const int iOffset = (int)(uId - IDC_AF_TYPE_FIRST);
	if (iOffset % 3 != 2)
		return;		// the label or the field, not the button
	const UINT uField = uId - 1;
	TCHAR szPath[MAX_PATH] = {};
	GetDlgItemText(uField, szPath, _countof(szPath));
	if (SelectDir(GetSafeHwnd(), szPath, PegasoAuto::TypeLabel(iOffset / 3)))
		SetDlgItemText(uField, szPath);
}

void CPegasoFoldersDlg::OnOK()
{
	CString sFolder;
	GetDlgItemText(IDC_AF_WATCH, sFolder);
	PegasoAuto::SetWatchFolder(sFolder);
	for (int i = 0; i < PegasoAuto::TYPE_COUNT; ++i) {
		GetDlgItemText(IDC_AF_TYPE_FIRST + 3 * i + 1, sFolder);
		PegasoAuto::SetTypeFolderSetting(i, sFolder);
	}
	CDialog::OnOK();
}

// ---------------------------------------------------------------- CPegasoImportDlg

IMPLEMENT_DYNAMIC(CPegasoImportDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoImportDlg, CDialog)
	ON_BN_CLICKED(IDC_IM_EMULE_BROWSE, OnBnClickedBrowseEmule)
	ON_BN_CLICKED(IDC_IM_QB_BROWSE, OnBnClickedBrowseQb)
	ON_BN_CLICKED(IDC_IM_EMULE_GO, OnBnClickedEmule)
	ON_BN_CLICKED(IDC_IM_QB_GO, OnBnClickedQb)
END_MESSAGE_MAP()

CPegasoImportDlg::CPegasoImportDlg(CWnd *pParent)
	: CDialog(CPegasoImportDlg::IDD, pParent)
{
}

BOOL CPegasoImportDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	SetWindowText(GetPegasoString(PS_IM_MENU));
	SetDlgItemText(IDC_IM_EMULE_LBL, GetPegasoString(PS_IM_EMULE));
	SetDlgItemText(IDC_IM_EMULE_HINT, GetPegasoString(PS_IM_EMULE_HINT));
	SetDlgItemText(IDC_IM_QB_LBL, GetPegasoString(PS_IM_QB));
	SetDlgItemText(IDC_IM_QB_HINT, GetPegasoString(PS_IM_QB_HINT));
	SetDlgItemText(IDC_IM_EMULE_GO, GetPegasoString(PS_IM_GO));
	SetDlgItemText(IDC_IM_QB_GO, GetPegasoString(PS_IM_GO));
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));
	SetDlgItemText(IDC_IM_EMULE, PegasoAuto::GuessEmuleTemp());
	SetDlgItemText(IDC_IM_QB, PegasoAuto::GuessQBittorrent());
	return TRUE;
}

void CPegasoImportDlg::OnBnClickedBrowseEmule()
{
	TCHAR szPath[MAX_PATH] = {};
	GetDlgItemText(IDC_IM_EMULE, szPath, _countof(szPath));
	if (SelectDir(GetSafeHwnd(), szPath, GetPegasoString(PS_IM_EMULE)))
		SetDlgItemText(IDC_IM_EMULE, szPath);
}

void CPegasoImportDlg::OnBnClickedBrowseQb()
{
	TCHAR szPath[MAX_PATH] = {};
	GetDlgItemText(IDC_IM_QB, szPath, _countof(szPath));
	if (SelectDir(GetSafeHwnd(), szPath, GetPegasoString(PS_IM_QB)))
		SetDlgItemText(IDC_IM_QB, szPath);
}

void CPegasoImportDlg::OnBnClickedEmule()
{
	CString sFolder, sReport;
	GetDlgItemText(IDC_IM_EMULE, sFolder);
	CWaitCursor wait;
	PegasoAuto::ImportEmule(sFolder, sReport);
	SetDlgItemText(IDC_IM_RESULT, _T("eMule: ") + sReport);
}

void CPegasoImportDlg::OnBnClickedQb()
{
	CString sFolder, sReport;
	GetDlgItemText(IDC_IM_QB, sFolder);
	CWaitCursor wait;
	PegasoAuto::ImportQBittorrent(sFolder, sReport);
	SetDlgItemText(IDC_IM_RESULT, _T("qBittorrent: ") + sReport);
}
