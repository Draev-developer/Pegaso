// Pegaso: torrent search on the web through Torznab providers. See WebSearch.h.
#include "stdafx.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include "emule.h"
#include "WebSearch.h"
#include "WebDownload.h"
#include "ED2KLink.h"
#include "DownloadQueue.h"
#include "TorrentQueue.h"
#include "NetGuard.h"
#include "PegasoStrings.h"
#include "PegasoPrivacy.h"
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
	const int MAX_PAGES = 8;	// pages opened for one search in one site
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

	// the address of a .torrent file, as opposed to the one of any other file
	bool IsTorrentAddress(const CString &sUrl)
	{
		CString sPath(sUrl);
		const int iCut = sPath.FindOneOf(_T("?#"));
		if (iCut >= 0)
			sPath.Truncate(iCut);
		if (sPath.Right(8).CompareNoCase(_T(".torrent")) == 0)
			return true;
		// ".../download.php?file=name.torrent" is one as well
		CString sWhole(sUrl);
		const int iFragment = sWhole.Find(_T('#'));
		if (iFragment >= 0)
			sWhole.Truncate(iFragment);
		return sWhole.Right(8).CompareNoCase(_T(".torrent")) == 0;
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
		const CString sStoredKey(theApp.GetProfileString(_T("eMule"), ProfileKey(i, _T("Key")), _T("")));
		p.apiKey = sStoredKey.Left(6) == _T("dpapi:") ? PegasoUnprotect(sStoredKey) : CString(OptUtf8ToStr(URLDecode(sStoredKey)));
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
		theApp.WriteProfileString(_T("eMule"), ProfileKey(i, _T("Key")), PegasoProtect(providers[i].apiKey));
	}
}

namespace
{
	// "href" of a tag, whatever its quotes
	CStringA Href(const CStringA &sTag)
	{
		CStringA sLower(sTag);
		sLower.MakeLower();
		int iPos = sLower.Find("href");
		while (iPos > 0 && sLower[iPos - 1] != ' ' && sLower[iPos - 1] != '\t' && sLower[iPos - 1] != '\n' && sLower[iPos - 1] != '\r')
			iPos = sLower.Find("href", iPos + 1);
		if (iPos < 0)
			return CStringA();
		iPos += 4;
		while (iPos < sTag.GetLength() && (sTag[iPos] == ' ' || sTag[iPos] == '='))
			++iPos;
		if (iPos >= sTag.GetLength())
			return CStringA();
		const char cQuote = sTag[iPos];
		int iEnd;
		if (cQuote == '"' || cQuote == '\'') {
			iEnd = sTag.Find(cQuote, ++iPos);
			if (iEnd < 0)
				return CStringA();
		} else {
			iEnd = iPos;
			while (iEnd < sTag.GetLength() && sTag[iEnd] != ' ' && sTag[iEnd] != '>')
				++iEnd;
		}
		return DecodeEntities(sTag.Mid(iPos, iEnd - iPos));
	}

	// what is written between <a> and </a>, without its tags
	CStringA PlainText(const CStringA &sHtml)
	{
		CStringA sOut;
		bool bTag = false;
		bool bSpace = false;
		for (int i = 0; i < sHtml.GetLength(); ++i) {
			const char c = sHtml[i];
			if (c == '<')
				bTag = true;
			else if (c == '>') {
				bTag = false;
				bSpace = true;
			} else if (!bTag) {
				if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
					bSpace = true;
				else {
					if (bSpace && !sOut.IsEmpty())
						sOut += ' ';
					bSpace = false;
					sOut += c;
				}
			}
		}
		return DecodeEntities(sOut);
	}

	// An address as written in a page, made complete with the address of the page.
	CString Absolute(const CString &sLink, const CString &sPageUrl)
	{
		if (sLink.Find(_T("://")) > 0 || _tcsnicmp(sLink, _T("magnet:"), 7) == 0)
			return sLink;
		const int iHost = sPageUrl.Find(_T("://"));
		if (iHost < 0)
			return CString();
		int iPath = sPageUrl.Find(_T('/'), iHost + 3);
		const CString sOrigin(iPath < 0 ? sPageUrl : sPageUrl.Left(iPath));
		if (sLink.Left(2) == _T("//"))
			return sPageUrl.Left(iHost + 1) + sLink;
		if (sLink.Left(1) == _T("/"))
			return sOrigin + sLink;
		CString sDir(iPath < 0 ? sPageUrl + _T('/') : sPageUrl);
		const int iCut = sDir.FindOneOf(_T("?#"));
		if (iCut >= 0)
			sDir.Truncate(iCut);
		sDir.Truncate(sDir.ReverseFind(_T('/')) + 1);
		return sDir + sLink;
	}

	CString HostOf(const CString &sUrl)
	{
		const int iHost = sUrl.Find(_T("://"));
		if (iHost < 0)
			return CString();
		CString s(sUrl.Mid(iHost + 3));
		const int iEnd = s.FindOneOf(_T("/?#"));
		if (iEnd >= 0)
			s.Truncate(iEnd);
		return s.MakeLower();
	}

	CString LastSegment(const CString &sUrl)
	{
		CString s(sUrl);
		const int iCut = s.FindOneOf(_T("?#"));
		if (iCut >= 0)
			s.Truncate(iCut);
		s = s.Mid(s.ReverseFind(_T('/')) + 1);
		return CString(OptUtf8ToStr(URLDecode(s)));
	}
}

// The download links of a web page. A link counts if it is an ed2k or magnet link, the
// address of a .torrent file or the address of something which looks like a file to
// download; the rest of the page - menus, pictures, other pages - is left out, or, if asked
// for, the links to other pages of the same site are given apart, each with its text as title.
void CWebSearch::ParsePage(const std::vector<BYTE> &html, const CString &sPageUrl, const CString &sProvider, std::vector<SWebSearchResult> &results
	, std::vector<SWebSearchResult> *pOtherPages)
{
	const CStringA sPage((LPCSTR)html.data(), (int)min(html.size(), (size_t)INT_MAX));
	CStringA sLower(sPage);
	sLower.MakeLower();
	const size_t uFirst = results.size();
	for (int iPos = sLower.Find("<a"); iPos >= 0 && results.size() - uFirst < 500; iPos = sLower.Find("<a", iPos + 1)) {
		const char cNext = sPage[iPos + 2];
		if (cNext != ' ' && cNext != '\t' && cNext != '\r' && cNext != '\n')
			continue;	// <abbr>, <area>, ...
		const int iTagEnd = sPage.Find('>', iPos);
		if (iTagEnd < 0)
			break;
		const CStringA sHref(Href(sPage.Mid(iPos, iTagEnd - iPos)));
		if (sHref.IsEmpty())
			continue;
		int iClose = sLower.Find("</a", iTagEnd);
		if (iClose < 0 || iClose - iTagEnd > 4096)
			iClose = iTagEnd + 1;
		const CString sText(FromUtf8(PlainText(sPage.Mid(iTagEnd + 1, iClose - iTagEnd - 1))).Trim());

		SWebSearchResult r;
		r.size = 0;
		r.seeds = r.peers = -1;
		r.provider = sProvider;
		r.link = FromUtf8(sHref).Trim();
		r.text = sText;
		if (_tcsnicmp(r.link, _T("ed2k://|file|"), 13) == 0) {
			// ed2k://|file|<name>|<size>|<hash>|...
			int iField = 13;
			const CString sName(r.link.Tokenize(_T("|"), iField));
			const CString sSize(iField >= 0 ? r.link.Tokenize(_T("|"), iField) : CString());
			r.title = CString(OptUtf8ToStr(URLDecode(sName)));
			r.size = _tcstoui64(sSize, NULL, 10);
		} else if (_tcsnicmp(r.link, _T("magnet:?"), 8) == 0) {
			if (!CTorrentQueue::IsTorrentLink(r.link))
				continue;
			const int iName = r.link.Find(_T("dn="));
			if (iName >= 0) {
				CString sName(r.link.Mid(iName + 3));
				const int iEnd = sName.Find(_T('&'));
				if (iEnd >= 0)
					sName.Truncate(iEnd);
				sName.Replace(_T('+'), _T(' '));
				r.title = CString(OptUtf8ToStr(URLDecode(sName)));
			}
		} else {
			r.link = Absolute(r.link, sPageUrl);
			if (!CWebDownloadQueue::IsWebUrl(r.link))
				continue;
			if (!IsTorrentAddress(r.link) && !CWebDownloadQueue::LooksLikeFileUrl(r.link)) {
				if (pOtherPages != NULL && !sText.IsEmpty() && pOtherPages->size() < 5000 && HostOf(r.link) == HostOf(sPageUrl)) {
					r.title = sText;
					pOtherPages->push_back(r);
				}
				continue;
			}
			r.title = LastSegment(r.link);
		}
		if (r.title.IsEmpty())
			r.title = sText.IsEmpty() ? r.link : sText;
		bool bKnown = false;
		for (size_t i = uFirst; i < results.size() && !bKnown; ++i)
			bKnown = results[i].link == r.link;
		if (!bKnown)
			results.push_back(r);
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
	// all the words, in any order
	bool HasWords(CString sText, const std::vector<CString> &words)
	{
		sText.MakeLower();
		for (const CString &sWord : words)
			if (sText.Find(sWord) < 0)
				return false;
		return !words.empty();
	}

	// Many sites do not link their downloads from the list but from one page for each thing
	// listed. The pages whose name holds the words are opened, a few at most, and their
	// download links taken, each under the name of its page.
	void OpenPages(const std::vector<SWebSearchResult> &pages, const std::vector<CString> &words, const CString &sProvider
		, std::vector<SWebSearchResult> &found, CString &sError)
	{
		int iOpened = 0;
		int iLeft = 0;
		std::vector<CString> seen;
		for (const SWebSearchResult &page : pages) {
			if (!HasWords(page.title, words) || std::find(seen.begin(), seen.end(), page.link) != seen.end())
				continue;
			seen.push_back(page.link);
			// A page which is not opened, cannot be read or shows no download - it may be behind
			// another site, or a step only a person can take - is listed itself: the user opens it
			// in his browser and goes on from there.
			SWebSearchResult self(page);
			self.page = true;
			if (iOpened >= MAX_PAGES) {
				++iLeft;
				found.push_back(self);
				continue;
			}
			++iOpened;
			std::vector<BYTE> data;
			CString sIgnored;
			std::vector<SWebSearchResult> inside;
			if (PegasoHttpGet(page.link, data, sIgnored, MAX_ANSWER))
				CWebSearch::ParsePage(data, page.link, sProvider, inside);
			if (inside.empty())
				found.push_back(self);
			for (SWebSearchResult &r : inside) {
				if (CWebDownloadQueue::IsWebUrl(r.link))
					r.title = page.title + _T(" - ") + (r.text.IsEmpty() ? r.title : r.text);
				found.push_back(r);
			}
		}
		if (iLeft > 0)
			sError.Format(GetPegasoString(PS_WS_MORE_PAGES), iLeft);
	}

	void SearchWorker(std::shared_ptr<SWebSearchJob> pJob, SSearchProvider provider, CString sQuery)
	{
		CString sUrl(provider.url);
		const bool bPage = provider.IsPage();
		if (bPage)
			sUrl.Replace(_T("%s"), EncodeQueryValue(sQuery));
		else {
			sUrl += (sUrl.Find(_T('?')) < 0) ? _T('?') : _T('&');
			sUrl += _T("t=search&q=") + EncodeQueryValue(sQuery);
			if (!provider.apiKey.IsEmpty())
				sUrl += _T("&apikey=") + EncodeQueryValue(provider.apiKey);
		}

		std::vector<CString> words;
		{
			CString sLower(sQuery);
			sLower.MakeLower();
			for (int iPos = 0; iPos >= 0;) {
				const CString sWord(sLower.Tokenize(_T(" \t"), iPos));
				if (!sWord.IsEmpty())
					words.push_back(sWord);
			}
		}

		std::vector<BYTE> data;
		CString sError;
		std::vector<SWebSearchResult> found;
		if (PegasoHttpGet(sUrl, data, sError, MAX_ANSWER)) {
			if (bPage) {
				// the page of results of the site; if it only leads to other pages, those are opened
				std::vector<SWebSearchResult> pages;
				CWebSearch::ParsePage(data, sUrl, provider.name, found, &pages);
				if (found.empty())
					OpenPages(pages, words, provider.name, found, sError);
			} else {
				CWebSearch::ParseResults(data, provider.name, found);
				bool bHtml = false;
				if (found.empty()) {
					CStringA sHead((LPCSTR)data.data(), (int)min(data.size(), (size_t)4096));
					sHead.MakeLower();
					bHtml = sHead.Find("<html") >= 0 || sHead.Find("<!doctype html") >= 0;
				}
				// An ordinary web page where a feed was expected: a site without a search of its
				// own. The page is taken as a catalogue: what it lists is searched for the words.
				if (bHtml) {
					data.clear();
					if (PegasoHttpGet(provider.url, data, sError, MAX_ANSWER)) {
						std::vector<SWebSearchResult> all, pages;
						CWebSearch::ParsePage(data, provider.url, provider.name, all, &pages);
						for (const SWebSearchResult &r : all)
							if (HasWords(r.title + _T(' ') + r.text, words))
								found.push_back(r);
						OpenPages(pages, words, provider.name, found, sError);
						if (found.empty() && sError.IsEmpty())
							sError = GetPegasoString(PS_WS_NOT_FEED);	// "0 results" alone explains nothing
					}
				}
			}
		}

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
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_WS_LIST, OnLvnItemChangedList)
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
	FillSites();

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
	if (!m_sInitialQuery.IsEmpty()) {
		SetDlgItemText(IDC_WS_QUERY, m_sInitialQuery);
		PostMessage(WM_COMMAND, IDOK);
	}
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
	std::vector<SSearchProvider> providers(CWebSearch::LoadProviders());
	if (providers.empty()) {
		SetDlgItemText(IDC_WS_STATUS, GetPegasoString(PS_WS_NO_PROVIDERS));
		return;
	}
	// one site alone, if the user chose one
	const int iSite = static_cast<CComboBox*>(GetDlgItem(IDC_WS_SITE))->GetCurSel();
	if (iSite > 0 && (size_t)iSite <= providers.size()) {
		const SSearchProvider only(providers[(size_t)iSite - 1]);
		providers.assign(1, only);
		theApp.WriteProfileString(_T("eMule"), _T("WebSearchSite"), EncodeUrlUtf8(only.name));
	} else
		theApp.WriteProfileString(_T("eMule"), _T("WebSearchSite"), _T(""));

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
	// best seeded first, the pages to open by hand last
	std::stable_sort(m_results.begin(), m_results.end(), [](const SWebSearchResult &a, const SWebSearchResult &b) { return a.page != b.page ? b.page : a.seeds > b.seeds; });
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	for (size_t i = 0; i < m_results.size(); ++i) {
		const SWebSearchResult &r = m_results[i];
		const int iItem = m_list.InsertItem((int)i, r.title);
		m_list.SetItemText(iItem, 1, r.page ? (LPCTSTR)GetPegasoString(PS_WS_PAGE) : r.size ? (LPCTSTR)CastItoXBytes(r.size) : _T(""));
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
	SetDlgItemText(IDC_WS_DOWNLOAD, GetPegasoString(PS_WS_DOWNLOAD));
}

// the button says what it will do with the result chosen
void CWebSearchDlg::OnLvnItemChangedList(LPNMHDR, LRESULT *pResult)
{
	const int iSel = m_list.GetNextItem(-1, LVNI_SELECTED);
	const bool bPage = iSel >= 0 && (size_t)iSel < m_results.size() && m_results[(size_t)iSel].page;
	SetDlgItemText(IDC_WS_DOWNLOAD, GetPegasoString(bPage ? PS_WS_OPEN_PAGE : PS_WS_DOWNLOAD));
	*pResult = 0;
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
	if (r.page) {	// a page of the site: the user goes on in his browser
		if (CWebDownloadQueue::IsWebUrl(r.link)) {
			::ShellExecute(NULL, _T("open"), r.link, NULL, NULL, SW_SHOWNORMAL);
			SetDlgItemText(IDC_WS_STATUS, GetPegasoString(PS_WS_PAGE_OPENED));
		}
		return;
	}
	if (_tcsnicmp(r.link, _T("magnet:?"), 8) == 0) {
		if (theTorrents.AddLink(r.link))
			SetDlgItemText(IDC_WS_STATUS, r.title);
		return;
	}
	if (_tcsnicmp(r.link, _T("ed2k://|file|"), 13) == 0) {
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(r.link.Right(1) == _T("/") ? r.link : r.link + _T('/'));
			if (pLink != NULL && pLink->GetKind() == CED2KLink::kFile) {
				theApp.downloadqueue->AddFileLinkToDownload(*pLink->GetFileLink(), 0);
				SetDlgItemText(IDC_WS_STATUS, r.title);
			}
		} catch (const CString &sError) {
			SetDlgItemText(IDC_WS_STATUS, r.title + _T("  -  ") + sError);
		}
		delete pLink;
		return;
	}
	if (!IsTorrentAddress(r.link)) {	// a file on the web
		theWebDownloads.AddUrl(r.link, 0);
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

// The sites to search in: all of them, or one.
void CWebSearchDlg::FillSites()
{
	CComboBox *pCombo = static_cast<CComboBox*>(GetDlgItem(IDC_WS_SITE));
	pCombo->ResetContent();
	pCombo->AddString(GetPegasoString(PS_WS_ALL_SITES));
	const CString sLast(OptUtf8ToStr(URLDecode(theApp.GetProfileString(_T("eMule"), _T("WebSearchSite"), _T("")))));
	int iSel = 0;
	for (const SSearchProvider &provider : CWebSearch::LoadProviders()) {
		const int i = pCombo->AddString(provider.name);
		if (!sLast.IsEmpty() && provider.name.CompareNoCase(sLast) == 0)
			iSel = i;
	}
	pCombo->SetCurSel(iSel);
}

void CWebSearchDlg::OnBnClickedProviders()
{
	CSearchProvidersDlg dlg(this);
	dlg.DoModal();
	FillSites();
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
