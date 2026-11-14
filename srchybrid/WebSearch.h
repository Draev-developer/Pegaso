// Pegaso: torrent search on the web through Torznab providers.
//
// Torznab is the search API spoken by indexer managers such as Jackett and Prowlarr.
// Pegaso ships without any provider: the user adds the ones he runs or is entitled to
// use (name, address of the API, key). A search asks every provider in the background
// and lists what they answer; a result is added as a magnet link or .torrent file.
//
// preferences.ini, section [eMule]: TorznabCount, Torznab<n>Name / Url / Key
#pragma once
#include <memory>
#include <vector>
#include "resource.h"

struct SSearchProvider
{
	CString	name;
	CString	url;		// e.g. http://127.0.0.1:9117/api/v2.0/indexers/all/results/torznab/api
	CString	apiKey;
};

struct SWebSearchResult
{
	CString	title;
	CString	link;		// magnet link, or the address of a .torrent file
	CString	provider;
	uint64	size;
	int		seeds;		// -1 if the provider does not tell
	int		peers;
};

struct SWebSearchJob;

class CWebSearch
{
public:
	static std::vector<SSearchProvider> LoadProviders();
	static void SaveProviders(const std::vector<SSearchProvider> &providers);
	// Parses a Torznab answer (RSS with torznab:attr elements).
	static void ParseResults(const std::vector<BYTE> &xml, const CString &sProvider, std::vector<SWebSearchResult> &results);
};

class CWebSearchDlg : public CDialog
{
	DECLARE_DYNAMIC(CWebSearchDlg)

	enum
	{
		IDD = IDD_WEB_SEARCH
	};

public:
	explicit CWebSearchDlg(CWnd *pParent = NULL);
	virtual ~CWebSearchDlg();

	CString	m_sInitialQuery;	// if not empty, the search starts with this as soon as the window opens

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();		// Enter starts the search

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedProviders();
	afx_msg void OnBnClickedDownload();
	afx_msg void OnNmDblClkList(LPNMHDR, LRESULT *pResult);
	afx_msg void OnTimer(UINT_PTR nIDEvent);

private:
	void	ShowResults();

	CListCtrl m_list;
	std::shared_ptr<SWebSearchJob> m_job;
	std::vector<SWebSearchResult> m_results;	// as shown, sorted
};

class CSearchProvidersDlg : public CDialog
{
	DECLARE_DYNAMIC(CSearchProvidersDlg)

	enum
	{
		IDD = IDD_WEB_PROVIDERS
	};

public:
	explicit CSearchProvidersDlg(CWnd *pParent = NULL);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK()							{ OnBnClickedSave(); }

	DECLARE_MESSAGE_MAP()
	afx_msg void OnLbnSelChange();
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedRemove();

private:
	void	FillList(int iSel);

	std::vector<SSearchProvider> m_providers;
};
