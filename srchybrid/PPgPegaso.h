// Pegaso: options page for web downloads and BitTorrent.
#pragma once
#include <string>
#include "resource.h"

class CPPgPegaso : public CPropertyPage
{
	DECLARE_DYNAMIC(CPPgPegaso)

	enum
	{
		IDD = IDD_PPG_PEGASO
	};

public:
	CPPgPegaso();

	void Localize();

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnChanged()					{ SetModified(TRUE); }
	afx_msg void OnBnClickedEnable();
	afx_msg void OnCbnSelChangeAdapter();
	afx_msg void OnBnClickedClearNow();
	afx_msg void OnBnClickedConn();

private:
	CStringArray m_asAdapters;	// adapter names, in the order of the combo box entries after the first
};

// Pegaso: lets the user choose which files of a torrent are downloaded.
class CTorrentFilesDlg : public CDialog
{
	DECLARE_DYNAMIC(CTorrentFilesDlg)

	enum
	{
		IDD = IDD_TORRENT_FILES
	};

public:
	CTorrentFilesDlg(const std::string &sTorrentId, const CString &sName, CWnd *pParent = NULL);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedAll();
	afx_msg void OnBnClickedNone();

private:
	void	CheckAll(BOOL bCheck);

	std::string	m_sTorrentId;
	CString		m_sName;
	CListCtrl	m_list;
};
