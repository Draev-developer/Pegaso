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
#pragma once
#include "ResizableLib/ResizableDialog.h"
#include "IconStatic.h"
#include "ButtonsTabCtrl.h"


class CDirectDownloadDlg : public CResizableDialog
{
	DECLARE_DYNAMIC(CDirectDownloadDlg)

	enum
	{
		IDD = IDD_DIRECT_DOWNLOAD
	};

public:
	explicit CDirectDownloadDlg(CWnd *pParent = NULL);	// standard constructor
	virtual	~CDirectDownloadDlg();

	// Pegaso: links found in the clipboard
	CString	m_sInitialLinks;	// shown when the window opens
	bool	m_bFromClipboard;	// opened by a copied link: comes to the front even if Pegaso is in the background
	// More links for a window which is open already; those it has are not repeated.
	void	AppendLinks(const CString &sLinks);
	// The download links in a text (ed2k file links, magnet links of torrents, web addresses
	// of files), one per line; empty if there is none.
	static CString ExtractLinks(const CString &sText);

protected:
	HICON m_icoWnd;
	CIconStatic m_ctrlDirectDlFrm;
	CButtonsTabCtrl	m_cattabs;

	void UpdateControls();
	void UpdateCatTabs();

	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange *pDX);	// DDX/DDV support
	virtual void OnOK();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnEnKillfocusElink();
	afx_msg void OnEnUpdateElink();
};