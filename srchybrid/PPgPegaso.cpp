// Pegaso: options page for web downloads and BitTorrent, and the torrent file chooser.
#include "stdafx.h"
#include "emule.h"
#include "PPgPegaso.h"
#include "PegasoStrings.h"
#include "TorrentQueue.h"
#include "WebDownload.h"
#include "NetGuard.h"
#include "PegasoPrivacy.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "PartFile.h"
#include "HybridSwarm.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// ---------------------------------------------------------------- CPPgPegaso

IMPLEMENT_DYNAMIC(CPPgPegaso, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgPegaso, CPropertyPage)
	ON_BN_CLICKED(IDC_PG_BT_ENABLE, OnBnClickedEnable)
	ON_BN_CLICKED(IDC_PG_BT_ENCRYPT, OnChanged)
	ON_EN_CHANGE(IDC_PG_BT_PORT, OnChanged)
	ON_EN_CHANGE(IDC_PG_WEB_CONN, OnChanged)
	ON_CBN_SELCHANGE(IDC_PG_VPN_ADAPTER, OnCbnSelChangeAdapter)
	ON_BN_CLICKED(IDC_PG_VPN_KILL, OnChanged)
	ON_BN_CLICKED(IDC_PG_BT_NOFOLDER, OnChanged)
	ON_BN_CLICKED(IDC_PG_CLEAR_EXIT, OnChanged)
	ON_BN_CLICKED(IDC_PG_CLEAR_NOW, OnBnClickedClearNow)
	ON_BN_CLICKED(IDC_PG_CONN, OnBnClickedConn)
END_MESSAGE_MAP()

CPPgPegaso::CPPgPegaso()
	: CPropertyPage(CPPgPegaso::IDD)
{
}

BOOL CPPgPegaso::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	static_cast<CEdit*>(GetDlgItem(IDC_PG_BT_PORT))->SetLimitText(5);
	static_cast<CEdit*>(GetDlgItem(IDC_PG_WEB_CONN))->SetLimitText(2);
	CheckDlgButton(IDC_PG_BT_ENABLE, CTorrentQueue::IsEnabledInPrefs() ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PG_BT_ENCRYPT, CTorrentQueue::RequireEncryptionInPrefs() ? BST_CHECKED : BST_UNCHECKED);
	SetDlgItemInt(IDC_PG_BT_PORT, CTorrentQueue::PortInPrefs(), FALSE);
	CheckDlgButton(IDC_PG_BT_NOFOLDER, CTorrentQueue::NoFolderInPrefs() ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PG_CLEAR_EXIT, PegasoClearTracesOnExit() ? BST_CHECKED : BST_UNCHECKED);
	SetDlgItemInt(IDC_PG_WEB_CONN, theWebDownloads.GetMaxConnections(), FALSE);
	OnBnClickedEnable();

	// VPN protection: the adapters of this computer, plus the configured one if it does not
	// exist right now (the adapter of a VPN often disappears while the VPN is off)
	CComboBox *pCombo = static_cast<CComboBox*>(GetDlgItem(IDC_PG_VPN_ADAPTER));
	const CString sCurrent(CNetGuard::AdapterInPrefs());
	pCombo->AddString(GetPegasoString(PS_OPT_VPN_NONE));
	int iSel = 0;
	bool bFound = sCurrent.IsEmpty();
	for (const SNetAdapter &adapter : CNetGuard::ListAdapters()) {
		CString sText;
		sText.Format(_T("%s  -  %s"), (LPCTSTR)adapter.name
			, (adapter.up && !adapter.ipv4.IsEmpty()) ? (LPCTSTR)adapter.ipv4 : GetPegasoString(PS_OPT_VPN_DOWN));
		pCombo->AddString(sText);
		m_asAdapters.Add(adapter.name);
		if (adapter.name.CompareNoCase(sCurrent) == 0) {
			iSel = (int)m_asAdapters.GetCount();
			bFound = true;
		}
	}
	if (!bFound) {
		pCombo->AddString(sCurrent + _T("  -  ") + GetPegasoString(PS_OPT_VPN_DOWN));
		m_asAdapters.Add(sCurrent);
		iSel = (int)m_asAdapters.GetCount();
	}
	pCombo->SetCurSel(iSel);
	CheckDlgButton(IDC_PG_VPN_KILL, CNetGuard::KillSwitchInPrefs() ? BST_CHECKED : BST_UNCHECKED);
	OnCbnSelChangeAdapter();
	SetModified(FALSE);

	Localize();
	return TRUE;
}

void CPPgPegaso::OnCbnSelChangeAdapter()
{
	GetDlgItem(IDC_PG_VPN_KILL)->EnableWindow(static_cast<CComboBox*>(GetDlgItem(IDC_PG_VPN_ADAPTER))->GetCurSel() > 0);
	SetModified(TRUE);
}

void CPPgPegaso::OnBnClickedClearNow()
{
	PegasoClearTracesAsk(this);
}

void CPPgPegaso::OnBnClickedConn()
{
	CPegasoConnDlg dlg(this);
	dlg.DoModal();
}

void CPPgPegaso::OnBnClickedEnable()
{
	const BOOL bEnabled = IsDlgButtonChecked(IDC_PG_BT_ENABLE) == BST_CHECKED;
	GetDlgItem(IDC_PG_BT_PORT)->EnableWindow(bEnabled);
	GetDlgItem(IDC_PG_BT_ENCRYPT)->EnableWindow(bEnabled);
	GetDlgItem(IDC_PG_BT_NOFOLDER)->EnableWindow(bEnabled);
	SetModified(TRUE);
}

BOOL CPPgPegaso::OnApply()
{
	UINT uPort = GetDlgItemInt(IDC_PG_BT_PORT, NULL, FALSE);
	if (uPort < 1024 || uPort > 65535) {
		uPort = 6881;
		SetDlgItemInt(IDC_PG_BT_PORT, uPort, FALSE);
	}
	UINT uConnections = GetDlgItemInt(IDC_PG_WEB_CONN, NULL, FALSE);
	uConnections = max(1u, min(16u, uConnections));
	SetDlgItemInt(IDC_PG_WEB_CONN, uConnections, FALSE);

	const int iAdapter = static_cast<CComboBox*>(GetDlgItem(IDC_PG_VPN_ADAPTER))->GetCurSel();
	theNetGuard.ApplyPrefs((iAdapter > 0 && iAdapter <= m_asAdapters.GetCount()) ? m_asAdapters[iAdapter - 1] : CString()
		, IsDlgButtonChecked(IDC_PG_VPN_KILL) == BST_CHECKED);
	theWebDownloads.SetMaxConnections(uConnections);
	theTorrents.ApplyPrefs(IsDlgButtonChecked(IDC_PG_BT_ENABLE) == BST_CHECKED, uPort, IsDlgButtonChecked(IDC_PG_BT_ENCRYPT) == BST_CHECKED);
	theTorrents.SetNoFolder(IsDlgButtonChecked(IDC_PG_BT_NOFOLDER) == BST_CHECKED);
	PegasoSetClearTracesOnExit(IsDlgButtonChecked(IDC_PG_CLEAR_EXIT) == BST_CHECKED);

	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgPegaso::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetPegasoString(PS_OPT_TITLE));
		SetDlgItemText(IDC_PG_BT_GROUP, _T("BitTorrent"));
		SetDlgItemText(IDC_PG_BT_ENABLE, GetPegasoString(PS_OPT_BT_ENABLE));
		SetDlgItemText(IDC_PG_BT_PORT_LBL, GetPegasoString(PS_OPT_BT_PORT));
		SetDlgItemText(IDC_PG_BT_ENCRYPT, GetPegasoString(PS_OPT_BT_ENCRYPT));
		SetDlgItemText(IDC_PG_WEB_GROUP, GetPegasoString(PS_OPT_WEB_GROUP));
		SetDlgItemText(IDC_PG_WEB_CONN_LBL, GetPegasoString(PS_OPT_WEB_CONN));
		SetDlgItemText(IDC_PG_VPN_GROUP, GetPegasoString(PS_OPT_VPN_GROUP));
		SetDlgItemText(IDC_PG_VPN_ADAPTER_LBL, GetPegasoString(PS_OPT_VPN_ADAPTER));
		SetDlgItemText(IDC_PG_VPN_KILL, GetPegasoString(PS_OPT_VPN_KILL));
		SetDlgItemText(IDC_PG_BT_NOFOLDER, GetPegasoString(PS_OPT_BT_NOFOLDER));
		SetDlgItemText(IDC_PG_PRIV_GROUP, GetPegasoString(PS_OPT_PRIV_GROUP));
		SetDlgItemText(IDC_PG_CLEAR_EXIT, GetPegasoString(PS_OPT_CLEAR_EXIT));
		SetDlgItemText(IDC_PG_CLEAR_NOW, GetPegasoString(PS_CT_MENU) + CString(_T("...")));
		SetDlgItemText(IDC_PG_CONN, GetPegasoString(PS_CC_MENU) + CString(_T("...")));
	}
}

// ---------------------------------------------------------------- CTorrentFilesDlg

IMPLEMENT_DYNAMIC(CTorrentFilesDlg, CDialog)

BEGIN_MESSAGE_MAP(CTorrentFilesDlg, CDialog)
	ON_BN_CLICKED(IDC_PG_FILES_ALL, OnBnClickedAll)
	ON_BN_CLICKED(IDC_PG_FILES_NONE, OnBnClickedNone)
END_MESSAGE_MAP()

CTorrentFilesDlg::CTorrentFilesDlg(const std::string &sTorrentId, const CString &sName, CWnd *pParent)
	: CDialog(CTorrentFilesDlg::IDD, pParent)
	, m_sTorrentId(sTorrentId)
	, m_sName(sName)
{
}

BOOL CTorrentFilesDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	m_list.SubclassDlgItem(IDC_PG_FILES_LIST, this);
	m_list.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

	SetWindowText(GetPegasoString(PS_TORRENT_FILES_TITLE) + CString(_T(" - ")) + m_sName);
	SetDlgItemText(IDC_PG_FILES_HINT, GetPegasoString(PS_TORRENT_FILES_HINT));
	SetDlgItemText(IDC_PG_FILES_ALL, GetPegasoString(PS_SELECT_ALL));
	SetDlgItemText(IDC_PG_FILES_NONE, GetPegasoString(PS_SELECT_NONE));
	SetDlgItemText(IDOK, GetResString(IDS_TREEOPTIONS_OK));
	SetDlgItemText(IDCANCEL, GetResString(IDS_CANCEL));

	CRect rc;
	m_list.GetClientRect(&rc);
	const int iSizeWidth = 90;
	m_list.InsertColumn(0, GetResString(IDS_DL_FILENAME), LVCFMT_LEFT, rc.Width() - iSizeWidth - ::GetSystemMetrics(SM_CXVSCROLL));
	m_list.InsertColumn(1, GetResString(IDS_DL_SIZE), LVCFMT_RIGHT, iSizeWidth);

	const std::vector<STorrentFile> files(theTorrents.GetFiles(m_sTorrentId));
	for (size_t i = 0; i < files.size(); ++i) {
		const int iItem = m_list.InsertItem((int)i, files[i].path.c_str());
		m_list.SetItemText(iItem, 1, CastItoXBytes((uint64)files[i].size));
		m_list.SetCheck(iItem, files[i].wanted);
	}
	return TRUE;
}

void CTorrentFilesDlg::CheckAll(BOOL bCheck)
{
	for (int i = m_list.GetItemCount(); --i >= 0;)
		m_list.SetCheck(i, bCheck);
}

void CTorrentFilesDlg::OnBnClickedAll()
{
	CheckAll(TRUE);
}

void CTorrentFilesDlg::OnBnClickedNone()
{
	CheckAll(FALSE);
}

void CTorrentFilesDlg::OnOK()
{
	std::vector<bool> wanted((size_t)m_list.GetItemCount());
	for (int i = m_list.GetItemCount(); --i >= 0;)
		wanted[(size_t)i] = m_list.GetCheck(i) != FALSE;
	theTorrents.SetFilesWanted(m_sTorrentId, wanted);
	CDialog::OnOK();
}

// ---------------------------------------------------------------- CPegasoAddTorrentDlg

IMPLEMENT_DYNAMIC(CPegasoAddTorrentDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoAddTorrentDlg, CDialog)
	ON_BN_CLICKED(IDC_AT_BROWSE, OnBnClickedBrowse)
	ON_LBN_SELCHANGE(IDC_AT_LIST, OnLbnSelChange)
	ON_LBN_DBLCLK(IDC_AT_LIST, OnLbnDblClk)
	ON_EN_CHANGE(IDC_AT_LINK, OnEnChangeLink)
END_MESSAGE_MAP()

CPegasoAddTorrentDlg::CPegasoAddTorrentDlg(CPartFile *pFile, CWnd *pParent)
	: CDialog(CPegasoAddTorrentDlg::IDD, pParent)
	, m_pFile(pFile)
	, m_bSetting(false)
{
}

BOOL CPegasoAddTorrentDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	CString sTitle(GetPegasoString(PS_HYB_ADD));
	sTitle.TrimRight(_T('.'));
	SetWindowText(sTitle + _T(" - ") + m_pFile->GetFileName());
	SetDlgItemText(IDC_AT_LINK_LBL, GetPegasoString(PS_HYB_HINT));
	SetDlgItemText(IDC_AT_BROWSE, GetResString(IDS_PW_BROWSE));
	SetDlgItemText(IDOK, GetResString(IDS_TREEOPTIONS_OK));
	SetDlgItemText(IDCANCEL, GetResString(IDS_CANCEL));

	CWaitCursor wait;
	m_candidates = theHybrid.Candidates(m_pFile);
	const CString sSize(CastItoXBytes((uint64)m_pFile->GetFileSize()));
	CString sLabel;
	sLabel.Format(GetPegasoString(m_candidates.empty() ? PS_AT_NONE : PS_AT_LIST), (LPCTSTR)sSize);
	SetDlgItemText(IDC_AT_LIST_LBL, sLabel);
	CListBox *pList = static_cast<CListBox*>(GetDlgItem(IDC_AT_LIST));
	for (const CHybridSwarm::SCandidate &candidate : m_candidates)
		pList->AddString(candidate.text);
	pList->EnableWindow(!m_candidates.empty());
	if (m_candidates.size() == 1)
		pList->SetCurSel(0);	// the one there is: OK takes it
	GetDlgItem(m_candidates.size() == 1 ? IDC_AT_LIST : IDC_AT_LINK)->SetFocus();
	return FALSE;
}

void CPegasoAddTorrentDlg::OnBnClickedBrowse()
{
	CFileDialog dlg(TRUE, _T("torrent"), NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, _T("Torrent (*.torrent)|*.torrent|*.*|*.*||"), this);
	const CString sStart(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));	// where a downloaded .torrent file is
	dlg.m_ofn.lpstrInitialDir = sStart;
	if (dlg.DoModal() == IDOK)
		SetDlgItemText(IDC_AT_LINK, dlg.GetPathName());
}

void CPegasoAddTorrentDlg::OnLbnSelChange()
{
	// one or the other: what is chosen in the list is not what is written above
	if (static_cast<CListBox*>(GetDlgItem(IDC_AT_LIST))->GetCurSel() >= 0) {
		m_bSetting = true;
		SetDlgItemText(IDC_AT_LINK, _T(""));
		m_bSetting = false;
	}
}

void CPegasoAddTorrentDlg::OnEnChangeLink()
{
	if (!m_bSetting && GetDlgItem(IDC_AT_LINK)->GetWindowTextLength() > 0)
		static_cast<CListBox*>(GetDlgItem(IDC_AT_LIST))->SetCurSel(-1);
}

void CPegasoAddTorrentDlg::OnLbnDblClk()
{
	OnOK();
}

void CPegasoAddTorrentDlg::OnOK()
{
	CString sLink;
	GetDlgItemText(IDC_AT_LINK, sLink);
	const int iSel = static_cast<CListBox*>(GetDlgItem(IDC_AT_LIST))->GetCurSel();
	if (!sLink.Trim().IsEmpty())
		theHybrid.Link(m_pFile, sLink);
	else if (iSel >= 0 && (size_t)iSel < m_candidates.size()) {
		const CHybridSwarm::SCandidate &candidate = m_candidates[(size_t)iSel];
		if (candidate.listed)
			theHybrid.LinkListed(m_pFile, candidate.id);
		else
			theHybrid.Link(m_pFile, candidate.path);
	} else
		return;		// nothing written and nothing chosen
	CDialog::OnOK();
}
