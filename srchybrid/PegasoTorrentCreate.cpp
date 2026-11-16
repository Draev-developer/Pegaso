// Pegaso: making a torrent of a file or folder of the user. See PegasoTorrentCreate.h.
#include "stdafx.h"
#include <atomic>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include "emule.h"
#include "PegasoTorrentCreate.h"
#include "PegasoStrings.h"
#include "TorrentQueue.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "StringConversion.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

struct SCreateJob
{
	std::atomic<bool> done{false};
	std::atomic<int> pieces{0};
	std::atomic<int> total{0};
	bool	ok = false;
	std::string magnet;
	std::wstring error;
};

namespace
{
	const UINT_PTR TIMER_POLL = 1;
	CPegasoTorrentCreateDlg *s_pOpen;

	void CreateWorker(std::shared_ptr<SCreateJob> pJob, STorrentCreate what, std::wstring torrentPath)
	{
		pJob->ok = CTorrentEngine::CreateTorrent(what, torrentPath, pJob->magnet, pJob->error, [pJob](int iDone, int iTotal) {
			pJob->pieces = iDone;
			pJob->total = iTotal;
		});
		pJob->done = true;
	}

	CString ProfileText(LPCTSTR pszKey)
	{
		return CString(OptUtf8ToStr(URLDecode(theApp.GetProfileString(_T("eMule"), pszKey, _T("")))));
	}
}

IMPLEMENT_DYNAMIC(CPegasoTorrentCreateDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoTorrentCreateDlg, CDialog)
	ON_BN_CLICKED(IDC_CR_FILE, OnBnClickedFile)
	ON_BN_CLICKED(IDC_CR_FOLDER, OnBnClickedFolder)
	ON_BN_CLICKED(IDC_CR_COPY, OnBnClickedCopy)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CPegasoTorrentCreateDlg::CPegasoTorrentCreateDlg(CWnd *pParent)
	: CDialog(CPegasoTorrentCreateDlg::IDD, pParent)
	, m_bSeed()
{
}

CPegasoTorrentCreateDlg::~CPegasoTorrentCreateDlg()
{
}

void CPegasoTorrentCreateDlg::Open(CWnd *pParent)
{
	if (s_pOpen != NULL) {
		s_pOpen->ShowWindow(SW_RESTORE);
		s_pOpen->SetForegroundWindow();
		return;
	}
	s_pOpen = new CPegasoTorrentCreateDlg(pParent);
	if (!s_pOpen->Create(CPegasoTorrentCreateDlg::IDD, pParent)) {
		delete s_pOpen;	// not destroyed as a window: there never was one
		s_pOpen = NULL;
		return;
	}
	s_pOpen->ShowWindow(SW_SHOW);
}

void CPegasoTorrentCreateDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (s_pOpen == this)
		s_pOpen = NULL;
	delete this;
}

void CPegasoTorrentCreateDlg::OnCancel()
{
	// while the files are being read the window stays: the result has to be told somewhere
	if (m_job && !m_job->done)
		return;
	DestroyWindow();
}

BOOL CPegasoTorrentCreateDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	SetWindowText(GetPegasoString(PS_CR_TITLE));
	SetDlgItemText(IDC_CR_PATH_LBL, GetPegasoString(PS_CR_PATH));
	SetDlgItemText(IDC_CR_FILE, GetPegasoString(PS_CR_FILE));
	SetDlgItemText(IDC_CR_FOLDER, GetPegasoString(PS_CR_FOLDER));
	SetDlgItemText(IDC_CR_TRACKERS_LBL, GetPegasoString(PS_CR_TRACKERS));
	SetDlgItemText(IDC_CR_PRIVATE, GetPegasoString(PS_CR_PRIVATE));
	SetDlgItemText(IDC_CR_SEED, GetPegasoString(PS_CR_SEED));
	SetDlgItemText(IDC_CR_MAGNET_LBL, GetPegasoString(PS_CR_MAGNET));
	SetDlgItemText(IDC_CR_COPY, GetPegasoString(PS_CR_COPY));
	SetDlgItemText(IDOK, GetPegasoString(PS_CR_CREATE));
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));
	SetDlgItemText(IDC_CR_TRACKERS, ProfileText(_T("TorrentCreateTrackers")));
	CheckDlgButton(IDC_CR_SEED, BST_CHECKED);
	static_cast<CProgressCtrl*>(GetDlgItem(IDC_CR_PROGRESS))->SetRange(0, 1000);
	GetDlgItem(IDC_CR_PROGRESS)->ShowWindow(SW_HIDE);
	GetDlgItem(IDC_CR_COPY)->EnableWindow(FALSE);
	return TRUE;
}

void CPegasoTorrentCreateDlg::Say(const CString &sText)
{
	SetDlgItemText(IDC_CR_STATUS, sText);
}

void CPegasoTorrentCreateDlg::OnBnClickedFile()
{
	CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, NULL, this);
	if (dlg.DoModal() == IDOK)
		SetDlgItemText(IDC_CR_PATH, dlg.GetPathName());
}

void CPegasoTorrentCreateDlg::OnBnClickedFolder()
{
	TCHAR szPath[MAX_PATH] = {};
	GetDlgItemText(IDC_CR_PATH, szPath, _countof(szPath));
	if (SelectDir(GetSafeHwnd(), szPath, GetPegasoString(PS_CR_PATH)))
		SetDlgItemText(IDC_CR_PATH, szPath);
}

void CPegasoTorrentCreateDlg::OnOK()
{
	if (m_job && !m_job->done)
		return;
	CString sPath;
	GetDlgItemText(IDC_CR_PATH, sPath);
	sPath.Trim(_T(" \t\""));
	sPath.TrimRight(_T('\\'));
	if (sPath.GetLength() < 3 || ::GetFileAttributes(sPath) == INVALID_FILE_ATTRIBUTES) {
		Say(GetPegasoString(PS_CR_NOPATH));
		GetDlgItem(IDC_CR_PATH)->SetFocus();
		return;
	}
	CString sTrackers;
	GetDlgItemText(IDC_CR_TRACKERS, sTrackers);
	theApp.WriteProfileString(_T("eMule"), _T("TorrentCreateTrackers"), EncodeUrlUtf8(sTrackers));

	STorrentCreate what;
	what.path = (LPCTSTR)sPath;
	what.privateTorrent = IsDlgButtonChecked(IDC_CR_PRIVATE) != 0;
	for (int iPos = 0; iPos >= 0;) {
		CString sTracker(sTrackers.Tokenize(_T(" \t\r\n"), iPos));
		if (sTracker.Trim().IsEmpty())
			continue;
		// what a tracker address may be; anything else would only spoil the torrent
		if (_tcsnicmp(sTracker, _T("http://"), 7) == 0 || _tcsnicmp(sTracker, _T("https://"), 8) == 0 || _tcsnicmp(sTracker, _T("udp://"), 6) == 0)
			what.trackers.push_back(std::string(CStringA(StrToUtf8(sTracker))));
	}
	m_sData = sPath;
	m_sTorrent = sPath + _T(".torrent");	// next to what it is made of
	m_bSeed = IsDlgButtonChecked(IDC_CR_SEED) != 0;
	SetDlgItemText(IDC_CR_MAGNET, _T(""));
	GetDlgItem(IDC_CR_COPY)->EnableWindow(FALSE);
	GetDlgItem(IDOK)->EnableWindow(FALSE);
	static_cast<CProgressCtrl*>(GetDlgItem(IDC_CR_PROGRESS))->SetPos(0);
	GetDlgItem(IDC_CR_PROGRESS)->ShowWindow(SW_SHOW);
	CString sStatus;
	sStatus.Format(GetPegasoString(PS_CR_HASHING), 0);
	Say(sStatus);

	m_job = std::make_shared<SCreateJob>();
	try {
		std::thread(CreateWorker, m_job, what, std::wstring((LPCTSTR)m_sTorrent)).detach();
	} catch (const std::system_error&) {
		m_job->error = L"no thread";
		m_job->done = true;
	}
	SetTimer(TIMER_POLL, 200, NULL);
}

void CPegasoTorrentCreateDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TIMER_POLL || !m_job) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	const int iTotal = m_job->total;
	if (iTotal > 0) {
		const int iPos = (int)((LONGLONG)m_job->pieces * 1000 / iTotal);
		static_cast<CProgressCtrl*>(GetDlgItem(IDC_CR_PROGRESS))->SetPos(iPos);
		if (!m_job->done) {
			CString sStatus;
			sStatus.Format(GetPegasoString(PS_CR_HASHING), iPos / 10);
			Say(sStatus);
		}
	}
	if (!m_job->done)
		return;
	KillTimer(TIMER_POLL);
	GetDlgItem(IDC_CR_PROGRESS)->ShowWindow(SW_HIDE);
	GetDlgItem(IDOK)->EnableWindow(TRUE);
	if (!m_job->ok) {
		Say(CString(m_job->error.c_str()));
		return;
	}
	SetDlgItemText(IDC_CR_MAGNET, CString(m_job->magnet.c_str()));
	GetDlgItem(IDC_CR_COPY)->EnableWindow(TRUE);
	CString sStatus;
	sStatus.Format(GetPegasoString(PS_CR_DONE), (LPCTSTR)m_sTorrent);
	if (m_bSeed) {
		// the data was read a moment ago: it is offered without being checked once more
		std::wstring error;
		const CString sFolder(m_sData.Left(m_sData.ReverseFind(_T('\\'))));
		if (!theTorrents.IsRunning())
			theTorrents.Start();
		if (theTorrents.Engine().AddSeed(std::wstring((LPCTSTR)m_sTorrent), std::wstring((LPCTSTR)sFolder), error))
			sStatus += _T(' ') + CString(GetPegasoString(PS_CR_SEEDING));
		else
			sStatus += _T("  -  ") + CString(error.c_str());
	}
	Say(sStatus);
	AddLogLine(false, _T("Torrent created: %s"), (LPCTSTR)m_sTorrent);
}

void CPegasoTorrentCreateDlg::OnBnClickedCopy()
{
	CString sMagnet;
	GetDlgItemText(IDC_CR_MAGNET, sMagnet);
	if (!sMagnet.IsEmpty() && ::CopyTextToClipboard(sMagnet))
		Say(GetPegasoString(PS_CR_COPIED));
}
