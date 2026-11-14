// Pegaso: torrent search on the web through Torznab providers. See WebSearch.h.
#include "stdafx.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include "emule.h"
#include "WebSearch.h"
#include "WebDownload.h"
#include "TorrentQueue.h"
#include "NetGuard.h"
#include "PegasoStrings.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "StringConversion.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const size_t MAX_ANSWER = 8 * 1024 * 1024;
	const size_t MAX_TORRENT = 16 * 1024 * 1024;
	const int MAX_PROVIDERS = 32;
	const UINT_PTR TIMER_POLL = 1;

	CStringA DecodeEntities(CStringA s)
	{
		s.Trim();
		if (s.Left(9) == "<![CDATA[" && s.Right(3) == "]]>")
			return s.Mid(9, s.GetLength() - 12);
		s.Replace("&lt;", "<");
		s.Replace("&gt;", ">");
		s.Replace("&quot;", "\"");
		s.Replace("&apos;", "'");
		// numeric references, decimal or hexadecimal, become UTF-8
		for (int iPos = s.Find("&#"); iPos >= 0; iPos = s.Find("&#", iPos + 1)) {
			const int iEnd = s.Find(';', iPos);
			if (iEnd < 0 || iEnd - iPos > 10)
				continue;
			const bool bHex = s[iPos + 2] == 'x' || s[iPos + 2] == 'X';
			char *pStop;
			const unsigned long uCode = strtoul((LPCSTR)s + iPos + (bHex ? 3 : 2), &pStop, bHex ? 16 : 10);
			if (*pStop != ';' || uCode == 0 || uCode > 0x10FFFF)
				continue;
			char sz[5] = {};
			if (uCode < 0x80)
				sz[0] = (char)uCode;
			else if (uCode < 0x800) {
				sz[0] = (char)(0xC0 | (uCode >> 6));
				sz[1] = (char)(0x80 | (uCode & 0x3F));
			} else if (uCode < 0x10000) {
				sz[0] = (char)(0xE0 | (uCode >> 12));
				sz[1] = (char)(0x80 | ((uCode >> 6) & 0x3F));
				sz[2] = (char)(0x80 | (uCode & 0x3F));
			} else {
				sz[0] = (char)(0xF0 | (uCode >> 18));
				sz[1] = (char)(0x80 | ((uCode >> 12) & 0x3F));
				sz[2] = (char)(0x80 | ((uCode >> 6) & 0x3F));
				sz[3] = (char)(0x80 | (uCode & 0x3F));
			}
			s = s.Left(iPos) + sz + s.Mid(iEnd + 1);
		}
		s.Replace("&amp;", "&");	// last: "&amp;lt;" is the text "&lt;"
		return s;
	}

	// Value of a query parameter: everything but letters, digits and -_.~ is escaped,
	// so "&", "=", "+" or "#" in what the user types cannot break the request.
	CString EncodeQueryValue(const CString &sValue)
	{
		const CStringA sUtf8(StrToUtf8(sValue));
		CString sOut;
		for (int i = 0; i < sUtf8.GetLength(); ++i) {
			const unsigned char c = (unsigned char)sUtf8[i];
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
				sOut += (TCHAR)c;
			else
				sOut.AppendFormat(_T("%%%02X"), c);
		}
		return sOut;
	}

	// text between <name ...> and </name>
	CStringA Element(const CStringA &sItem, LPCSTR pszName)
	{
		const CStringA sOpen(CStringA("<") + pszName);
		for (int iPos = sItem.Find(sOpen); iPos >= 0; iPos = sItem.Find(sOpen, iPos + 1)) {
			const char c = sItem[iPos + sOpen.GetLength()];
			if (c != '>' && c != ' ' && c != '\t' && c != '\r' && c != '\n')
				continue;	// a longer name starting the same way
			const int iStart = sItem.Find('>', iPos);
			const int iEnd = sItem.Find(CStringA("</") + pszName + ">", iStart);
			if (iStart < 0 || iEnd < 0)
				return CStringA();
			return DecodeEntities(sItem.Mid(iStart + 1, iEnd - iStart - 1));
		}
		return CStringA();
	}

	// value of an attribute inside one tag, e.g. url="..."
	CStringA Attribute(const CStringA &sTag, LPCSTR pszName)
	{
		const CStringA sKey(CStringA(pszName) + "=\"");
		int iPos = sTag.Find(sKey);
		while (iPos > 0 && sTag[iPos - 1] != ' ' && sTag[iPos - 1] != '\t' && sTag[iPos - 1] != '\n' && sTag[iPos - 1] != '\r')
			iPos = sTag.Find(sKey, iPos + 1);
		if (iPos < 0)
			return CStringA();
		const int iStart = iPos + sKey.GetLength();
		const int iEnd = sTag.Find('"', iStart);
		return (iEnd < 0) ? CStringA() : DecodeEntities(sTag.Mid(iStart, iEnd - iStart));
	}

	CString FromUtf8(const CStringA &s)
	{
		return CString(OptUtf8ToStr(s));
	}

	CString ProfileKey(int i, LPCTSTR pszWhat)
	{
		CString s;
		s.Format(_T("Torznab%d%s"), i, pszWhat);
		return s;
	}
}

// ---------------------------------------------------------------- CWebSearch

std::vector<SSearchProvider> CWebSearch::LoadProviders()
{
	std::vector<SSearchProvider> out;
	const int iCount = min(MAX_PROVIDERS, (int)theApp.GetProfileInt(_T("eMule"), _T("TorznabCount"), 0));
	for (int i = 0; i < iCount; ++i) {
		SSearchProvider p;
		// URL-encoded: names and keys may hold characters the ini file cannot
		p.name = CString(OptUtf8ToStr(URLDecode(theApp.GetProfileString(_T("eMule"), ProfileKey(i, _T("Name")), _T("")))));
		p.url = theApp.GetProfileString(_T("eMule"), ProfileKey(i, _T("Url")), _T(""));
		p.apiKey = CString(OptUtf8ToStr(URLDecode(theApp.GetProfileString(_T("eMule"), ProfileKey(i, _T("Key")), _T("")))));
		if (!p.url.Trim().IsEmpty())
			out.push_back(p);
	}
	return out;
}

void CWebSearch::SaveProviders(const std::vector<SSearchProvider> &providers)
{
	const int iCount = min(MAX_PROVIDERS, (int)providers.size());
	theApp.WriteProfileInt(_T("eMule"), _T("TorznabCount"), iCount);
	for (int i = 0; i < iCount; ++i) {
		theApp.WriteProfileString(_T("eMule"), ProfileKey(i, _T("Name")), EncodeUrlUtf8(providers[i].name));
		theApp.WriteProfileString(_T("eMule"), ProfileKey(i, _T("Url")), providers[i].url);
		theApp.WriteProfileString(_T("eMule"), ProfileKey(i, _T("Key")), EncodeUrlUtf8(providers[i].apiKey));
	}
}

void CWebSearch::ParseResults(const std::vector<BYTE> &xml, const CString &sProvider, std::vector<SWebSearchResult> &results)
{
	const CStringA sXml(reinterpret_cast<LPCSTR>(xml.data()), (int)xml.size());
	for (int iPos = sXml.Find("<item"); iPos >= 0;) {
		const int iEnd = sXml.Find("</item>", iPos);
		if (iEnd < 0)
			break;
		const CStringA sItem(sXml.Mid(iPos, iEnd - iPos));
		iPos = sXml.Find("<item", iEnd);

		SWebSearchResult r;
		r.provider = sProvider;
		r.title = FromUtf8(Element(sItem, "title"));
		r.size = _strtoui64(Element(sItem, "size"), NULL, 10);
		r.seeds = r.peers = -1;
		CStringA sMagnet, sEnclosure;

		for (int iTag = sItem.Find('<'); iTag >= 0; iTag = sItem.Find('<', iTag + 1)) {
			const int iTagEnd = sItem.Find('>', iTag);
			if (iTagEnd < 0)
				break;
			const CStringA sTag(sItem.Mid(iTag, iTagEnd - iTag + 1));
			if (sTag.Left(13) == "<torznab:attr" || sTag.Left(12) == "<newznab:att") {
				const CStringA sName(Attribute(sTag, "name"));
				const CStringA sValue(Attribute(sTag, "value"));
				if (sName == "seeders")
					r.seeds = atoi(sValue);
				else if (sName == "peers")
					r.peers = atoi(sValue);
				else if (sName == "magneturl")
					sMagnet = sValue;
				else if (sName == "size" && r.size == 0)
					r.size = _strtoui64(sValue, NULL, 10);
			} else if (sTag.Left(10) == "<enclosure") {
				sEnclosure = Attribute(sTag, "url");
				if (r.size == 0)
					r.size = _strtoui64(Attribute(sTag, "length"), NULL, 10);
			}
		}
		const CStringA sLink(!sMagnet.IsEmpty() ? sMagnet : !sEnclosure.IsEmpty() ? sEnclosure : Element(sItem, "link"));
		r.link = FromUtf8(sLink);
		// only what Pegaso can act upon
		if (!r.title.IsEmpty() && (_tcsnicmp(r.link, _T("magnet:?"), 8) == 0 || CWebDownloadQueue::IsWebUrl(r.link)))
			results.push_back(r);
	}
}

// ---------------------------------------------------------------- background search

struct SWebSearchJob
{
	std::mutex mtx;
	std::vector<SWebSearchResult> results;
	CString errors;
	std::atomic<int> pending{0};
	bool changed = false;
};

namespace
{
	void SearchWorker(std::shared_ptr<SWebSearchJob> pJob, SSearchProvider provider, CString sQuery)
	{
		CString sUrl(provider.url);
		sUrl += (sUrl.Find(_T('?')) < 0) ? _T('?') : _T('&');
		sUrl += _T("t=search&q=") + EncodeQueryValue(sQuery);
		if (!provider.apiKey.IsEmpty())
			sUrl += _T("&apikey=") + EncodeQueryValue(provider.apiKey);

		std::vector<BYTE> data;
		CString sError;
		std::vector<SWebSearchResult> found;
		if (PegasoHttpGet(sUrl, data, sError, MAX_ANSWER))
			CWebSearch::ParseResults(data, provider.name, found);

		std::lock_guard<std::mutex> lock(pJob->mtx);
		pJob->results.insert(pJob->results.end(), found.begin(), found.end());
		if (!sError.IsEmpty())
			pJob->errors.AppendFormat(_T("%s%s: %s"), pJob->errors.IsEmpty() ? _T("") : _T("; "), (LPCTSTR)provider.name, (LPCTSTR)sError);
		pJob->changed = true;
		--pJob->pending;
	}
}

// ---------------------------------------------------------------- CWebSearchDlg

IMPLEMENT_DYNAMIC(CWebSearchDlg, CDialog)

BEGIN_MESSAGE_MAP(CWebSearchDlg, CDialog)
	ON_BN_CLICKED(IDC_WS_SEARCH, OnOK)
	ON_BN_CLICKED(IDC_WS_PROVIDERS, OnBnClickedProviders)
	ON_BN_CLICKED(IDC_WS_DOWNLOAD, OnBnClickedDownload)
	ON_NOTIFY(NM_DBLCLK, IDC_WS_LIST, OnNmDblClkList)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CWebSearchDlg::CWebSearchDlg(CWnd *pParent)
	: CDialog(CWebSearchDlg::IDD, pParent)
{
}

CWebSearchDlg::~CWebSearchDlg()
{
}

BOOL CWebSearchDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	m_list.SubclassDlgItem(IDC_WS_LIST, this);
	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT);

	SetWindowText(GetPegasoString(PS_WS_TITLE));
	SetDlgItemText(IDC_WS_SEARCH, GetPegasoString(PS_WS_SEARCH));
	SetDlgItemText(IDC_WS_PROVIDERS, GetPegasoString(PS_WS_PROVIDERS));
	SetDlgItemText(IDC_WS_DOWNLOAD, GetPegasoString(PS_WS_DOWNLOAD));
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));

	CRect rc;
	m_list.GetClientRect(&rc);
	const int iFixed = 80 + 60 + 60 + 110 + ::GetSystemMetrics(SM_CXVSCROLL);
	m_list.InsertColumn(0, GetResString(IDS_DL_FILENAME), LVCFMT_LEFT, max(120, rc.Width() - iFixed));
	m_list.InsertColumn(1, GetResString(IDS_DL_SIZE), LVCFMT_RIGHT, 80);
	m_list.InsertColumn(2, GetPegasoString(PS_WS_COL_SEEDS), LVCFMT_RIGHT, 60);
	m_list.InsertColumn(3, GetPegasoString(PS_WS_COL_PEERS), LVCFMT_RIGHT, 60);
	m_list.InsertColumn(4, GetPegasoString(PS_WS_COL_PROVIDER), LVCFMT_LEFT, 110);

	if (CWebSearch::LoadProviders().empty())
		SetDlgItemText(IDC_WS_STATUS, GetPegasoString(PS_WS_NO_PROVIDERS));
	return TRUE;
}

void CWebSearchDlg::OnOK()
{
	CString sQuery;
	GetDlgItemText(IDC_WS_QUERY, sQuery);
	if (sQuery.Trim().IsEmpty())
		return;
	if (theNetGuard.IsBlocked()) {
		SetDlgItemText(IDC_WS_STATUS, GetPegasoString(PS_WS_BLOCKED));
		return;
	}
	const std::vector<SSearchProvider> providers(CWebSearch::LoadProviders());
	if (providers.empty()) {
		SetDlgItemText(IDC_WS_STATUS, GetPegasoString(PS_WS_NO_PROVIDERS));
		return;
	}

	// an older search still running keeps its own job object and is simply ignored
	m_job = std::make_shared<SWebSearchJob>();
	m_job->pending = (int)providers.size();
	m_results.clear();
	m_list.DeleteAllItems();
	SetDlgItemText(IDC_WS_STATUS, GetPegasoString(PS_WS_SEARCHING));
	for (const SSearchProvider &provider : providers)
		try {
			std::thread(SearchWorker, m_job, provider, sQuery).detach();
		} catch (const std::system_error&) {
			--m_job->pending;
		}
	SetTimer(TIMER_POLL, 300, NULL);
}

void CWebSearchDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TIMER_POLL || !m_job) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	bool bChanged;
	CString sErrors;
	{
		std::lock_guard<std::mutex> lock(m_job->mtx);
		bChanged = m_job->changed;
		m_job->changed = false;
		if (bChanged)
			m_results = m_job->results;
		sErrors = m_job->errors;
	}
	if (bChanged)
		ShowResults();
	if (m_job->pending <= 0) {
		KillTimer(TIMER_POLL);
		CString sStatus;
		sStatus.Format(GetPegasoString(PS_WS_RESULTS), (int)m_results.size());
		if (!sErrors.IsEmpty())
			sStatus += _T("  -  ") + sErrors;
		SetDlgItemText(IDC_WS_STATUS, sStatus);
	}
}

void CWebSearchDlg::ShowResults()
{
	// best seeded first
	std::stable_sort(m_results.begin(), m_results.end(), [](const SWebSearchResult &a, const SWebSearchResult &b) { return a.seeds > b.seeds; });
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	for (size_t i = 0; i < m_results.size(); ++i) {
		const SWebSearchResult &r = m_results[i];
		const int iItem = m_list.InsertItem((int)i, r.title);
		m_list.SetItemText(iItem, 1, r.size ? (LPCTSTR)CastItoXBytes(r.size) : _T(""));
		CString s;
		if (r.seeds >= 0) {
			s.Format(_T("%d"), r.seeds);
			m_list.SetItemText(iItem, 2, s);
		}
		if (r.peers >= 0) {
			s.Format(_T("%d"), r.peers);
			m_list.SetItemText(iItem, 3, s);
		}
		m_list.SetItemText(iItem, 4, r.provider);
	}
	m_list.SetRedraw(TRUE);
}

void CWebSearchDlg::OnNmDblClkList(LPNMHDR, LRESULT *pResult)
{
	OnBnClickedDownload();
	*pResult = 0;
}

void CWebSearchDlg::OnBnClickedDownload()
{
	const int iSel = m_list.GetNextItem(-1, LVNI_SELECTED);
	if (iSel < 0 || (size_t)iSel >= m_results.size())
		return;
	const SWebSearchResult &r = m_results[(size_t)iSel];
	if (theNetGuard.IsBlocked()) {
		SetDlgItemText(IDC_WS_STATUS, GetPegasoString(PS_WS_BLOCKED));
		return;
	}
	if (_tcsnicmp(r.link, _T("magnet:?"), 8) == 0) {
		if (theTorrents.AddLink(r.link))
			SetDlgItemText(IDC_WS_STATUS, r.title);
		return;
	}

	// a .torrent file behind a web address: fetch it and hand it to the engine
	CWaitCursor wait;
	std::vector<BYTE> data;
	CString sError;
	if (!PegasoHttpGet(r.link, data, sError, MAX_TORRENT) || data.empty() || data[0] != 'd') {
		SetDlgItemText(IDC_WS_STATUS, r.title + _T("  -  ") + (sError.IsEmpty() ? CString(_T("not a torrent file")) : sError));
		return;
	}
	const CString sDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("torrents\\"));
	::CreateDirectory(sDir, NULL);
	const CString sPath(sDir + _T("websearch.torrent"));
	FILE *f = _tfsopen(sPath, _T("wb"), _SH_DENYWR);
	if (f == NULL)
		return;
	const bool bWritten = fwrite(data.data(), 1, data.size(), f) == data.size();
	fclose(f);
	if (bWritten && theTorrents.AddLink(sPath))
		SetDlgItemText(IDC_WS_STATUS, r.title);
}

void CWebSearchDlg::OnBnClickedProviders()
{
	CSearchProvidersDlg dlg(this);
	dlg.DoModal();
	SetDlgItemText(IDC_WS_STATUS, CWebSearch::LoadProviders().empty() ? GetPegasoString(PS_WS_NO_PROVIDERS) : _T(""));
}

// ---------------------------------------------------------------- CSearchProvidersDlg

IMPLEMENT_DYNAMIC(CSearchProvidersDlg, CDialog)

BEGIN_MESSAGE_MAP(CSearchProvidersDlg, CDialog)
	ON_LBN_SELCHANGE(IDC_WP_LIST, OnLbnSelChange)
	ON_BN_CLICKED(IDC_WP_SAVE, OnBnClickedSave)
	ON_BN_CLICKED(IDC_WP_REMOVE, OnBnClickedRemove)
END_MESSAGE_MAP()

CSearchProvidersDlg::CSearchProvidersDlg(CWnd *pParent)
	: CDialog(CSearchProvidersDlg::IDD, pParent)
{
}

BOOL CSearchProvidersDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	SetWindowText(GetPegasoString(PS_WS_PROV_TITLE));
	SetDlgItemText(IDC_WP_NAME_LBL, GetPegasoString(PS_WS_PROV_NAME));
	SetDlgItemText(IDC_WP_URL_LBL, GetPegasoString(PS_WS_PROV_URL));
	SetDlgItemText(IDC_WP_KEY_LBL, GetPegasoString(PS_WS_PROV_KEY));
	SetDlgItemText(IDC_WP_SAVE, GetPegasoString(PS_WS_PROV_SAVE));
	SetDlgItemText(IDC_WP_REMOVE, GetPegasoString(PS_WS_PROV_REMOVE));
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));
	m_providers = CWebSearch::LoadProviders();
	FillList(-1);
	return TRUE;
}

void CSearchProvidersDlg::FillList(int iSel)
{
	CListBox *pList = static_cast<CListBox*>(GetDlgItem(IDC_WP_LIST));
	pList->ResetContent();
	for (const SSearchProvider &p : m_providers)
		pList->AddString(p.name + _T("  -  ") + p.url);
	pList->SetCurSel(iSel);
}

void CSearchProvidersDlg::OnLbnSelChange()
{
	const int iSel = static_cast<CListBox*>(GetDlgItem(IDC_WP_LIST))->GetCurSel();
	if (iSel < 0 || (size_t)iSel >= m_providers.size())
		return;
	SetDlgItemText(IDC_WP_NAME, m_providers[(size_t)iSel].name);
	SetDlgItemText(IDC_WP_URL, m_providers[(size_t)iSel].url);
	SetDlgItemText(IDC_WP_KEY, m_providers[(size_t)iSel].apiKey);
}

void CSearchProvidersDlg::OnBnClickedSave()
{
	SSearchProvider p;
	GetDlgItemText(IDC_WP_NAME, p.name);
	GetDlgItemText(IDC_WP_URL, p.url);
	GetDlgItemText(IDC_WP_KEY, p.apiKey);
	p.name.Trim();
	p.url.Trim();
	p.apiKey.Trim();
	if (!CWebDownloadQueue::IsWebUrl(p.url)) {
		GetDlgItem(IDC_WP_URL)->SetFocus();
		::MessageBeep(MB_ICONWARNING);
		return;
	}
	if (p.name.IsEmpty())
		p.name = p.url;

	// same name: update, otherwise add
	size_t i = 0;
	while (i < m_providers.size() && m_providers[i].name.CompareNoCase(p.name) != 0)
		++i;
	if (i < m_providers.size())
		m_providers[i] = p;
	else
		m_providers.push_back(p);
	CWebSearch::SaveProviders(m_providers);
	FillList((int)i);
}

void CSearchProvidersDlg::OnBnClickedRemove()
{
	const int iSel = static_cast<CListBox*>(GetDlgItem(IDC_WP_LIST))->GetCurSel();
	if (iSel < 0 || (size_t)iSel >= m_providers.size())
		return;
	m_providers.erase(m_providers.begin() + iSel);
	CWebSearch::SaveProviders(m_providers);
	FillList(-1);
	SetDlgItemText(IDC_WP_NAME, _T(""));
	SetDlgItemText(IDC_WP_URL, _T(""));
	SetDlgItemText(IDC_WP_KEY, _T(""));
}
