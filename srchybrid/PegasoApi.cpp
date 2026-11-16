// Pegaso: the local API (REST and MCP), see PegasoApi.h
#include "stdafx.h"
#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <bcrypt.h>
#include "emule.h"
#include "emuleDlg.h"
#include "PegasoApi.h"
#include "PegasoStrings.h"
#include "PegasoTrust.h"
#include "PartFile.h"
#include "KnownFile.h"
#include "DownloadQueue.h"
#include "UploadQueue.h"
#include "SharedFileList.h"
#include "SearchDlg.h"
#include "SearchParams.h"
#include "SearchList.h"
#include "SearchFile.h"
#include "ServerConnect.h"
#include "Server.h"
#include "ED2KLink.h"
#include "WebDownload.h"
#include "TorrentQueue.h"
#include "Exceptions.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Log.h"
#include "Opcodes.h"
#include "Version.h"
#include "MenuCmds.h"
#include "kademlia/kademlia/Kademlia.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#pragma comment(lib, "bcrypt.lib")

CPegasoApi thePegasoApi;

namespace
{
	const UINT WM_PEGASO_API = WM_APP + 0x51;
	const size_t MAX_HEADERS = 16 * 1024;
	const size_t MAX_BODY = 1024 * 1024;

	///////////////////////////////////////////////////////////////////////////////////////
	// JSON, as much of it as the API needs

	struct JVal
	{
		enum EType { Null, Bool, Num, Str, Arr, Obj } type = Null;
		bool	b = false;
		double	n = 0;
		std::string s;	// UTF-8
		std::vector<JVal> a;
		std::vector<std::pair<std::string, JVal> > o;

		const JVal* Get(const char *pszKey) const
		{
			for (const auto &member : o)
				if (member.first == pszKey)
					return &member.second;
			return NULL;
		}
		std::string GetStr(const char *pszKey) const
		{
			const JVal *p = Get(pszKey);
			return p != NULL && p->type == Str ? p->s : std::string();
		}
	};

	class CJsonParser
	{
	public:
		explicit CJsonParser(const std::string &sText) : m_s(sText), m_i(0) {}

		bool Parse(JVal &rOut)
		{
			return Value(rOut, 0) && (Skip(), m_i == m_s.size());
		}

	private:
		void Skip()
		{
			while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t' || m_s[m_i] == '\r' || m_s[m_i] == '\n'))
				++m_i;
		}

		bool Literal(const char *psz)
		{
			const size_t uLen = strlen(psz);
			if (m_s.compare(m_i, uLen, psz) != 0)
				return false;
			m_i += uLen;
			return true;
		}

		static void AppendUtf8(std::string &rs, unsigned uCode)
		{
			if (uCode < 0x80)
				rs += (char)uCode;
			else if (uCode < 0x800) {
				rs += (char)(0xC0 | (uCode >> 6));
				rs += (char)(0x80 | (uCode & 0x3F));
			} else if (uCode < 0x10000) {
				rs += (char)(0xE0 | (uCode >> 12));
				rs += (char)(0x80 | ((uCode >> 6) & 0x3F));
				rs += (char)(0x80 | (uCode & 0x3F));
			} else {
				rs += (char)(0xF0 | (uCode >> 18));
				rs += (char)(0x80 | ((uCode >> 12) & 0x3F));
				rs += (char)(0x80 | ((uCode >> 6) & 0x3F));
				rs += (char)(0x80 | (uCode & 0x3F));
			}
		}

		bool Hex4(unsigned &ruCode)
		{
			if (m_i + 4 > m_s.size())
				return false;
			ruCode = 0;
			for (int k = 0; k < 4; ++k) {
				const char c = m_s[m_i++];
				ruCode <<= 4;
				if (c >= '0' && c <= '9')
					ruCode |= (unsigned)(c - '0');
				else if (c >= 'a' && c <= 'f')
					ruCode |= (unsigned)(c - 'a' + 10);
				else if (c >= 'A' && c <= 'F')
					ruCode |= (unsigned)(c - 'A' + 10);
				else
					return false;
			}
			return true;
		}

		bool String(std::string &rs)
		{
			if (m_i >= m_s.size() || m_s[m_i] != '"')
				return false;
			for (++m_i; m_i < m_s.size();) {
				const char c = m_s[m_i++];
				if (c == '"')
					return true;
				if ((unsigned char)c < 0x20)
					return false;
				if (c != '\\') {
					rs += c;
					continue;
				}
				if (m_i >= m_s.size())
					return false;
				switch (m_s[m_i++]) {
				case '"': rs += '"'; break;
				case '\\': rs += '\\'; break;
				case '/': rs += '/'; break;
				case 'b': rs += '\b'; break;
				case 'f': rs += '\f'; break;
				case 'n': rs += '\n'; break;
				case 'r': rs += '\r'; break;
				case 't': rs += '\t'; break;
				case 'u':
					{
						unsigned uCode, uLow;
						if (!Hex4(uCode))
							return false;
						if (uCode >= 0xD800 && uCode < 0xDC00) {	// first half of a pair
							if (m_s.compare(m_i, 2, "\\u") != 0)
								return false;
							m_i += 2;
							if (!Hex4(uLow) || uLow < 0xDC00 || uLow > 0xDFFF)
								return false;
							uCode = 0x10000 + ((uCode - 0xD800) << 10) + (uLow - 0xDC00);
						}
						AppendUtf8(rs, uCode);
					}
					break;
				default:
					return false;
				}
			}
			return false;
		}

		bool Value(JVal &rOut, int iDepth)
		{
			if (iDepth > 32)
				return false;
			Skip();
			if (m_i >= m_s.size())
				return false;
			const char c = m_s[m_i];
			if (c == '{') {
				rOut.type = JVal::Obj;
				++m_i;
				Skip();
				if (m_i < m_s.size() && m_s[m_i] == '}') {
					++m_i;
					return true;
				}
				for (;;) {
					Skip();
					std::string sKey;
					if (!String(sKey))
						return false;
					Skip();
					if (m_i >= m_s.size() || m_s[m_i++] != ':')
						return false;
					rOut.o.emplace_back(sKey, JVal());
					if (!Value(rOut.o.back().second, iDepth + 1))
						return false;
					Skip();
					if (m_i >= m_s.size())
						return false;
					if (m_s[m_i] == ',') {
						++m_i;
						continue;
					}
					return m_s[m_i++] == '}';
				}
			}
			if (c == '[') {
				rOut.type = JVal::Arr;
				++m_i;
				Skip();
				if (m_i < m_s.size() && m_s[m_i] == ']') {
					++m_i;
					return true;
				}
				for (;;) {
					rOut.a.emplace_back();
					if (!Value(rOut.a.back(), iDepth + 1))
						return false;
					Skip();
					if (m_i >= m_s.size())
						return false;
					if (m_s[m_i] == ',') {
						++m_i;
						continue;
					}
					return m_s[m_i++] == ']';
				}
			}
			if (c == '"') {
				rOut.type = JVal::Str;
				return String(rOut.s);
			}
			if (Literal("true")) {
				rOut.type = JVal::Bool;
				rOut.b = true;
				return true;
			}
			if (Literal("false")) {
				rOut.type = JVal::Bool;
				return true;
			}
			if (Literal("null"))
				return true;
			const size_t uStart = m_i;
			while (m_i < m_s.size() && (isdigit((unsigned char)m_s[m_i]) || strchr("+-.eE", m_s[m_i]) != NULL))
				++m_i;
			if (m_i == uStart)
				return false;
			rOut.type = JVal::Num;
			rOut.n = atof(m_s.substr(uStart, m_i - uStart).c_str());
			return true;
		}

		const std::string &m_s;
		size_t m_i;
	};

	std::string Utf8(const CString &s)
	{
		const CStringW w(s);
		if (w.IsEmpty())
			return std::string();
		const int iLen = ::WideCharToMultiByte(CP_UTF8, 0, w, w.GetLength(), NULL, 0, NULL, NULL);
		std::string out((size_t)iLen, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w, w.GetLength(), &out[0], iLen, NULL, NULL);
		return out;
	}

	CString FromUtf8(const std::string &s)
	{
		if (s.empty())
			return CString();
		const int iLen = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
		CStringW w;
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.GetBuffer(iLen), iLen);
		w.ReleaseBuffer(iLen);
		return CString(w);
	}

	std::string Quote(const std::string &sUtf8)
	{
		std::string out("\"");
		for (const char c : sUtf8) {
			switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ((unsigned char)c < 0x20) {
					char sz[8];
					sprintf_s(sz, "\\u%04x", (unsigned)(unsigned char)c);
					out += sz;
				} else
					out += c;
			}
		}
		return out + '"';
	}

	std::string Quote(const CString &s)	{ return Quote(Utf8(s)); }

	std::string Number(uint64 u)
	{
		char sz[32];
		sprintf_s(sz, "%llu", (unsigned long long)u);
		return sz;
	}

	class CObj
	{
	public:
		CObj& Raw(const char *pszKey, const std::string &sJson)
		{
			if (!m_s.empty())
				m_s += ',';
			m_s += Quote(std::string(pszKey)) + ':' + sJson;
			return *this;
		}
		CObj& Str(const char *pszKey, const CString &s)		{ return Raw(pszKey, Quote(s)); }
		CObj& Num(const char *pszKey, uint64 u)				{ return Raw(pszKey, Number(u)); }
		CObj& Bool(const char *pszKey, bool b)				{ return Raw(pszKey, b ? "true" : "false"); }
		std::string Done() const							{ return '{' + m_s + '}'; }

	private:
		std::string m_s;
	};

	std::string Array(const std::vector<std::string> &items)
	{
		std::string out("[");
		for (size_t i = 0; i < items.size(); ++i)
			out += (i ? "," : "") + items[i];
		return out + ']';
	}

	std::string Scalar(const JVal &v)	// a JSON-RPC id back as it came
	{
		switch (v.type) {
		case JVal::Str:
			return Quote(v.s);
		case JVal::Num:
			{
				char sz[40];
				if (v.n == (double)(long long)v.n)
					sprintf_s(sz, "%lld", (long long)v.n);
				else
					sprintf_s(sz, "%.17g", v.n);
				return sz;
			}
		case JVal::Bool:
			return v.b ? "true" : "false";
		default:
			return "null";
		}
	}

	///////////////////////////////////////////////////////////////////////////////////////
	// What the API does. Main thread only.

	struct SApiError
	{
		int		status;
		CString	message;
	};

	std::set<uint32> s_searches;	// started through the API

	CPartFile* DownloadByHash(const std::string &sHash)
	{
		uchar hash[MDX_DIGEST_SIZE];
		if (sHash.size() != 32 || !strmd4(sHash.c_str(), hash))
			throw SApiError{400, _T("the hash has to be 32 hexadecimal digits")};
		CPartFile *pFile = theApp.downloadqueue->GetFileByID(hash);
		if (pFile == NULL)
			throw SApiError{404, _T("no download with that hash")};
		return pFile;
	}

	std::string TrustJson(const SPegasoTrust &trust)
	{
		std::vector<std::string> reasons;
		for (int iPos = 0; iPos >= 0;) {
			const CString sLine(trust.reasons.Tokenize(_T("\n"), iPos));
			if (!sLine.IsEmpty())
				reasons.push_back(Quote(sLine));
		}
		return CObj().Num("score", (uint64)trust.score).Str("label", PegasoTrustLabel(trust)).Raw("reasons", Array(reasons)).Done();
	}

	std::string StatusJson()
	{
		CObj ed2k;
		const bool bServer = theApp.serverconnect != NULL && theApp.serverconnect->IsConnected();
		ed2k.Bool("connected", bServer);
		if (bServer) {
			ed2k.Bool("lowId", theApp.serverconnect->IsLowID());
			const CServer *pServer = theApp.serverconnect->GetCurrentServer();
			if (pServer != NULL)
				ed2k.Str("server", pServer->GetListName()).Str("address", pServer->GetAddress());
		}
		CObj kad;
		kad.Bool("connected", Kademlia::CKademlia::IsConnected());
		if (Kademlia::CKademlia::IsConnected())
			kad.Bool("firewalled", Kademlia::CKademlia::IsFirewalled());
		return CObj().Str("name", PEGASO_NAME).Str("version", PEGASO_VERSION_NAME)
			.Raw("ed2k", ed2k.Done()).Raw("kad", kad.Done())
			.Num("downloadRate", theApp.downloadqueue->GetDatarate())
			.Num("uploadRate", theApp.uploadqueue->GetDatarate())
			.Num("downloads", (uint64)theApp.downloadqueue->GetFileCount())
			.Num("sharedFiles", (uint64)theApp.sharedfiles->GetCount())
			.Done();
	}

	std::string DownloadJson(CPartFile *pFile)
	{
		const EPartFileStatus status = pFile->GetStatus();
		return CObj().Str("hash", md4str(pFile->GetFileHash())).Str("name", pFile->GetFileName())
			.Str("kind", pFile->IsTorrent() ? _T("torrent") : pFile->IsWebFile() ? _T("web") : _T("ed2k"))
			.Str("origin", pFile->GetOriginText())
			.Num("size", (uint64)pFile->GetFileSize()).Num("completed", (uint64)pFile->GetCompletedSize())
			.Num("rate", pFile->GetDatarate()).Num("sources", pFile->IsTorrent() ? pFile->GetTorrentPeers() : pFile->GetSourceCount())
			.Str("status", pFile->getPartfileStatus())
			.Bool("paused", status == PS_PAUSED).Bool("complete", status == PS_COMPLETE)
			.Raw("trust", TrustJson(PegasoTrustOfDownload(pFile)))
			.Done();
	}

	std::string DownloadsJson()
	{
		std::vector<std::string> items;
		for (POSITION pos = NULL;;) {
			CPartFile *pFile = theApp.downloadqueue->GetFileNext(pos);
			if (pFile == NULL)
				break;
			items.push_back(DownloadJson(pFile));
			if (pos == NULL)
				break;
		}
		return CObj().Raw("downloads", Array(items)).Done();
	}

	// the web address of a .torrent file, as far as the address tells
	bool IsTorrentAddress(const CString &sUrl)
	{
		CString s(sUrl);
		const int iFragment = s.Find(_T('#'));
		if (iFragment >= 0)
			s.Truncate(iFragment);
		if (s.Right(8).CompareNoCase(_T(".torrent")) == 0)
			return true;
		const int iQuery = s.Find(_T('?'));
		return iQuery >= 0 && s.Left(iQuery).Right(8).CompareNoCase(_T(".torrent")) == 0;
	}

	// A .torrent file behind a web address: fetched and handed to the BitTorrent engine.
	void AddTorrentAddress(const CString &sUrl)
	{
		std::vector<BYTE> data;
		CString sError;
		if (!PegasoHttpGet(sUrl, data, sError, 16 * 1024 * 1024))
			throw sError;
		if (data.empty() || data[0] != 'd')
			throwCStr(_T("not a torrent file"));
		static UINT s_uCount;
		const CString sDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("torrents\\"));
		::CreateDirectory(sDir, NULL);
		CString sPath;
		sPath.Format(_T("%sapi-%u.torrent"), (LPCTSTR)sDir, ++s_uCount % 16);
		FILE *f = _tfsopen(sPath, _T("wb"), _SH_DENYWR);
		if (f == NULL)
			throwCStr(_T("the torrent file cannot be written"));
		const bool bWritten = fwrite(data.data(), 1, data.size(), f) == data.size();
		fclose(f);
		if (!bWritten || !theTorrents.AddLink(sPath))
			throwCStr(_T("the torrent file cannot be opened"));
	}

	// ed2k links, magnet links, paths of .torrent files, web addresses.
	// bTorrents: the web addresses are those of .torrent files, whatever they look like.
	std::string AddLinks(const std::vector<CString> &links, bool bTorrents = false)
	{
		if (links.empty())
			throw SApiError{400, _T("no link given")};
		UINT uAdded = 0;
		std::vector<std::string> errors;
		for (CString sLink : links) {
			sLink.Trim();
			if (sLink.IsEmpty())
				continue;
			if (theTorrents.AddLink(sLink) ) {
				++uAdded;
				continue;
			}
			if (CWebDownloadQueue::IsWebUrl(sLink) && (bTorrents || IsTorrentAddress(sLink))) {
				try {
					AddTorrentAddress(sLink);
					++uAdded;
				} catch (const CString &sError) {
					errors.push_back(CObj().Str("link", sLink).Str("error", sError).Done());
				}
				continue;
			}
			if (CWebDownloadQueue::IsWebUrl(sLink)) {
				theWebDownloads.AddUrl(sLink, 0);
				++uAdded;
				continue;
			}
			CED2KLink *pLink = NULL;
			try {
				pLink = CED2KLink::CreateLinkFromUrl(sLink[sLink.GetLength() - 1] == _T('/') ? sLink : sLink + _T('/'));
				if (pLink == NULL || pLink->GetKind() != CED2KLink::kFile)
					throwCStr(_T("not a link to a file"));
				theApp.downloadqueue->AddFileLinkToDownload(*pLink->GetFileLink(), 0);
				++uAdded;
			} catch (const CString &sError) {
				errors.push_back(CObj().Str("link", sLink).Str("error", sError).Done());
			}
			delete pLink;
		}
		return CObj().Num("added", uAdded).Raw("errors", Array(errors)).Done();
	}

	std::string DownloadAction(const std::string &sHash, const std::string &sAction)
	{
		CPartFile *pFile = DownloadByHash(sHash);
		if (sAction == "pause") {
			if (pFile->IsTorrent())
				theTorrents.Pause(pFile->GetTorrentId());
			else if (pFile->CanPauseFile())
				pFile->PauseFile();
		} else if (sAction == "resume") {
			if (pFile->IsTorrent())
				theTorrents.Resume(pFile->GetTorrentId());
			else if (pFile->CanResumeFile())
				pFile->ResumeFile();
		} else if (sAction == "remove") {
			// the download and what it has received so far; a finished file stays on disk
			if (pFile->IsTorrent())
				theTorrents.Remove(pFile->GetTorrentId(), pFile->GetStatus() != PS_COMPLETE);
			else if (pFile->IsPartFile() && pFile->GetStatus() != PS_COMPLETING)
				pFile->DeletePartFile();
			else
				throw SApiError{409, _T("the download is being completed")};
			return CObj().Bool("removed", true).Done();
		} else
			throw SApiError{404, _T("unknown action")};
		return DownloadJson(pFile);
	}

	std::string SharedJson()
	{
		const size_t MAX_LISTED = 1000;
		std::vector<std::string> items;
		CKnownFilesMap shared;
		theApp.sharedfiles->CopySharedFileMap(shared);
		for (POSITION pos = shared.GetStartPosition(); pos != NULL && items.size() < MAX_LISTED;) {
			CCKey key;
			CKnownFile *pFile;
			shared.GetNextAssoc(pos, key, pFile);
			if (pFile != NULL)
				items.push_back(CObj().Str("hash", md4str(pFile->GetFileHash())).Str("name", pFile->GetFileName())
					.Num("size", (uint64)pFile->GetFileSize()).Str("path", pFile->GetFilePath()).Str("link", pFile->GetED2kLink()).Done());
		}
		return CObj().Num("total", (uint64)theApp.sharedfiles->GetCount()).Raw("files", Array(items)).Done();
	}

	std::string StartSearch(const std::string &sQuery, const std::string &sNetwork, const std::string &sType)
	{
		CString sExpression(FromUtf8(sQuery));
		sExpression.Trim();
		if (sExpression.IsEmpty())
			throw SApiError{400, _T("no query given")};
		static const struct { const char *name; LPCTSTR type; } types[] = {
			{"video", _T(ED2KFTSTR_VIDEO)}, {"audio", _T(ED2KFTSTR_AUDIO)}, {"image", _T(ED2KFTSTR_IMAGE)},
			{"document", _T(ED2KFTSTR_DOCUMENT)}, {"program", _T(ED2KFTSTR_PROGRAM)}, {"archive", _T(ED2KFTSTR_ARCHIVE)},
			{"cdimage", _T(ED2KFTSTR_CDIMAGE)}
		};
		SSearchParams *pParams = new SSearchParams;
		pParams->strExpression = sExpression;
		for (const auto &type : types)
			if (sType == type.name)
				pParams->strFileType = type.type;
		const bool bKad = sNetwork == "kad" || (sNetwork.empty() && !(theApp.serverconnect != NULL && theApp.serverconnect->IsConnected()));
		pParams->eType = bKad ? SearchTypeKademlia : sNetwork == "global" ? SearchTypeEd2kGlobal : SearchTypeEd2kServer;
		bool bStarted = false;
		CString sError;
		try {
			bStarted = bKad ? theApp.emuledlg->searchwnd->DoNewKadSearch(pParams) : theApp.emuledlg->searchwnd->DoNewEd2kSearch(pParams);
		} catch (CMsgBoxException *ex) {
			sError = ex->m_strMsg;
			ex->Delete();
		} catch (CException *ex) {
			ex->Delete();
		} catch (const CString &s) {
			sError = s;
		}
		if (!bStarted) {
			delete pParams;
			if (sError.IsEmpty())
				sError = bKad ? _T("Kad is not connected, or the query is not valid for Kad") : _T("not connected to a server, or the query is not valid");
			throw SApiError{409, sError};
		}
		s_searches.insert(pParams->dwSearchID);
		return CObj().Num("id", pParams->dwSearchID).Str("network", bKad ? _T("kad") : pParams->eType == SearchTypeEd2kGlobal ? _T("global") : _T("server")).Done();
	}

	const SearchList* SearchById(uint32 uId)
	{
		if (!s_searches.count(uId))
			throw SApiError{404, _T("no search with that id (searches are started with POST /api/v1/searches)")};
		return theApp.searchlist->PegasoListOf(uId);
	}

	std::string SearchResults(uint32 uId)
	{
		const SearchList *pList = SearchById(uId);
		const size_t MAX_LISTED = 500;
		std::vector<std::string> items;
		UINT uTotal = 0;
		for (POSITION pos = pList ? pList->GetHeadPosition() : NULL; pos != NULL;) {
			const CSearchFile *pFile = pList->GetNext(pos);
			if (pFile->GetListParent() != NULL)
				continue;	// another name of a file already listed
			++uTotal;
			if (items.size() >= MAX_LISTED)
				continue;
			items.push_back(CObj().Str("hash", md4str(pFile->GetFileHash())).Str("name", pFile->GetFileName())
				.Num("size", (uint64)pFile->GetFileSize()).Num("sources", pFile->GetSourceCount())
				.Num("completeSources", pFile->GetCompleteSourceCount()).Str("type", pFile->GetFileType())
				.Str("link", pFile->GetED2kLink())
				.Bool("known", theApp.downloadqueue->IsFileExisting(pFile->GetFileHash(), false))
				.Raw("trust", TrustJson(PegasoTrustOfSearchFile(pFile)))
				.Done());
		}
		return CObj().Num("id", uId).Num("total", uTotal).Raw("results", Array(items)).Done();
	}

	std::string DownloadResult(uint32 uId, const std::string &sHash)
	{
		const SearchList *pList = SearchById(uId);
		uchar hash[MDX_DIGEST_SIZE];
		if (sHash.size() != 32 || !strmd4(sHash.c_str(), hash))
			throw SApiError{400, _T("the hash has to be 32 hexadecimal digits")};
		for (POSITION pos = pList ? pList->GetHeadPosition() : NULL; pos != NULL;) {
			CSearchFile *pFile = pList->GetNext(pos);
			if (pFile->GetListParent() == NULL && md4equ(pFile->GetFileHash(), hash)) {
				theApp.downloadqueue->AddSearchToDownload(pFile, thePrefs.AddNewFilesPaused(), 0);
				CPartFile *pDownload = theApp.downloadqueue->GetFileByID(hash);
				if (pDownload == NULL)
					throw SApiError{409, _T("the file could not be added (already shared or downloaded?)")};
				return DownloadJson(pDownload);
			}
		}
		throw SApiError{404, _T("the search has no result with that hash")};
	}

	std::string Connect(bool bConnect)
	{
		// the Connect button of the toolbar: it connects, or disconnects (also a connection attempt)
		const bool bActive = theApp.IsConnected() || (theApp.serverconnect != NULL && theApp.serverconnect->IsConnecting())
			|| Kademlia::CKademlia::IsRunning();
		if (bConnect != bActive)
			theApp.emuledlg->SendMessage(WM_COMMAND, MP_HM_CON);
		return StatusJson();
	}

	///////////////////////////////////////////////////////////////////////////////////////
	// MCP: the same things as tools, over JSON-RPC

	struct STool
	{
		const char *name;
		const char *description;
		const char *schema;	// JSON Schema of the arguments
		std::function<std::string(const JVal&)> call;
	};

	uint32 IdArg(const JVal &args)
	{
		const JVal *p = args.Get("search_id");
		if (p == NULL || p->type != JVal::Num)
			throw SApiError{400, _T("search_id is missing")};
		return (uint32)p->n;
	}

	const std::vector<STool>& Tools()
	{
		static const std::vector<STool> tools = {
			{"pegaso_status", "Connection state of Pegaso (eD2K server, Kad), transfer rates and counts.",
				"{\"type\":\"object\",\"properties\":{}}",
				[](const JVal&) { return StatusJson(); }},
			{"pegaso_list_downloads", "The download list: name, size, progress, rate, sources, origin (E eMule, T torrent, W web) and the trust score of the fake detector.",
				"{\"type\":\"object\",\"properties\":{}}",
				[](const JVal&) { return DownloadsJson(); }},
			{"pegaso_add_download", "Adds downloads from links: ed2k:// links, magnet links, web addresses (http/https) or the path of a .torrent file on this computer.",
				"{\"type\":\"object\",\"properties\":{\"links\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"One or more links\"}},\"required\":[\"links\"]}",
				[](const JVal &args) {
					std::vector<CString> links;
					const JVal *p = args.Get("links");
					if (p != NULL && p->type == JVal::Arr)
						for (const JVal &v : p->a)
							links.push_back(FromUtf8(v.s));
					else if (p != NULL && p->type == JVal::Str)
						links.push_back(FromUtf8(p->s));
					return AddLinks(links);
				}},
			{"pegaso_pause_download", "Pauses a download.",
				"{\"type\":\"object\",\"properties\":{\"hash\":{\"type\":\"string\",\"description\":\"Hash of the download, as listed\"}},\"required\":[\"hash\"]}",
				[](const JVal &args) { return DownloadAction(args.GetStr("hash"), "pause"); }},
			{"pegaso_resume_download", "Resumes a paused download.",
				"{\"type\":\"object\",\"properties\":{\"hash\":{\"type\":\"string\"}},\"required\":[\"hash\"]}",
				[](const JVal &args) { return DownloadAction(args.GetStr("hash"), "resume"); }},
			{"pegaso_remove_download", "Removes a download from the list. Data received so far for an unfinished download is deleted; a finished file stays on disk. Ask the user before removing.",
				"{\"type\":\"object\",\"properties\":{\"hash\":{\"type\":\"string\"}},\"required\":[\"hash\"]}",
				[](const JVal &args) { return DownloadAction(args.GetStr("hash"), "remove"); }},
			{"pegaso_list_shared", "The files Pegaso shares, with their ed2k links (up to 1000).",
				"{\"type\":\"object\",\"properties\":{}}",
				[](const JVal&) { return SharedJson(); }},
			{"pegaso_search", "Starts a search on the eD2K server or on Kad. Returns a search id; results arrive over the next seconds and are read with pegaso_search_results.",
				"{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},"
				"\"network\":{\"type\":\"string\",\"enum\":[\"server\",\"global\",\"kad\"],\"description\":\"Default: the server if connected, else Kad\"},"
				"\"type\":{\"type\":\"string\",\"enum\":[\"video\",\"audio\",\"image\",\"document\",\"program\",\"archive\",\"cdimage\"]}},\"required\":[\"query\"]}",
				[](const JVal &args) { return StartSearch(args.GetStr("query"), args.GetStr("network"), args.GetStr("type")); }},
			{"pegaso_search_results", "Results of a search so far: name, size, sources, ed2k link and the trust score of the fake detector (0 nothing against it, 100 certainly not what the name says).",
				"{\"type\":\"object\",\"properties\":{\"search_id\":{\"type\":\"integer\"}},\"required\":[\"search_id\"]}",
				[](const JVal &args) { return SearchResults(IdArg(args)); }},
			{"pegaso_download_result", "Downloads one result of a search.",
				"{\"type\":\"object\",\"properties\":{\"search_id\":{\"type\":\"integer\"},\"hash\":{\"type\":\"string\"}},\"required\":[\"search_id\",\"hash\"]}",
				[](const JVal &args) { return DownloadResult(IdArg(args), args.GetStr("hash")); }},
			{"pegaso_connect", "Connects Pegaso to an eD2K server and to Kad.",
				"{\"type\":\"object\",\"properties\":{}}",
				[](const JVal&) { return Connect(true); }},
			{"pegaso_disconnect", "Disconnects Pegaso from the eD2K server and from Kad.",
				"{\"type\":\"object\",\"properties\":{}}",
				[](const JVal&) { return Connect(false); }},
		};
		return tools;
	}

	std::string RpcError(const std::string &sId, int iCode, const std::string &sMessage)
	{
		return "{\"jsonrpc\":\"2.0\",\"id\":" + sId + ",\"error\":{\"code\":" + std::to_string(iCode) + ",\"message\":" + Quote(sMessage) + "}}";
	}

	// One JSON-RPC message. Empty result: a notification, nothing to answer.
	std::string McpMessage(const JVal &msg)
	{
		const JVal *pId = msg.Get("id");
		const std::string sId(pId != NULL ? Scalar(*pId) : "null");
		const std::string sMethod(msg.GetStr("method"));
		if (msg.type != JVal::Obj || sMethod.empty())
			return pId != NULL || msg.type != JVal::Obj ? RpcError(sId, -32600, "not a JSON-RPC request") : std::string();
		if (pId == NULL)
			return std::string();	// notifications/initialized and the like
		static const JVal empty;
		const JVal *pParams = msg.Get("params");
		const JVal &params = pParams != NULL ? *pParams : empty;
		std::string sResult;
		if (sMethod == "initialize") {
			// the version the client speaks, if it is one this server knows
			std::string sVersion(params.GetStr("protocolVersion"));
			if (sVersion != "2025-06-18" && sVersion != "2025-03-26" && sVersion != "2024-11-05")
				sVersion = "2025-06-18";
			sResult = CObj().Raw("protocolVersion", Quote(sVersion)).Raw("capabilities", "{\"tools\":{}}")
				.Raw("serverInfo", CObj().Str("name", _T("pegaso")).Str("version", PEGASO_VERSION_NAME).Done())
				.Raw("instructions", Quote(std::string("Pegaso is a P2P client (eD2K, Kad, BitTorrent, web downloads). Search results and downloads carry a trust score from its fake detector; mention it when it is 25 or more.")))
				.Done();
		} else if (sMethod == "ping")
			sResult = "{}";
		else if (sMethod == "tools/list") {
			std::vector<std::string> items;
			for (const STool &tool : Tools())
				items.push_back(CObj().Raw("name", Quote(std::string(tool.name))).Raw("description", Quote(std::string(tool.description)))
					.Raw("inputSchema", tool.schema).Done());
			sResult = CObj().Raw("tools", Array(items)).Done();
		} else if (sMethod == "tools/call") {
			const std::string sName(params.GetStr("name"));
			const JVal *pArgs = params.Get("arguments");
			const STool *pTool = NULL;
			for (const STool &tool : Tools())
				if (sName == tool.name)
					pTool = &tool;
			if (pTool == NULL)
				return RpcError(sId, -32602, "unknown tool: " + sName);
			std::string sText;
			bool bError = false;
			try {
				sText = pTool->call(pArgs != NULL ? *pArgs : empty);
			} catch (const SApiError &error) {
				sText = Utf8(error.message);
				bError = true;
			}
			sResult = CObj().Raw("content", "[" + CObj().Raw("type", "\"text\"").Raw("text", Quote(sText)).Done() + "]").Bool("isError", bError).Done();
		} else
			return RpcError(sId, -32601, "method not found: " + sMethod);
		return "{\"jsonrpc\":\"2.0\",\"id\":" + sId + ",\"result\":" + sResult + "}";
	}

	///////////////////////////////////////////////////////////////////////////////////////
	// Requests, on the main thread

	struct SRequest
	{
		std::string method;
		std::string path;	// without the query
		std::string body;
		int			status = 200;
		std::string reply;
	};

	JVal Body(const SRequest &req)
	{
		JVal v;
		if (!req.body.empty() && !CJsonParser(req.body).Parse(v))
			throw SApiError{400, _T("the body is not valid JSON")};
		return v;
	}

	void Handle(SRequest &req)
	{
		// path split at the slashes
		std::vector<std::string> parts;
		for (size_t i = 0; i < req.path.size();) {
			const size_t uNext = req.path.find('/', i);
			const std::string sPart(req.path.substr(i, uNext == std::string::npos ? std::string::npos : uNext - i));
			if (!sPart.empty())
				parts.push_back(sPart);
			if (uNext == std::string::npos)
				break;
			i = uNext + 1;
		}
		const bool bGet = req.method == "GET", bPost = req.method == "POST";

		if (parts.size() == 1 && parts[0] == "mcp") {
			if (!bPost)
				throw SApiError{405, _T("the MCP endpoint takes POST")};
			JVal msg;
			if (!CJsonParser(req.body).Parse(msg)) {
				req.reply = RpcError("null", -32700, "parse error");
				return;
			}
			if (msg.type == JVal::Arr) {
				std::vector<std::string> answers;
				for (const JVal &one : msg.a) {
					const std::string sAnswer(McpMessage(one));
					if (!sAnswer.empty())
						answers.push_back(sAnswer);
				}
				req.reply = Array(answers);
				if (answers.empty())
					req.status = 202;
			} else {
				req.reply = McpMessage(msg);
				if (req.reply.empty())
					req.status = 202;
			}
			return;
		}

		if (parts.size() < 3 || parts[0] != "api" || parts[1] != "v1")
			throw SApiError{404, _T("unknown address; the API lives under /api/v1 and MCP under /mcp")};
		const std::string &sWhat = parts[2];
		const size_t n = parts.size();
		if (sWhat == "status" && n == 3 && bGet)
			req.reply = StatusJson();
		else if (sWhat == "downloads" && n == 3 && bGet)
			req.reply = DownloadsJson();
		else if (sWhat == "downloads" && n == 3 && bPost) {
			const JVal body(Body(req));
			std::vector<CString> links;
			const JVal *pLinks = body.Get("links");
			if (pLinks != NULL && pLinks->type == JVal::Arr)
				for (const JVal &v : pLinks->a)
					links.push_back(FromUtf8(v.s));
			if (!body.GetStr("link").empty())
				links.push_back(FromUtf8(body.GetStr("link")));
			req.reply = AddLinks(links, body.GetStr("kind") == "torrent");
		} else if (sWhat == "downloads" && n == 4 && bGet)
			req.reply = DownloadJson(DownloadByHash(parts[3]));
		else if (sWhat == "downloads" && n == 4 && req.method == "DELETE")
			req.reply = DownloadAction(parts[3], "remove");
		else if (sWhat == "downloads" && n == 5 && bPost && (parts[4] == "pause" || parts[4] == "resume"))
			req.reply = DownloadAction(parts[3], parts[4]);
		else if (sWhat == "shared" && n == 3 && bGet)
			req.reply = SharedJson();
		else if (sWhat == "searches" && n == 3 && bPost) {
			const JVal body(Body(req));
			req.reply = StartSearch(body.GetStr("query"), body.GetStr("network"), body.GetStr("type"));
		} else if (sWhat == "searches" && n == 4 && bGet)
			req.reply = SearchResults((uint32)strtoul(parts[3].c_str(), NULL, 10));
		else if (sWhat == "searches" && n == 5 && bPost && parts[4] == "download")
			req.reply = DownloadResult((uint32)strtoul(parts[3].c_str(), NULL, 10), Body(req).GetStr("hash"));
		else if ((sWhat == "connect" || sWhat == "disconnect") && n == 3 && bPost)
			req.reply = Connect(sWhat == "connect");
		else
			throw SApiError{404, _T("unknown address or method")};
	}

	std::atomic<bool> s_bStopping;

	LRESULT CALLBACK ApiWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		if (uMsg != WM_PEGASO_API)
			return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
		SRequest &req = *reinterpret_cast<SRequest*>(lParam);
		if (s_bStopping || !theApp.IsRunning() || theApp.downloadqueue == NULL) {
			req.status = 503;
			req.reply = "{\"error\":\"Pegaso is starting or closing\"}";
			return 1;
		}
		try {
			Handle(req);
		} catch (const SApiError &error) {
			req.status = error.status;
			req.reply = CObj().Str("error", error.message).Done();
		} catch (CException *ex) {
			ex->Delete();
			req.status = 500;
			req.reply = "{\"error\":\"internal error\"}";
		} catch (...) {
			req.status = 500;
			req.reply = "{\"error\":\"internal error\"}";
		}
		return 1;
	}

	///////////////////////////////////////////////////////////////////////////////////////
	// The HTTP server, on its own thread

	SOCKET				s_listen = INVALID_SOCKET;
	HWND				s_hWnd;
	std::thread			s_thread;
	std::atomic<bool>	s_bThreadDone;
	std::string			s_token;
	UINT				s_uPort;

	const char* Reason(int iStatus)
	{
		switch (iStatus) {
		case 200: return "OK";
		case 202: return "Accepted";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 409: return "Conflict";
		case 413: return "Payload Too Large";
		case 503: return "Service Unavailable";
		default: return "Internal Server Error";
		}
	}

	void Send(SOCKET s, int iStatus, const std::string &sBody, const char *pszExtra = "")
	{
		char szHead[400];
		sprintf_s(szHead, "HTTP/1.1 %d %s\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: %u\r\n"
			"Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n%s\r\n"
			, iStatus, Reason(iStatus), (unsigned)sBody.size(), pszExtra);
		std::string out(szHead);
		out += sBody;
		for (size_t uSent = 0; uSent < out.size();) {
			const int iSent = send(s, out.data() + uSent, (int)min(out.size() - uSent, (size_t)65536), 0);
			if (iSent <= 0)
				break;
			uSent += (size_t)iSent;
		}
	}

	std::string Lower(std::string s)
	{
		for (char &c : s)
			c = (char)tolower((unsigned char)c);
		return s;
	}

	// "127.0.0.1", "localhost" or "[::1]", with or without a port
	bool IsLocalName(std::string sHost)
	{
		sHost = Lower(sHost);
		if (!sHost.empty() && sHost[0] == '[') {
			const size_t uEnd = sHost.find(']');
			return uEnd != std::string::npos && sHost.substr(0, uEnd + 1) == "[::1]";
		}
		const size_t uColon = sHost.find(':');
		if (uColon != std::string::npos)
			sHost.erase(uColon);
		return sHost == "127.0.0.1" || sHost == "localhost";
	}

	bool SameToken(const std::string &a, const std::string &b)
	{
		unsigned char diff = (unsigned char)(a.size() != b.size());
		for (size_t i = 0; i < a.size() && i < b.size(); ++i)
			diff |= (unsigned char)(a[i] ^ b[i]);
		return diff == 0 && !b.empty();
	}

	void Serve(SOCKET s)
	{
		DWORD dwTimeout = 5000;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&dwTimeout, sizeof dwTimeout);
		setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&dwTimeout, sizeof dwTimeout);
		std::string data;
		size_t uHeadEnd = std::string::npos;
		char buf[8192];
		while (uHeadEnd == std::string::npos && data.size() <= MAX_HEADERS) {
			const int iRead = recv(s, buf, sizeof buf, 0);
			if (iRead <= 0)
				return;
			data.append(buf, (size_t)iRead);
			uHeadEnd = data.find("\r\n\r\n");
		}
		if (uHeadEnd == std::string::npos) {
			Send(s, 400, "{\"error\":\"headers too long\"}");
			return;
		}
		// request line
		const size_t uLineEnd = data.find("\r\n");
		const std::string sLine(data.substr(0, uLineEnd));
		const size_t uSp1 = sLine.find(' '), uSp2 = sLine.rfind(' ');
		if (uSp1 == std::string::npos || uSp2 <= uSp1) {
			Send(s, 400, "{\"error\":\"bad request\"}");
			return;
		}
		SRequest req;
		req.method = sLine.substr(0, uSp1);
		req.path = sLine.substr(uSp1 + 1, uSp2 - uSp1 - 1);
		const size_t uQuery = req.path.find('?');
		if (uQuery != std::string::npos)
			req.path.erase(uQuery);
		// headers
		std::map<std::string, std::string> headers;
		for (size_t i = uLineEnd + 2; i < uHeadEnd;) {
			size_t uEnd = data.find("\r\n", i);
			if (uEnd == std::string::npos || uEnd > uHeadEnd)
				uEnd = uHeadEnd;
			const std::string sHeader(data.substr(i, uEnd - i));
			const size_t uColon = sHeader.find(':');
			if (uColon != std::string::npos) {
				size_t uValue = uColon + 1;
				while (uValue < sHeader.size() && sHeader[uValue] == ' ')
					++uValue;
				headers[Lower(sHeader.substr(0, uColon))] = sHeader.substr(uValue);
			}
			i = uEnd + 2;
		}

		// only this computer, never a web page
		if (!IsLocalName(headers["host"])) {
			Send(s, 403, "{\"error\":\"this API only answers to this computer\"}");
			return;
		}
		const std::string sOrigin(headers["origin"]);
		if (!sOrigin.empty()) {
			const size_t uScheme = sOrigin.find("://");
			// A page of a web site is turned away. An extension of the browser of this computer is
			// not a web site: no page can give itself that origin, and it needs the key like anyone.
			const std::string sScheme(Lower(sOrigin.substr(0, uScheme == std::string::npos ? 0 : uScheme)));
			const bool bExtension = sScheme == "chrome-extension" || sScheme == "moz-extension";
			if (uScheme == std::string::npos || (!bExtension && !IsLocalName(sOrigin.substr(uScheme + 3)))) {
				Send(s, 403, "{\"error\":\"requests from web pages are not accepted\"}");
				return;
			}
		}
		const std::string sAuth(headers["authorization"]);
		if (sAuth.size() < 8 || Lower(sAuth.substr(0, 7)) != "bearer " || !SameToken(sAuth.substr(7), s_token)) {
			Send(s, 401, "{\"error\":\"the token is missing or wrong (Authorization: Bearer <token>)\"}", "WWW-Authenticate: Bearer\r\n");
			return;
		}

		// body
		const size_t uLength = (size_t)strtoul(headers["content-length"].c_str(), NULL, 10);
		if (uLength > MAX_BODY) {
			Send(s, 413, "{\"error\":\"body too long\"}");
			return;
		}
		req.body = data.substr(uHeadEnd + 4);
		while (req.body.size() < uLength) {
			const int iRead = recv(s, buf, (int)min(sizeof buf, uLength - req.body.size()), 0);
			if (iRead <= 0)
				return;
			req.body.append(buf, (size_t)iRead);
		}
		req.body.resize(min(req.body.size(), uLength));

		DWORD_PTR dwResult = 0;
		if (s_bStopping || !::SendMessageTimeout(s_hWnd, WM_PEGASO_API, 0, (LPARAM)&req, SMTO_NORMAL, 30000, &dwResult) || dwResult != 1) {
			Send(s, 503, "{\"error\":\"Pegaso is busy or closing\"}");
			return;
		}
		Send(s, req.status, req.reply);
	}

	void ServerThread()
	{
		while (!s_bStopping) {
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(s_listen, &fds);
			timeval tv = {0, 300000};
			const int iReady = select(0, &fds, NULL, NULL, &tv);
			if (iReady < 0)
				break;
			if (iReady == 0)
				continue;
			const SOCKET s = accept(s_listen, NULL, NULL);
			if (s == INVALID_SOCKET)
				continue;
			Serve(s);
			shutdown(s, SD_SEND);
			closesocket(s);
		}
		s_bThreadDone = true;
	}
}

bool CPegasoApi::IsEnabledInPrefs()
{
	return theApp.GetProfileInt(_T("eMule"), _T("ApiEnabled"), 0) != 0;
}

UINT CPegasoApi::PortInPrefs()
{
	const UINT uPort = theApp.GetProfileInt(_T("eMule"), _T("ApiPort"), 4780);
	return uPort > 0 && uPort < 65536 ? uPort : 4780;
}

CString CPegasoApi::TokenInPrefs()
{
	CString sToken(theApp.GetProfileString(_T("eMule"), _T("ApiToken"), _T("")));
	if (sToken.GetLength() < 32) {
		sToken.Empty();
		for (int i = 0; i < 8; ++i) {
			unsigned u = 0;
			if (!BCRYPT_SUCCESS(::BCryptGenRandom(NULL, (PUCHAR)&u, sizeof u, BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
				AfxThrowUserException();	// no random numbers, no token
			sToken.AppendFormat(_T("%08x"), u);
		}
		theApp.WriteProfileString(_T("eMule"), _T("ApiToken"), sToken);
	}
	return sToken;
}

bool CPegasoApi::IsRunning() const
{
	return s_listen != INVALID_SOCKET;
}

void CPegasoApi::Start()
{
	if (IsRunning() || !IsEnabledInPrefs())
		return;
	s_uPort = PortInPrefs();
	s_token = std::string(CStringA(TokenInPrefs()));

	static const TCHAR szClass[] = _T("PegasoApiWindow");
	WNDCLASS wc = {};
	wc.lpfnWndProc = ApiWndProc;
	wc.hInstance = AfxGetInstanceHandle();
	wc.lpszClassName = szClass;
	::RegisterClass(&wc);	// fails harmlessly the second time
	if (s_hWnd == NULL)
		s_hWnd = ::CreateWindowEx(0, szClass, _T(""), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, AfxGetInstanceHandle(), NULL);

	const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);	// this computer only
	addr.sin_port = htons((u_short)s_uPort);
	BOOL bExclusive = TRUE;
	if (s != INVALID_SOCKET)
		setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&bExclusive, sizeof bExclusive);
	if (s_hWnd == NULL || s == INVALID_SOCKET || bind(s, (sockaddr*)&addr, sizeof addr) != 0 || listen(s, 8) != 0) {
		if (s != INVALID_SOCKET)
			closesocket(s);
		LogWarning(LOG_STATUSBAR, GetPegasoString(PS_API_FAILED), s_uPort);
		return;
	}
	s_listen = s;
	s_bStopping = false;
	s_bThreadDone = false;
	s_thread = std::thread(ServerThread);
	AddLogLine(false, _T("Local API listening on http://127.0.0.1:%u (REST under /api/v1, MCP under /mcp)"), s_uPort);
}

void CPegasoApi::Stop()
{
	if (!IsRunning())
		return;
	s_bStopping = true;
	// a request may be waiting for this thread: let it through, it is answered "closing"
	for (int i = 0; i < 100 && !s_bThreadDone; ++i) {
		MSG msg;
		::PeekMessage(&msg, s_hWnd, 0, 0, PM_NOREMOVE);
		::Sleep(50);
	}
	if (s_bThreadDone)
		s_thread.join();
	else
		s_thread.detach();
	closesocket(s_listen);
	s_listen = INVALID_SOCKET;
}

void CPegasoApi::ShowDialog(CWnd*)
{
	if (IsRunning()) {
		const CString sToken(TokenInPrefs());
		CopyTextToClipboard(sToken);
		CString sText;
		sText.Format(GetPegasoString(PS_API_ON), s_uPort, s_uPort, (LPCTSTR)sToken);
		sText += _T("\n\n");
		sText += GetPegasoString(PS_API_ASK_OFF);
		if (AfxMessageBox(sText, MB_YESNO | MB_DEFBUTTON2 | MB_ICONINFORMATION) == IDYES) {
			theApp.WriteProfileInt(_T("eMule"), _T("ApiEnabled"), 0);
			Stop();
		}
		return;
	}
	if (AfxMessageBox(GetPegasoString(PS_API_OFF), MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	theApp.WriteProfileInt(_T("eMule"), _T("ApiEnabled"), 1);
	Start();
	CString sText;
	if (IsRunning()) {
		const CString sToken(TokenInPrefs());
		CopyTextToClipboard(sToken);
		sText.Format(GetPegasoString(PS_API_ON), s_uPort, s_uPort, (LPCTSTR)sToken);
		AfxMessageBox(sText, MB_ICONINFORMATION);
	} else {
		sText.Format(GetPegasoString(PS_API_FAILED), PortInPrefs());
		AfxMessageBox(sText, MB_ICONWARNING);
	}
}
