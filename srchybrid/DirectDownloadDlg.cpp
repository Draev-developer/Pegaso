//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "emule.h"
#include "DirectDownloadDlg.h"
#include "WebDownload.h"
#include "TorrentQueue.h"
#include "ED2KLink.h"
#include "DownloadQueue.h"
#include "Preferences.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#include "PegasoStrings.h"

#define	PREF_INI_SECTION	_T("DirectDownloadDlg")

IMPLEMENT_DYNAMIC(CDirectDownloadDlg, CDialog)

BEGIN_MESSAGE_MAP(CDirectDownloadDlg, CResizableDialog)
	ON_EN_KILLFOCUS(IDC_ELINK, OnEnKillfocusElink)
	ON_EN_UPDATE(IDC_ELINK, OnEnUpdateElink)
END_MESSAGE_MAP()

CDirectDownloadDlg::CDirectDownloadDlg(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CDirectDownloadDlg::IDD, pParent)
	, m_bFromClipboard()
	, m_icoWnd()
{
}

CString CDirectDownloadDlg::ExtractLinks(const CString &sText)
{
	CString sOut;
	if (sText.GetLength() > 64 * 1024)	// not a list of links somebody copied
		return sOut;
	for (int iPos = 0; iPos >= 0;) {
		const CString &sToken(sText.Tokenize(_T(" \t\r\n"), iPos));
		if (sToken.IsEmpty())
			break;
		const bool bLink = _tcsnicmp(sToken, _T("ed2k://|file|"), 13) == 0
			|| (_tcsnicmp(sToken, _T("magnet:?"), 8) == 0 && CTorrentQueue::IsTorrentLink(sToken))
			|| CWebDownloadQueue::LooksLikeFileUrl(sToken);
		if (bLink && (_T("\r\n") + sOut).Find(_T("\r\n") + sToken + _T("\r\n")) < 0)
			sOut += sToken + _T("\r\n");
	}
	return sOut;
}

void CDirectDownloadDlg::AppendLinks(const CString &sLinks)
{
	if (m_hWnd == NULL)
		return;
	CString sHave;
	GetDlgItemText(IDC_ELINK, sHave);
	if (!sHave.IsEmpty() && sHave.Right(1) != _T("\n"))
		sHave += _T("\r\n");
	bool bAdded = false;
	for (int iPos = 0; iPos >= 0;) {
		const CString &sToken(sLinks.Tokenize(_T("\r\n"), iPos));
		if (sToken.IsEmpty())
			break;
		if ((_T("\r\n") + sHave).Find(_T("\r\n") + sToken + _T("\r\n")) < 0) {
			sHave += sToken + _T("\r\n");
			bAdded = true;
		}
	}
	if (bAdded) {
		SetDlgItemText(IDC_ELINK, sHave);
		static_cast<CEdit*>(GetDlgItem(IDC_ELINK))->LineScroll(0x7FFF);
		UpdateControls();
		FlashWindow(TRUE);
	}
}

CDirectDownloadDlg::~CDirectDownloadDlg()
{
	if (m_icoWnd)
		VERIFY(::DestroyIcon(m_icoWnd));
}

void CDirectDownloadDlg::DoDataExchange(CDataExchange *pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DDOWN_FRM, m_ctrlDirectDlFrm);
	DDX_Control(pDX, IDC_CATS, m_cattabs);
}

void CDirectDownloadDlg::UpdateControls()
{
	GetDlgItem(IDOK)->EnableWindow(GetDlgItem(IDC_ELINK)->GetWindowTextLength() > 0);
}

void CDirectDownloadDlg::OnEnUpdateElink()
{
	UpdateControls();
}

void CDirectDownloadDlg::OnEnKillfocusElink()
{
	CString strLinks;
	GetDlgItemText(IDC_ELINK, strLinks);
	if (!strLinks.IsEmpty()) {
		strLinks.Replace(_T("\n"), _T("\r\n"));
		strLinks.Replace(_T("\r\r"), _T("\r"));
		SetDlgItemText(IDC_ELINK, strLinks);
	}
}

void CDirectDownloadDlg::OnOK()
{
	CString strLinks;
	GetDlgItemText(IDC_ELINK, strLinks);

	for (int iPos = 0; iPos >= 0;) {
		const CString &sToken(strLinks.Tokenize(_T(" \t\r\n"), iPos)); //tokenize by whitespace
		if (sToken.IsEmpty())
			break;
		if (theTorrents.AddLink(sToken))	// Pegaso: magnet link of a torrent
			continue;
		if (CWebDownloadQueue::IsWebUrl(sToken)) {	// Pegaso: web address instead of an ed2k link
			theWebDownloads.AddUrl(sToken, thePrefs.GetCatCount() ? m_cattabs.GetCurSel() : 0);
			continue;
		}
		bool bSlash = (sToken[sToken.GetLength() - 1] == _T('/'));
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(bSlash ? sToken : sToken + _T('/'));
			if (pLink) {
				if (pLink->GetKind() != CED2KLink::kFile)
					throwCStr(_T("bad link"));
				theApp.downloadqueue->AddFileLinkToDownload(*pLink->GetFileLink(), thePrefs.GetCatCount() ? m_cattabs.GetCurSel() : 0);
				delete pLink;
				pLink = NULL;
			}
		} catch (const CString &error) {
			delete pLink;
			CString sBuffer;
			sBuffer.Format(GetResString(IDS_ERR_INVALIDLINK), (LPCTSTR)error);
			CString strError;
			strError.Format(GetResString(IDS_ERR_LINKERROR), (LPCTSTR)sBuffer);
			AfxMessageBox(strError);
			return;
		}
	}

	CResizableDialog::OnOK();
}

BOOL CDirectDownloadDlg::OnInitDialog()
{
	CResizableDialog::OnInitDialog();
	InitWindowStyles(this);
	SetIcon(m_icoWnd = theApp.LoadIcon(_T("PasteLink")), FALSE);

	if (theApp.IsVistaThemeActive())
		m_cattabs.ModifyStyle(0, TCS_HOTTRACK);

	AddAnchor(IDC_DDOWN_FRM, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_ELINK, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDCANCEL, BOTTOM_RIGHT);
	AddAnchor(IDOK, BOTTOM_RIGHT);
	AddAnchor(IDC_CATLABEL, BOTTOM_LEFT);
	AddAnchor(IDC_CATS, BOTTOM_LEFT, BOTTOM_RIGHT);

	EnableSaveRestore(PREF_INI_SECTION);

	SetWindowText(GetPegasoString(PS_ADD_LINKS));
	m_ctrlDirectDlFrm.SetIcon(_T("Download"));
	m_ctrlDirectDlFrm.SetWindowText(GetPegasoString(PS_ADD_LINKS));
	SetDlgItemText(IDOK, GetResString(IDS_DOWNLOAD));
	SetDlgItemText(IDC_FSTATIC2, GetPegasoString(PS_ADD_LINKS_HINT));
	if (!m_sInitialLinks.IsEmpty()) {
		SetDlgItemText(IDC_ELINK, m_sInitialLinks);
		// the caret goes to the end once the dialog has given the focus to the box (which
		// selects everything): what is typed next is added, not put in place of the links
		GetDlgItem(IDC_ELINK)->PostMessage(EM_SETSEL, 0x7FFF, 0x7FFF);
	}
	if (m_bFromClipboard) {	// the user is in another program, e.g. the browser
		SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		SetForegroundWindow();
	}
	SetDlgItemText(IDC_CATLABEL, GetResString(IDS_CAT) + _T(':'));

	SetDlgItemText(IDOK, GetResString(IDS_DOWNLOAD));
	SetDlgItemText(IDCANCEL, GetResString(IDS_CANCEL));

	if (thePrefs.GetCatCount() == 0) {
		GetDlgItem(IDC_CATLABEL)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_CATS)->ShowWindow(SW_HIDE);
	} else {
		UpdateCatTabs();
		if (theApp.m_fontSymbol.m_hObject) {
			GetDlgItem(IDC_CATLABEL)->SetFont(&theApp.m_fontSymbol);
			SetDlgItemText(IDC_CATLABEL, (GetExStyle() & WS_EX_LAYOUTRTL) ? _T("3") : _T("4")); // show a right-arrow
		}

	}

	UpdateControls();

	return TRUE;	// return TRUE unless you set the focus to the control
					// EXCEPTION: OCX Property Pages should return FALSE
}

void CDirectDownloadDlg::UpdateCatTabs()
{
	int oldsel = m_cattabs.GetCurSel();
	m_cattabs.DeleteAllItems();
	for (INT_PTR i = thePrefs.GetCatCount(); --i >= 0;) {
		CString label(i ? thePrefs.GetCategory(i)->strTitle : GetResString(IDS_ALL));
		DupAmpersand(label);
		m_cattabs.InsertItem(0, label);
	}

	m_cattabs.SetCurSel(oldsel >= 0 && oldsel < m_cattabs.GetItemCount() ? oldsel : 0);
}