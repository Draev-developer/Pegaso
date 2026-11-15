// Pegaso: search on the web, in the sites of the user.
//
// The user keeps a list of the sites he wants to search in; Pegaso ships with none. A site
// is either
//  - a web page: the address of its search page with %s where the words go. Pegaso fetches
//    that page as a browser would and lists the download links it finds in it (ed2k,
//    magnet, .torrent files, files). Nothing which is specific to any site, no logging in
//    and no getting around what a site puts in the way of programs: where the download
//    is not in the page itself, the page is listed and the user opens it in his browser;
//  - a Torznab API, as spoken by indexer managers such as Jackett and Prowlarr (address of
//    the API and key).
// A search asks all the sites, or the one chosen, in the background.
//
// preferences.ini, section [eMule]: TorznabCount, Torznab<n>Name / Url / Key; WebSearchSite
#pragma once
#include <memory>
#include <vector>
#include "resource.h"

struct SSearchProvider
{
	CString	name;
	CString	url;		// e.g. http://127.0.0.1:9117/api/v2.0/indexers/all/results/torznab/api
	CString	apiKey;

	bool	IsPage() const		{ return url.Find(_T("%s")) >= 0; }	// a search page, not a Torznab API
};

struct SWebSearchResult
{
	CString	title;
	CString	link;		// magnet or ed2k link, or the address of a .torrent file or of a file
	CString	provider;
	CString	text;		// what the link says in its page, if it comes from one
	uint64	size;
	int		seeds;		// -1 if the provider does not tell
	int		peers;
	bool	page = false;	// not a download but a page of the site, for the user to open in his browser
};

struct SWebSearchJob;

class CWebSearch
{
public:
	static std::vector<SSearchProvider> LoadProviders();
	static void SaveProviders(const std::vector<SSearchProvider> &providers);
	// Parses a Torznab answer (RSS with torznab:attr elements).
	static void ParseResults(const std::vector<BYTE> &xml, const CString &sProvider, std::vector<SWebSearchResult> &results);
	// The download links of a web page; sPageUrl is where it came from, for relative addresses.
	static void ParsePage(const std::vector<BYTE> &html, const CString &sPageUrl, const CString &sProvider, std::vector<SWebSearchResult> &results
		, std::vector<SWebSearchResult> *pOtherPages = NULL);
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
	afx_msg void OnLvnItemChangedList(LPNMHDR, LRESULT *pResult);
	afx_msg void OnTimer(UINT_PTR nIDEvent);

private:
	void	ShowResults();
	void	FillSites();

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
