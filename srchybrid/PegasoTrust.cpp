// Pegaso: the fake detector, see PegasoTrust.h
#include "stdafx.h"
#include <map>
#include <set>
#include <vector>
#include "emule.h"
#include "PegasoTrust.h"
#include "PegasoRating.h"
#include "PegasoStrings.h"
#include "AbstractFile.h"
#include "KnownFile.h"
#include "PartFile.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "UpDownClient.h"
#include "DownloadQueue.h"
#include "TorrentQueue.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Log.h"
#include "Opcodes.h"
#include "Ini2.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	// extensions, each between bars
	LPCTSTR const EXT_PROGRAM = _T("|exe|scr|com|bat|cmd|pif|lnk|vbs|vbe|js|jse|wsf|wsh|hta|msi|jar|ps1|cpl|");
	LPCTSTR const EXT_SCRIPT = _T("|scr|pif|lnk|vbs|vbe|js|jse|wsf|wsh|hta|ps1|cpl|");	// hardly ever shared for a good reason
	LPCTSTR const EXT_VIDEO = _T("|avi|mkv|mp4|m4v|mov|wmv|mpg|mpeg|divx|flv|webm|ts|vob|ogm|rmvb|3gp|asf|");
	LPCTSTR const EXT_AUDIO = _T("|mp3|flac|ogg|wma|m4a|aac|wav|opus|ape|");
	LPCTSTR const EXT_DOCUMENT = _T("|pdf|doc|docx|epub|txt|rtf|jpg|jpeg|png|gif|");
	LPCTSTR const EXT_ARCHIVE = _T("|zip|rar|7z|ace|gz|tar|");
	// the first bytes of these are always recognised by GetFileTypeEx
	LPCTSTR const EXT_SURE_HEADER = _T("|avi|mkv|mp4|m4v|wmv|asf|wma|zip|7z|pdf|png|gif|");
	// words of a video release
	LPCTSTR const s_apszRelease[] = {
		_T("dvdrip"), _T("bdrip"), _T("brrip"), _T("bluray"), _T("blu-ray"), _T("webrip"), _T("web-dl"), _T("hdtv"),
		_T("hdrip"), _T("1080p"), _T("720p"), _T("2160p"), _T("x264"), _T("x265"), _T("hevc"), _T("dvdscr"), _T("camrip")
	};
	// words which say nothing about what a file is
	LPCTSTR const NOISE = _T("|the|and|los|las|del|por|con|une|des|der|die|das|und|spanish|castellano|espanol|english|ingles|")
		_T("latino|dual|subs|sub|dvd|rip|ac3|aac|mp3|dts|www|com|net|org|emule|xvid|divx|h264|h265|480p|")
		_T("dvdrip|bdrip|brrip|bluray|webrip|hdtv|hdrip|1080p|720p|2160p|x264|x265|hevc|dvdscr|camrip|");

	bool IsIn(const CString &sExt, LPCTSTR pszList)
	{
		return !sExt.IsEmpty() && _tcsstr(pszList, _T('|') + sExt + _T('|')) != NULL;
	}

	// Lower-case extension without the dot; rsRest gets the name without it.
	CString SplitExt(const CString &sName, CString &rsRest)
	{
		CString s(sName);
		s.TrimRight(_T(" ._\t"));
		const int iDot = s.ReverseFind(_T('.'));
		if (iDot <= 0 || s.GetLength() - iDot > 12 || s.Find(_T(' '), iDot) >= 0) {
			rsRest = s;
			return CString();
		}
		CString sExt(s.Mid(iDot + 1));
		sExt.MakeLower();
		rsRest = s.Left(iDot);
		return sExt;
	}

	CString ExtOf(const CString &sName)
	{
		CString sRest;
		return SplitExt(sName, sRest);
	}

	bool HasReleaseWord(const CString &sLower)
	{
		for (LPCTSTR psz : s_apszRelease)
			if (sLower.Find(psz) >= 0)
				return true;
		// S01E02
		for (int i = 0; i + 5 < sLower.GetLength(); ++i)
			if (sLower[i] == _T('s') && _istdigit(sLower[i + 1]) && _istdigit(sLower[i + 2]) && sLower[i + 3] == _T('e')
				&& _istdigit(sLower[i + 4]) && _istdigit(sLower[i + 5]) && (i == 0 || !_istalnum(sLower[i - 1])))
			{
				return true;
			}
		return false;
	}

	struct SScore
	{
		int		score = 0;
		bool	confirmed = false;
		CString	reasons;

		void Add(int iPoints, const CString &sReason)
		{
			score += iPoints;
			if (!reasons.IsEmpty())
				reasons += _T('\n');
			reasons += sReason;
		}
	};

	CString Fmt(EPegasoString id, ...)
	{
		CString s;
		va_list args;
		va_start(args, id);
		s.FormatV(GetPegasoString(id), args);
		va_end(args);
		return s;
	}

	// ---- what the name alone tells. Returns true for a program in disguise.
	bool JudgeName(const CString &sName, uint64 uSize, SScore &r)
	{
		if (sName.Find((TCHAR)0x202E) >= 0 || sName.Find((TCHAR)0x202D) >= 0) {
			r.Add(85, GetPegasoString(PS_TRUST_R_RTL));
			return true;
		}
		CString sRest, sRest2;
		const CString sExt(SplitExt(sName, sRest));
		const CString sPrev(SplitExt(sRest, sRest2));
		CString sLower(sName);
		sLower.MakeLower();
		const bool bPrevIsContent = IsIn(sPrev, EXT_VIDEO) || IsIn(sPrev, EXT_AUDIO) || IsIn(sPrev, EXT_DOCUMENT);
		if (IsIn(sExt, EXT_PROGRAM)) {
			if (bPrevIsContent || IsIn(sPrev, EXT_ARCHIVE)) {
				r.Add(85, Fmt(PS_TRUST_R_DISGUISED, (LPCTSTR)sExt, (LPCTSTR)sPrev));
				return true;
			}
			if (HasReleaseWord(sLower)) {
				r.Add(55, Fmt(PS_TRUST_R_EXE_MEDIA, (LPCTSTR)sExt));
				return true;
			}
			if (IsIn(sExt, EXT_SCRIPT))
				r.Add(25, Fmt(PS_TRUST_R_SCRIPT, (LPCTSTR)sExt));
		} else if (IsIn(sExt, EXT_ARCHIVE)) {
			if (bPrevIsContent && !IsIn(sPrev, EXT_DOCUMENT))
				r.Add(25, Fmt(PS_TRUST_R_ARCHIVE_MEDIA, (LPCTSTR)sExt, (LPCTSTR)sPrev));
			else if (uSize > 0 && uSize < 5 * 1024 * 1024 && HasReleaseWord(sLower))
				r.Add(35, Fmt(PS_TRUST_R_ARCHIVE_SMALL, (LPCTSTR)CastItoXBytes(uSize)));
		} else if (IsIn(sExt, EXT_VIDEO) && uSize > 0 && uSize < 1024 * 1024)
			r.Add(35, Fmt(PS_TRUST_R_SMALL, (LPCTSTR)CastItoXBytes(uSize)));
		return false;
	}

	// ---- announced duration and bit rate against the size
	void JudgeMedia(const CAbstractFile *pFile, const CString &sName, SScore &r)
	{
		const uint64 uSize = (uint64)pFile->GetFileSize();
		const UINT uLength = pFile->GetIntTagValue(FT_MEDIA_LENGTH);	// seconds
		if (uLength == 0 || uSize == 0)
			return;
		const CString sExt(ExtOf(sName));
		const uint64 uKbps = uSize * 8 / uLength / 1000;
		if ((IsIn(sExt, EXT_VIDEO) && uLength >= 600 && uKbps < 40) || (IsIn(sExt, EXT_AUDIO) && uLength >= 60 && uKbps < 5)) {
			r.Add(40, Fmt(PS_TRUST_R_DURATION, (LPCTSTR)SecToTimeLength(uLength), (LPCTSTR)CastItoXBytes(uSize)));
			return;
		}
		const UINT uBitrate = pFile->GetIntTagValue(FT_MEDIA_BITRATE);	// kbit/s
		if (sExt == _T("mp3") && uBitrate > 0 && uLength >= 30 && (uKbps > 4ull * uBitrate + 64 || uKbps * 4 < uBitrate))
			r.Add(25, GetPegasoString(PS_TRUST_R_BITRATE));
	}

	// ---- the names one file circulates under
	struct SOtherName
	{
		CString	name;
		UINT	weight;	// how many sources use it
	};

	void Tokens(const CString &sName, std::set<CString> &rTokens, CString &rsSquashed)
	{
		CString sRest;
		SplitExt(sName, sRest);
		sRest.MakeLower();
		for (int iPos = 0; iPos >= 0;) {
			const CString sToken(sRest.Tokenize(_T(" ._-[](){}+,;'!&~\t"), iPos));
			if (sToken.IsEmpty())
				continue;
			rsSquashed += sToken;
			if (sToken.GetLength() < 3 || sToken.SpanIncluding(_T("0123456789")) == sToken || IsIn(sToken, NOISE))
				continue;
			rTokens.insert(sToken);
		}
	}

	LPCTSTR KindOfExt(const CString &sExt)
	{
		if (IsIn(sExt, EXT_PROGRAM))
			return _T("program");
		if (IsIn(sExt, EXT_VIDEO))
			return _T("video");
		if (IsIn(sExt, EXT_AUDIO))
			return _T("audio");
		if (IsIn(sExt, EXT_ARCHIVE))
			return _T("archive");
		if (IsIn(sExt, EXT_DOCUMENT))
			return _T("document");
		return NULL;
	}

	void JudgeNames(const CString &sReference, const std::vector<SOtherName> &names, SScore &r)
	{
		if (names.empty())
			return;
		// kinds: a video for some, a program for others
		std::map<CString, UINT> kinds;	// kind -> weight
		std::map<CString, CString> kindExt;
		UINT uTotal = 0;
		auto addKind = [&](const CString &sName, UINT uWeight) {
			const CString sExt(ExtOf(sName));
			const LPCTSTR pszKind = KindOfExt(sExt);
			if (pszKind != NULL) {
				kinds[pszKind] += uWeight;
				kindExt[pszKind] = sExt;
				uTotal += uWeight;
			}
		};
		addKind(sReference, 1);
		for (const SOtherName &other : names)
			addKind(other.name, max(other.weight, 1u));
		CString sKinds;
		int iKinds = 0;
		for (const auto &kind : kinds)
			if (kind.second * 100 >= uTotal * 15) {	// a single odd source does not count
				++iKinds;
				sKinds.AppendFormat(_T("%s.%s"), sKinds.IsEmpty() ? _T("") : _T(", "), (LPCTSTR)kindExt[kind.first]);
			}
		if (iKinds >= 2)
			r.Add(kinds.count(_T("program")) ? 60 : 40, Fmt(PS_TRUST_R_KINDS, (LPCTSTR)sKinds));

		// unrelated names
		std::set<CString> ref;
		CString sRefSquashed;
		Tokens(sReference, ref, sRefSquashed);
		if (ref.empty())
			return;
		UINT uJudged = 1, uUnrelated = 0, uUnrelatedNames = 0, uBest = 0;
		CString sExample;
		for (const SOtherName &other : names) {
			std::set<CString> tokens;
			CString sSquashed;
			Tokens(other.name, tokens, sSquashed);
			if (tokens.empty())
				continue;
			bool bShared = false;
			for (const CString &sToken : tokens)
				if (ref.count(sToken) || (sToken.GetLength() >= 5 && sRefSquashed.Find(sToken) >= 0)) {
					bShared = true;
					break;
				}
			for (auto it = ref.begin(); !bShared && it != ref.end(); ++it)
				bShared = it->GetLength() >= 5 && sSquashed.Find(*it) >= 0;
			const UINT uWeight = max(other.weight, 1u);
			uJudged += uWeight;
			if (!bShared) {
				uUnrelated += uWeight;
				++uUnrelatedNames;
				if (uWeight > uBest) {
					uBest = uWeight;
					sExample = other.name;
				}
			}
		}
		if (uUnrelatedNames >= 2 && uUnrelated * 2 >= uJudged) {
			if (sExample.GetLength() > 60)
				sExample = sExample.Left(57) + _T("...");
			r.Add(35, Fmt(PS_TRUST_R_NAMES, (LPCTSTR)sExample));
		}
	}

	// ---- what the users say
	void JudgeVerdicts(const CAbstractFile *pFile, SScore &r)
	{
		SPegasoVerdictCount count(PegasoCountVerdicts(pFile));
		if (count.Total() == 0 && pFile->HasRating() && !pFile->IsPartFile()) {
			// the rating a server or a search gave, without the comments behind it
			if (pFile->UserRating() == 1)
				count.fake = 1;
			else if (pFile->UserRating() >= 3)
				count.ok = 1;
		}
		const UINT uNegative = count.Negative();
		if (uNegative > 0)
			r.Add(min(70u, count.fake * 30 + count.mismatch * 20), Fmt(PS_TRUST_R_USERS_BAD, uNegative));
		if (count.ok > 0) {
			r.Add(-(int)min(30u, count.ok * 10), Fmt(PS_TRUST_R_USERS_OK, count.ok));
			if (count.ok > uNegative)
				r.confirmed = true;
		}
	}

	// ---- the content of a download against its extension
	bool IsContainer(EFileType type)
	{
		return type == VIDEO_AVI || type == VIDEO_MPG || type == VIDEO_MP4 || type == VIDEO_MKV || type == VIDEO_OGG || type == WM;
	}

	void JudgeContent(CPartFile *pFile, EFileType content, SScore &r)
	{
		const CString sExt(ExtOf(pFile->GetFileName()));
		if (sExt.IsEmpty())
			return;
		const EFileType byExt = GetFileTypeEx(pFile, true, false, true);	// by the extension only
		if (content == FILETYPE_UNKNOWN) {
			if (IsIn(sExt, EXT_SURE_HEADER))
				r.Add(40, Fmt(PS_TRUST_R_CONTENT_UNKNOWN, (LPCTSTR)sExt));
			return;
		}
		if (content == byExt) {
			r.Add(-15, GetPegasoString(PS_TRUST_R_CONTENT_OK));
			r.confirmed = true;
			return;
		}
		if (content == FILETYPE_EXECUTABLE) {
			if (!IsIn(sExt, EXT_PROGRAM) && KindOfExt(sExt) != NULL)
				r.Add(90, Fmt(PS_TRUST_R_CONTENT_EXE, (LPCTSTR)sExt));
			return;
		}
		if (byExt == FILETYPE_UNKNOWN)
			return;	// an extension eMule knows nothing about (many formats are ZIP files inside)
		const CString sContent(GetFileTypeName(content));
		if (IsContainer(content) && IsContainer(byExt))
			r.Add(10, Fmt(PS_TRUST_R_CONTAINER, (LPCTSTR)sContent, (LPCTSTR)sExt));
		else
			r.Add(IsContainer(byExt) || byExt == AUDIO_MPEG ? 60 : 50, Fmt(PS_TRUST_R_CONTENT_OTHER, (LPCTSTR)sContent, (LPCTSTR)sExt));
	}

	SPegasoTrust Result(const SScore &r, bool bSpam)
	{
		SPegasoTrust trust;
		trust.score = max(0, min(100, r.score));
		trust.reasons = r.reasons;
		if (bSpam) {
			trust.spam = true;
			trust.score = max(trust.score, 90);
			if (!trust.reasons.IsEmpty())
				trust.reasons += _T('\n');
			trust.reasons += GetPegasoString(PS_TRUST_R_SPAM);
		}
		trust.confirmed = r.confirmed && trust.score < 25;
		return trust;
	}

	// ---- downloads: results are kept for a few seconds, what never changes for good
	struct SDownloadEntry
	{
		DWORD		dwTick = 0;
		bool		bValid = false;
		SPegasoTrust trust;
		bool		bHeaderDone = false;
		EFileType	content = FILETYPE_UNKNOWN;
		bool		bTorrentDone = false;
		SScore		torrent;
		bool		bWarned = false;
		uchar		hash[MDX_DIGEST_SIZE] = {};
	};
	std::map<const CPartFile*, SDownloadEntry> s_downloads;
	DWORD s_dwLastProcess;

	void JudgeTorrentFiles(CPartFile *pFile, SDownloadEntry &entry)
	{
		if (entry.bTorrentDone)
			return;
		const std::vector<STorrentFile> files(theTorrents.GetFiles(pFile->GetTorrentId()));
		if (files.empty())
			return;	// not known yet
		entry.bTorrentDone = true;
		CString sTorrent(pFile->GetFileName());
		sTorrent.MakeLower();
		int64_t iTotal = 0;
		for (const STorrentFile &file : files)
			iTotal += file.size;
		for (const STorrentFile &file : files) {
			CString sLeaf(file.path.c_str());
			sLeaf = sLeaf.Mid(sLeaf.ReverseFind(_T('\\')) + 1);
			sLeaf = sLeaf.Mid(sLeaf.ReverseFind(_T('/')) + 1);
			SScore one;
			bool bDisguised = JudgeName(sLeaf, 0, one);
			// a "video" which is mostly one program
			if (!bDisguised && IsIn(ExtOf(sLeaf), EXT_PROGRAM) && file.size * 2 >= iTotal && files.size() > 1
				&& (HasReleaseWord(sTorrent) || IsIn(ExtOf(pFile->GetFileName()), EXT_VIDEO)))
			{
				bDisguised = true;
				one.score = 55;
			}
			if (bDisguised && one.score > entry.torrent.score) {
				entry.torrent = SScore();
				entry.torrent.Add(one.score, Fmt(PS_TRUST_R_TORRENT_EXE, (LPCTSTR)sLeaf));
			}
		}
	}
}

SPegasoTrust PegasoTrustOfSearchFile(const CSearchFile *pFile, const CString &sName)
{
	if (pFile == NULL)
		return SPegasoTrust();
	if (pFile->GetListParent() != NULL)
		pFile = pFile->GetListParent();
	const bool bMain = sName.IsEmpty() || sName == pFile->GetFileName();
	if (bMain && pFile->m_iPegasoTrust >= 0) {
		SPegasoTrust trust;
		trust.score = pFile->m_iPegasoTrust & 0xff;
		trust.spam = (pFile->m_iPegasoTrust & 0x100) != 0;
		trust.confirmed = (pFile->m_iPegasoTrust & 0x200) != 0;
		trust.reasons = pFile->m_sPegasoTrustWhy;
		return trust;
	}

	const CString &sJudged(bMain ? pFile->GetFileName() : sName);
	SScore r;
	JudgeName(sJudged, (uint64)pFile->GetFileSize(), r);
	JudgeMedia(pFile, sJudged, r);

	// the other names of the same file
	bool bMultipleAICH = pFile->HasFoundMultipleAICH();
	std::vector<SOtherName> names;
	if (pFile->GetListChildCount() > 0 && theApp.searchlist != NULL) {
		const SearchList *list = theApp.searchlist->PegasoListOf(pFile->GetSearchID());
		for (POSITION pos = list ? list->GetHeadPosition() : NULL; pos != NULL;) {
			const CSearchFile *pChild = list->GetNext(pos);
			if (pChild->GetListParent() != pFile)
				continue;
			bMultipleAICH |= pChild->HasFoundMultipleAICH();
			if (pChild->GetFileName().CompareNoCase(sJudged) != 0)
				names.push_back(SOtherName{pChild->GetFileName(), pChild->GetListChildCount()});
		}
	}
	JudgeNames(sJudged, names, r);
	if (bMultipleAICH)
		r.Add(30, GetPegasoString(PS_TRUST_R_AICH));
	JudgeVerdicts(pFile, r);

	const SPegasoTrust trust(Result(r, thePrefs.IsSearchSpamFilterEnabled() && pFile->IsConsideredSpam()));
	if (bMain) {
		pFile->m_iPegasoTrust = trust.score | (trust.spam ? 0x100 : 0) | (trust.confirmed ? 0x200 : 0);
		pFile->m_sPegasoTrustWhy = trust.reasons;
	}
	return trust;
}

SPegasoTrust PegasoTrustOfDownload(CPartFile *pFile)
{
	if (pFile == NULL)
		return SPegasoTrust();
	const DWORD dwNow = ::GetTickCount();
	SDownloadEntry &entry = s_downloads[pFile];
	if (!md4equ(entry.hash, pFile->GetFileHash())) {	// another file at the address of one which is gone
		entry = SDownloadEntry();
		md4cpy(entry.hash, pFile->GetFileHash());
	}
	if (entry.bValid && dwNow - entry.dwTick < SEC2MS(5))
		return entry.trust;

	SScore r;
	JudgeName(pFile->GetFileName(), pFile->IsTorrent() ? 0 : (uint64)pFile->GetFileSize(), r);
	if (pFile->IsTorrent()) {
		JudgeTorrentFiles(pFile, entry);
		if (entry.torrent.score > r.score)
			r = entry.torrent;
	} else {
		JudgeMedia(pFile, pFile->GetFileName(), r);
		// the names the sources give to the file
		std::map<CString, UINT> counted;
		for (POSITION pos = pFile->srclist.GetHeadPosition(); pos != NULL;) {
			const CString &sName(pFile->srclist.GetNext(pos)->GetClientFilename());
			if (!sName.IsEmpty() && sName.CompareNoCase(pFile->GetFileName()) != 0)
				++counted[sName];
		}
		std::vector<SOtherName> names;
		for (const auto &name : counted)
			names.push_back(SOtherName{name.first, name.second});
		JudgeNames(pFile->GetFileName(), names, r);

		// the first bytes, once they are on disk
		if (!pFile->IsWebFile()) {
			if (!entry.bHeaderDone && pFile->GetCompletedSize() >= (uint64)16 && pFile->IsCompleteBDSafe(0, 16)) {
				entry.bHeaderDone = true;
				entry.content = GetFileTypeEx(pFile, false, true);
			}
			if (entry.bHeaderDone)
				JudgeContent(pFile, entry.content, r);
		}
	}
	JudgeVerdicts(pFile, r);

	entry.trust = Result(r, false);
	entry.dwTick = dwNow;
	entry.bValid = true;
	return entry.trust;
}

CString PegasoTrustLabel(const SPegasoTrust &trust)
{
	EPegasoString id;
	if (trust.spam)
		id = PS_TRUST_SPAM;
	else if (trust.score >= 75)
		id = PS_TRUST_FAKE;
	else if (trust.score >= 50)
		id = PS_TRUST_SUSPICIOUS;
	else if (trust.score >= 25)
		id = PS_TRUST_CAUTION;
	else
		id = trust.confirmed ? PS_TRUST_OK : PS_TRUST_NONE;
	CString sLabel(GetPegasoString(id));
	if (trust.score >= 25)
		sLabel.AppendFormat(_T(" (%d)"), trust.score);
	return sLabel;
}

COLORREF PegasoTrustColor(const SPegasoTrust &trust, COLORREF crDefault)
{
	if (trust.score >= 75)
		return RGB(200, 0, 0);
	if (trust.score >= 50)
		return RGB(215, 85, 0);
	if (trust.score >= 25)
		return RGB(160, 115, 0);
	return trust.confirmed ? RGB(0, 128, 0) : crDefault;
}

CString PegasoTrustTip(const SPegasoTrust &trust)
{
	if (trust.reasons.IsEmpty())
		return CString();
	CString sTip;
	sTip.Format(_T("%s: %s"), GetPegasoString(PS_TRUST_COL), (LPCTSTR)PegasoTrustLabel(trust));
	for (int iPos = 0; iPos >= 0;) {
		const CString sLine(trust.reasons.Tokenize(_T("\n"), iPos));
		if (!sLine.IsEmpty())
			sTip.AppendFormat(_T("\n  - %s"), (LPCTSTR)sLine);
	}
	return sTip;
}

void PegasoTrustProcess()
{
	const DWORD dwNow = ::GetTickCount();
	if (dwNow - s_dwLastProcess < SEC2MS(10) || theApp.downloadqueue == NULL || !theApp.IsRunning())
		return;
	s_dwLastProcess = dwNow;

	std::set<const CPartFile*> alive;
	for (POSITION pos = NULL;;) {
		CPartFile *pFile = theApp.downloadqueue->GetFileNext(pos);
		if (pFile == NULL)
			break;
		alive.insert(pFile);
		const SPegasoTrust trust(PegasoTrustOfDownload(pFile));
		SDownloadEntry &entry = s_downloads[pFile];
		if (trust.IsSuspicious() && !entry.bWarned) {
			entry.bWarned = true;
			CString sWhy(trust.reasons);
			sWhy.Replace(_T("\n"), _T("; "));
			LogWarning(LOG_STATUSBAR, _T("\"%s\": %s - %s"), (LPCTSTR)pFile->GetFileName(), (LPCTSTR)PegasoTrustLabel(trust), (LPCTSTR)sWhy);
		}
		if (pos == NULL)
			break;
	}
	// forget the downloads which are gone
	for (auto it = s_downloads.begin(); it != s_downloads.end();)
		if (alive.count(it->first))
			++it;
		else
			it = s_downloads.erase(it);
}

void PegasoPlaceColumn(LPCTSTR pszOrdersKey, LPCTSTR pszFlag, int iColumnCount, int iColumn, int iPosition)
{
	if (theApp.GetProfileInt(_T("ListControlSetup"), pszFlag, 0) != 0)
		return;
	theApp.WriteProfileInt(_T("ListControlSetup"), pszFlag, 1);
	std::vector<int> stored((size_t)iColumnCount, 0), order;
	std::vector<bool> seen((size_t)iColumnCount, false);
	CIni ini(thePrefs.GetConfigFile(), _T("ListControlSetup"));
	ini.SerGet(true, stored.data(), iColumnCount, pszOrdersKey);
	// display position -> column; the first one is always the name
	order.push_back(0);
	seen[0] = seen[(size_t)iColumn] = true;
	for (int i = 1; i < iColumnCount; ++i)	// the others keep their order; what is missing follows
		if (stored[(size_t)i] > 0 && stored[(size_t)i] < iColumnCount && !seen[(size_t)stored[(size_t)i]]) {
			seen[(size_t)stored[(size_t)i]] = true;
			order.push_back(stored[(size_t)i]);
		}
	for (int i = 1; i < iColumnCount; ++i)
		if (!seen[(size_t)i])
			order.push_back(i);
	order.insert(order.begin() + min((size_t)iPosition, order.size()), iColumn);
	ini.SerGet(false, order.data(), iColumnCount, pszOrdersKey);
}
