// Pegaso: videos published as a list of pieces. See PegasoVideo.h.
#include "stdafx.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include "emule.h"
#include "emuleDlg.h"
#include "PegasoVideo.h"
#include "PegasoStrings.h"
#include "PegasoAuto.h"
#include "Preferences.h"
#include "Opcodes.h"
#include "OtherFunctions.h"
#include "StringConversion.h"
#include "WebDownload.h"
#include "NetGuard.h"
#include "SharedFileList.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const size_t MAX_LIST = 8 * 1024 * 1024;		// a list or a page
	const size_t MAX_PIECE = 512 * 1024 * 1024;		// one piece (a list may name a whole file as one)
	const int MAX_PIECES = 200000;
	const UINT_PTR TIMER_POLL = 1;

	// ---------------------------------------------------------------- addresses and text

	// The address a list names, seen from the address of the list.
	CString Resolve(const CString &sBase, CString sRel)
	{
		sRel.Trim();
		const int iOwn = sRel.Find(_T("://"));
		if (iOwn > 0 && iOwn < 8)
			return sRel;
		const int iScheme = sBase.Find(_T("://"));
		if (iScheme < 0)
			return sRel;
		CString sPlain(sBase);
		const int iQuery = sPlain.FindOneOf(_T("?#"));
		if (iQuery >= 0)
			sPlain.Truncate(iQuery);
		const int iPath = sPlain.Find(_T('/'), iScheme + 3);
		const CString sOrigin(iPath < 0 ? sPlain : sPlain.Left(iPath));
		if (sRel.Left(2) == _T("//"))
			return sBase.Left(iScheme + 1) + sRel;
		if (sRel.Left(1) == _T("/"))
			return sOrigin + sRel;
		CString sDir(iPath < 0 ? sPlain + _T('/') : sPlain.Left(sPlain.ReverseFind(_T('/')) + 1));
		for (;;) {
			if (sRel.Left(2) == _T("./"))
				sRel.Delete(0, 2);
			else if (sRel.Left(3) == _T("../")) {
				sRel.Delete(0, 3);
				if (sDir.GetLength() > sOrigin.GetLength() + 1) {
					sDir.Truncate(sDir.GetLength() - 1);
					sDir.Truncate(sDir.ReverseFind(_T('/')) + 1);
				}
			} else
				break;
		}
		return sDir + sRel;
	}

	bool GetText(const CString &sUrl, CStringA &sText, CString &sError)
	{
		std::vector<BYTE> data;
		if (!PegasoHttpGet(sUrl, data, sError, MAX_LIST))
			return false;
		sText = CStringA(reinterpret_cast<LPCSTR>(data.data()), (int)data.size());
		if (sText.Left(3) == "\xEF\xBB\xBF")
			sText.Delete(0, 3);
		return true;
	}

	CString FromUtf8(const CStringA &s)
	{
		return CString(OptUtf8ToStr(s));
	}

	// KEY=VALUE,KEY="VA,LUE" as the lines of an HLS list have them
	std::map<CStringA, CStringA> Attributes(const CStringA &s)
	{
		std::map<CStringA, CStringA> out;
		int i = 0;
		const int n = s.GetLength();
		while (i < n) {
			const int iEq = s.Find('=', i);
			if (iEq < 0)
				break;
			CStringA sKey(s.Mid(i, iEq - i));
			sKey.Trim(" ,");
			CStringA sValue;
			i = iEq + 1;
			if (i < n && s[i] == '"') {
				const int iEnd = s.Find('"', i + 1);
				sValue = iEnd < 0 ? s.Mid(i + 1) : s.Mid(i + 1, iEnd - i - 1);
				i = iEnd < 0 ? n : iEnd + 1;
			} else {
				const int iEnd = s.Find(',', i);
				sValue = iEnd < 0 ? s.Mid(i) : s.Mid(i, iEnd - i);
				i = iEnd < 0 ? n : iEnd;
			}
			out[sKey.MakeUpper()] = sValue.Trim();
			while (i < n && (s[i] == ',' || s[i] == ' '))
				++i;
		}
		return out;
	}

	std::vector<CStringA> Lines(const CStringA &sText)
	{
		std::vector<CStringA> out;
		int iPos = 0;
		while (iPos >= 0 && iPos <= sText.GetLength()) {
			int iEnd = sText.Find('\n', iPos);
			CStringA sLine(iEnd < 0 ? sText.Mid(iPos) : sText.Mid(iPos, iEnd - iPos));
			sLine.Trim();
			if (!sLine.IsEmpty())
				out.push_back(sLine);
			if (iEnd < 0)
				break;
			iPos = iEnd + 1;
		}
		return out;
	}

	CString QualityLabel(int iHeight, int iBandwidth)
	{
		CString s;
		if (iHeight > 0 && iBandwidth > 0)
			s.Format(_T("%dp  (%.1f Mbit/s)"), iHeight, iBandwidth / 1000000.0);
		else if (iHeight > 0)
			s.Format(_T("%dp"), iHeight);
		else if (iBandwidth > 0)
			s.Format(_T("%.1f Mbit/s"), iBandwidth / 1000000.0);
		else
			s = _T("-");
		return s;
	}

	CString LanguageLabel(const CString &sName, const CString &sLang)
	{
		if (sName.IsEmpty())
			return sLang.IsEmpty() ? CString(_T("?")) : sLang;
		if (sLang.IsEmpty() || sName.CompareNoCase(sLang) == 0)
			return sName;
		return sName + _T("  (") + sLang + _T(")");
	}

	void SortBest(std::vector<SVideoTrack> &tracks)
	{
		std::stable_sort(tracks.begin(), tracks.end(), [](const SVideoTrack &a, const SVideoTrack &b) {
			return a.height != b.height ? a.height > b.height : a.bandwidth > b.bandwidth;
		});
	}

	// ---------------------------------------------------------------- HLS

	struct SHlsPieces
	{
		std::vector<CString> urls;	// the first of all (EXT-X-MAP) first, if there is one
		bool	hasMap = false;
		bool	live = true;		// until the end of the list is seen
		bool	encrypted = false;
		bool	ranges = false;		// pieces which are parts of one file: not taken
	};

	void ParseHlsPieces(const CStringA &sText, const CString &sBase, SHlsPieces &out)
	{
		for (const CStringA &sLine : Lines(sText)) {
			if (sLine[0] != '#') {
				if ((int)out.urls.size() < MAX_PIECES)
					out.urls.push_back(Resolve(sBase, FromUtf8(sLine)));
			} else if (sLine.Left(11) == "#EXT-X-KEY:") {
				const CStringA sMethod(Attributes(sLine.Mid(11))["METHOD"]);
				if (!sMethod.IsEmpty() && sMethod.CompareNoCase("NONE") != 0)
					out.encrypted = true;
			} else if (sLine.Left(11) == "#EXT-X-MAP:") {
				std::map<CStringA, CStringA> attrs(Attributes(sLine.Mid(11)));
				if (!attrs["BYTERANGE"].IsEmpty())
					out.ranges = true;
				if (!out.hasMap && !attrs["URI"].IsEmpty()) {
					out.urls.insert(out.urls.begin(), Resolve(sBase, FromUtf8(attrs["URI"])));
					out.hasMap = true;
				}
			} else if (sLine.Left(17) == "#EXT-X-BYTERANGE:")
				out.ranges = true;
			else if (sLine.Left(14) == "#EXT-X-ENDLIST")
				out.live = false;
			else if (sLine.Left(19) == "#EXT-X-SESSION-KEY:")
				out.encrypted = true;
		}
	}

	void ParseHls(const CStringA &sText, const CString &sUrl, SVideoInfo &info)
	{
		const std::vector<CStringA> lines(Lines(sText));
		bool bMaster = false;
		for (size_t i = 0; i < lines.size(); ++i) {
			const CStringA &sLine = lines[i];
			if (sLine.Left(13) == "#EXT-X-MEDIA:") {
				std::map<CStringA, CStringA> attrs(Attributes(sLine.Mid(13)));
				if (attrs["TYPE"].CompareNoCase("AUDIO") == 0 && !attrs["URI"].IsEmpty()) {
					SVideoTrack track;
					track.lang = FromUtf8(attrs["LANGUAGE"]);
					track.label = LanguageLabel(FromUtf8(attrs["NAME"]), track.lang);
					track.group = FromUtf8(attrs["GROUP-ID"]);
					track.url = Resolve(sUrl, FromUtf8(attrs["URI"]));
					info.audios.push_back(track);
				}
			} else if (sLine.Left(18) == "#EXT-X-STREAM-INF:") {
				bMaster = true;
				std::map<CStringA, CStringA> attrs(Attributes(sLine.Mid(18)));
				size_t j = i + 1;
				while (j < lines.size() && lines[j][0] == '#')
					++j;
				if (j >= lines.size())
					break;
				SVideoTrack track;
				track.bandwidth = atoi(attrs["BANDWIDTH"]);
				const CStringA sResolution(attrs["RESOLUTION"]);
				const int iX = sResolution.FindOneOf("xX");
				if (iX > 0)
					track.height = atoi(sResolution.Mid(iX + 1));
				track.group = FromUtf8(attrs["AUDIO"]);
				track.url = Resolve(sUrl, FromUtf8(lines[j]));
				track.label = QualityLabel(track.height, track.bandwidth);
				info.videos.push_back(track);
				i = j;
			}
		}
		if (!bMaster) {
			// the list of the pieces itself: one quality, the sound inside
			SHlsPieces pieces;
			ParseHlsPieces(sText, sUrl, pieces);
			if (pieces.urls.empty()) {
				info.notFound = true;
				return;
			}
			info.isProtected = pieces.encrypted;
			info.live = pieces.live;
			SVideoTrack track;
			track.url = sUrl;
			track.label = _T("-");
			info.videos.push_back(track);
			info.audios.clear();
		}
		SortBest(info.videos);
	}

	// ---------------------------------------------------------------- DASH

	struct SXmlTag
	{
		CStringA name;		// without its prefix
		std::map<CStringA, CStringA> attrs;
		CStringA text;		// what follows an opening tag, up to the next one
		bool	closing = false;
		bool	empty = false;	// <tag/>
	};

	// Tags in the order they come; enough for a list written by a program.
	std::vector<SXmlTag> ScanXml(const CStringA &sXml)
	{
		std::vector<SXmlTag> out;
		int i = 0;
		const int n = sXml.GetLength();
		while (i < n) {
			const int iOpen = sXml.Find('<', i);
			if (iOpen < 0)
				break;
			if (sXml.Mid(iOpen, 4) == "<!--") {
				const int iEnd = sXml.Find("-->", iOpen);
				i = iEnd < 0 ? n : iEnd + 3;
				continue;
			}
			const int iClose = sXml.Find('>', iOpen);
			if (iClose < 0)
				break;
			CStringA sInside(sXml.Mid(iOpen + 1, iClose - iOpen - 1));
			i = iClose + 1;
			if (sInside.IsEmpty() || sInside[0] == '?' || sInside[0] == '!')
				continue;
			SXmlTag tag;
			if (sInside[0] == '/') {
				tag.closing = true;
				sInside.Delete(0);
			}
			if (sInside.Right(1) == "/") {
				tag.empty = true;
				sInside.Truncate(sInside.GetLength() - 1);
			}
			int iName = sInside.FindOneOf(" \t\r\n");
			tag.name = iName < 0 ? sInside : sInside.Left(iName);
			const int iPrefix = tag.name.Find(':');
			if (iPrefix >= 0)
				tag.name.Delete(0, iPrefix + 1);
			// attributes: name="value" or name='value'
			int k = iName < 0 ? sInside.GetLength() : iName;
			const int m = sInside.GetLength();
			while (k < m) {
				const int iEq = sInside.Find('=', k);
				if (iEq < 0)
					break;
				CStringA sKey(sInside.Mid(k, iEq - k));
				sKey.Trim();
				int q = iEq + 1;
				while (q < m && (sInside[q] == ' ' || sInside[q] == '\t'))
					++q;
				if (q >= m || (sInside[q] != '"' && sInside[q] != '\''))
					break;
				const int iEnd = sInside.Find(sInside[q], q + 1);
				if (iEnd < 0)
					break;
				CStringA sValue(sInside.Mid(q + 1, iEnd - q - 1));
				sValue.Replace("&amp;", "&");
				tag.attrs[sKey] = sValue;
				k = iEnd + 1;
			}
			if (!tag.closing && !tag.empty) {
				const int iNext = sXml.Find('<', i);
				tag.text = iNext < 0 ? sXml.Mid(i) : sXml.Mid(i, iNext - i);
				tag.text.Trim();
				tag.text.Replace("&amp;", "&");
			}
			out.push_back(tag);
		}
		return out;
	}

	// "PT1H2M3.5S" in seconds
	double IsoSeconds(const CStringA &s)
	{
		double dTotal = 0;
		double dNumber = 0;
		double dScale = 0;
		bool bTime = false;
		for (int i = 0; i < s.GetLength(); ++i) {
			const char c = s[i];
			if (c >= '0' && c <= '9') {
				if (dScale > 0) {
					dNumber += (c - '0') * dScale;
					dScale /= 10;
				} else
					dNumber = dNumber * 10 + (c - '0');
			} else if (c == '.' || c == ',')
				dScale = 0.1;
			else {
				if (c == 'T')
					bTime = true;
				else if (c == 'D')
					dTotal += dNumber * 86400;
				else if (c == 'H')
					dTotal += dNumber * 3600;
				else if (c == 'M' && bTime)
					dTotal += dNumber * 60;
				else if (c == 'S')
					dTotal += dNumber;
				dNumber = 0;
				dScale = 0;
			}
		}
		return dTotal;
	}

	// $RepresentationID$, $Bandwidth$, $Number$, $Number%05d$, $Time$
	CString Fill(CStringA sTemplate, const CStringA &sId, int iBandwidth, long long llNumber, long long llTime)
	{
		CStringA sOut;
		int i = 0;
		while (i < sTemplate.GetLength()) {
			const int iStart = sTemplate.Find('$', i);
			const int iEnd = iStart < 0 ? -1 : sTemplate.Find('$', iStart + 1);
			if (iEnd < 0) {
				sOut += sTemplate.Mid(i);
				break;
			}
			sOut += sTemplate.Mid(i, iStart - i);
			CStringA sName(sTemplate.Mid(iStart + 1, iEnd - iStart - 1));
			CStringA sFormat("%lld");
			const int iFormat = sName.Find('%');
			if (iFormat >= 0) {
				sFormat = sName.Mid(iFormat);	// "%05d"
				sFormat.Replace("d", "lld");
				sName.Truncate(iFormat);
			}
			CStringA sValue;
			if (sName.IsEmpty())
				sValue = "$";
			else if (sName == "RepresentationID")
				sValue = sId;
			else if (sName == "Bandwidth")
				sValue.Format(sFormat, (long long)iBandwidth);
			else if (sName == "Number")
				sValue.Format(sFormat, llNumber);
			else if (sName == "Time")
				sValue.Format(sFormat, llTime);
			sOut += sValue;
			i = iEnd + 1;
		}
		return FromUtf8(sOut);
	}

	struct SDashLevel
	{
		CStringA name;
		CString	base;			// where addresses are seen from, at this depth
		std::map<CStringA, CStringA> attrs;
		std::map<CStringA, CStringA> tmpl;	// SegmentTemplate in force
		std::vector<std::pair<long long, std::pair<long long, long long> > > timeline;	// t (or -1), d, r
		bool	hasTimeline = false;
		std::vector<CStringA> list;	// SegmentList: the first of all first
		bool	isProtected = false;
	};

	void FinishRepresentation(SDashLevel &rep, const CString &sUrl, double dDuration, SVideoInfo &info, std::vector<SVideoTrack> &audios);

	void ParseDash(const CStringA &sXml, const CString &sUrl, SVideoInfo &info)
	{
		const std::vector<SXmlTag> tags(ScanXml(sXml));
		std::vector<SDashLevel> stack;
		double dDuration = 0;
		bool bFirstPeriodDone = false;
		bool bInTimeline = false;
		std::vector<SVideoTrack> audios;
		auto finish = [&](SDashLevel &rep) {
			FinishRepresentation(rep, sUrl, dDuration, info, audios);
		};
		for (size_t t = 0; t < tags.size(); ++t) {
			const SXmlTag &tag = tags[t];
			if (tag.closing) {
				if (tag.name == "SegmentTimeline")
					bInTimeline = false;
				if (!stack.empty() && stack.back().name == tag.name) {
					if (tag.name == "Period")
						bFirstPeriodDone = true;
					else if (tag.name == "Representation" && !bFirstPeriodDone)
						finish(stack.back());
					stack.pop_back();
				}
				continue;
			}
			const bool bLevel = tag.name == "MPD" || tag.name == "Period" || tag.name == "AdaptationSet" || tag.name == "Representation";
			if (tag.name == "MPD") {
				if (tag.attrs.count("type") != 0 && tag.attrs.at("type").CompareNoCase("dynamic") == 0)
					info.live = true;
				if (tag.attrs.count("mediaPresentationDuration") != 0)
					dDuration = IsoSeconds(tag.attrs.at("mediaPresentationDuration"));
			}
			if (bFirstPeriodDone)
				continue;	// the first period only
			if (bLevel) {
				SDashLevel level;
				if (!stack.empty())
					level = stack.back();	// what the level above set holds here too
				else
					level.base = sUrl;
				level.name = tag.name;
				for (const auto &attr : tag.attrs)
					level.attrs[attr.first] = attr.second;
				if (tag.name == "Period" && tag.attrs.count("duration") != 0 && dDuration == 0)
					dDuration = IsoSeconds(tag.attrs.at("duration"));
				stack.push_back(level);
			} else if (stack.empty()) {
				continue;
			} else if (tag.name == "BaseURL") {
				stack.back().base = Resolve(stack.back().base, FromUtf8(tag.text));
			} else if (tag.name == "ContentProtection") {
				stack.back().isProtected = true;
			} else if (tag.name == "SegmentTemplate") {
				for (const auto &attr : tag.attrs)
					stack.back().tmpl[attr.first] = attr.second;
			} else if (tag.name == "SegmentTimeline") {
				bInTimeline = !tag.empty;
				stack.back().timeline.clear();
				stack.back().hasTimeline = true;
			} else if (tag.name == "S" && bInTimeline) {
				const long long llT = tag.attrs.count("t") != 0 ? _atoi64(tag.attrs.at("t")) : -1;
				const long long llD = tag.attrs.count("d") != 0 ? _atoi64(tag.attrs.at("d")) : 0;
				const long long llR = tag.attrs.count("r") != 0 ? _atoi64(tag.attrs.at("r")) : 0;
				stack.back().timeline.push_back(std::make_pair(llT, std::make_pair(llD, llR)));
			} else if (tag.name == "Initialization" && tag.attrs.count("sourceURL") != 0) {
				stack.back().list.insert(stack.back().list.begin(), tag.attrs.at("sourceURL"));
			} else if (tag.name == "SegmentURL" && tag.attrs.count("media") != 0) {
				stack.back().list.push_back(tag.attrs.at("media"));
			}

			// one written as <Representation .../> is complete right away
			if (bLevel && tag.empty) {
				if (tag.name == "Representation")
					finish(stack.back());
				stack.pop_back();
			}
		}
		SortBest(info.videos);
		// of each language, the best sound
		std::stable_sort(audios.begin(), audios.end(), [](const SVideoTrack &a, const SVideoTrack &b) { return a.bandwidth > b.bandwidth; });
		for (const SVideoTrack &track : audios) {
			bool bThere = false;
			for (const SVideoTrack &kept : info.audios)
				bThere |= (kept.lang.CompareNoCase(track.lang) == 0);
			if (!bThere)
				info.audios.push_back(track);
		}
		if (info.videos.empty())
			info.notFound = true;
	}

	// What a representation which has just been read to its end comes to: a quality or a sound track.
	void FinishRepresentation(SDashLevel &rep, const CString &sUrl, double dDuration, SVideoInfo &info, std::vector<SVideoTrack> &audios)
	{
		{
			const CStringA sMime(rep.attrs["mimeType"] + " " + rep.attrs["contentType"]);
			const bool bVideo = sMime.Find("video") >= 0;
			const bool bAudio = sMime.Find("audio") >= 0;
			if (bVideo || bAudio) {
				if (rep.isProtected)
					info.isProtected = true;
				SVideoTrack track;
				track.known = true;
				track.bandwidth = atoi(rep.attrs["bandwidth"]);
				track.height = atoi(rep.attrs["height"]);
				track.lang = FromUtf8(rep.attrs["lang"]);
				const bool bWebm = sMime.Find("webm") >= 0;
				track.ext = bWebm ? _T(".webm") : (bVideo ? _T(".mp4") : _T(".m4a"));
				const CStringA sId(rep.attrs["id"]);
				if (!rep.tmpl["media"].IsEmpty()) {
					const long long llScale = max(1LL, _atoi64(rep.tmpl["timescale"]));
					long long llNumber = rep.tmpl["startNumber"].IsEmpty() ? 1 : _atoi64(rep.tmpl["startNumber"]);
					if (!rep.tmpl["initialization"].IsEmpty())
						track.pieces.push_back(Resolve(rep.base, Fill(rep.tmpl["initialization"], sId, track.bandwidth, 0, 0)));
					if (rep.hasTimeline) {
						long long llTime = 0;
						for (const auto &s : rep.timeline) {
							if (s.first >= 0)
								llTime = s.first;
							long long llRepeat = s.second.second;
							if (llRepeat < 0)	// "to the end"
								llRepeat = s.second.first > 0 ? (long long)(dDuration * llScale - llTime) / s.second.first : 0;
							for (long long r = 0; r <= llRepeat && (int)track.pieces.size() < MAX_PIECES; ++r) {
								track.pieces.push_back(Resolve(rep.base, Fill(rep.tmpl["media"], sId, track.bandwidth, llNumber, llTime)));
								llTime += s.second.first;
								++llNumber;
							}
						}
					} else {
						const long long llPiece = _atoi64(rep.tmpl["duration"]);
						const long long llCount = llPiece > 0 ? (long long)((dDuration * llScale + llPiece - 1) / llPiece) : 0;
						for (long long k = 0; k < llCount && (int)track.pieces.size() < MAX_PIECES; ++k)
							track.pieces.push_back(Resolve(rep.base, Fill(rep.tmpl["media"], sId, track.bandwidth, llNumber + k, k * llPiece)));
					}
				} else if (!rep.list.empty()) {
					for (const CStringA &sPiece : rep.list)
						track.pieces.push_back(Resolve(rep.base, FromUtf8(sPiece)));
				} else if (rep.base != sUrl)
					track.pieces.push_back(rep.base);	// the whole of it in one file
				if (!track.pieces.empty()) {
					if (bVideo) {
						track.label = QualityLabel(track.height, track.bandwidth);
						info.videos.push_back(track);
					} else {
						track.label = LanguageLabel(CString(), track.lang);
						audios.push_back(track);
					}
				}
			}
		}
	}

	// ---------------------------------------------------------------- a page

	// The first address of a list which the page names in plain sight.
	CString ListInPage(CStringA sHtml, const CString &sUrl)
	{
		sHtml.Replace("\\/", "/");		// as JSON writes it
		sHtml.Replace("\\u002F", "/");
		sHtml.Replace("&amp;", "&");
		static const LPCSTR s_apszMark[] = {".m3u8", ".mpd"};
		for (LPCSTR pszMark : s_apszMark) {
			int iPos = 0;
			while ((iPos = sHtml.Find(pszMark, iPos)) >= 0) {
				const int iAfter = iPos + (int)strlen(pszMark);
				const char cAfter = iAfter < sHtml.GetLength() ? sHtml[iAfter] : '"';
				iPos = iAfter;
				if (isalnum((unsigned char)cAfter))
					continue;	// ".mpdata" and the like
				int iStart = iAfter - (int)strlen(pszMark);
				while (iStart > 0 && strchr("\"'` \t\r\n<>()=,;", sHtml[iStart - 1]) == NULL)
					--iStart;
				int iEnd = iAfter;
				while (iEnd < sHtml.GetLength() && strchr("\"'` \t\r\n<>(),;\\", sHtml[iEnd]) == NULL)
					++iEnd;
				const CStringA sFound(sHtml.Mid(iStart, iEnd - iStart));
				if (sFound.GetLength() > 6 && (sFound.Left(4) == "http" || sFound[0] == '/' || sFound.Find('/') > 0))
					return Resolve(sUrl, FromUtf8(sFound));
			}
		}
		return CString();
	}

	// "&oacute;", "&#243;", "&#xF3;" as the letters they stand for
	CString DecodeEntities(const CString &s)
	{
		static const struct
		{
			LPCTSTR	name;
			WCHAR	ch;
		} s_aNamed[] = {
			{_T("amp"), L'&'}, {_T("quot"), L'"'}, {_T("apos"), L'\''}, {_T("lt"), L'<'}, {_T("gt"), L'>'}, {_T("nbsp"), L' '},
			{_T("aacute"), 0xE1}, {_T("eacute"), 0xE9}, {_T("iacute"), 0xED}, {_T("oacute"), 0xF3}, {_T("uacute"), 0xFA},
			{_T("Aacute"), 0xC1}, {_T("Eacute"), 0xC9}, {_T("Iacute"), 0xCD}, {_T("Oacute"), 0xD3}, {_T("Uacute"), 0xDA},
			{_T("ntilde"), 0xF1}, {_T("Ntilde"), 0xD1}, {_T("uuml"), 0xFC}, {_T("Uuml"), 0xDC}, {_T("ccedil"), 0xE7}, {_T("Ccedil"), 0xC7},
			{_T("agrave"), 0xE0}, {_T("egrave"), 0xE8}, {_T("igrave"), 0xEC}, {_T("ograve"), 0xF2}, {_T("ugrave"), 0xF9},
			{_T("acirc"), 0xE2}, {_T("ecirc"), 0xEA}, {_T("ocirc"), 0xF4}, {_T("auml"), 0xE4}, {_T("ouml"), 0xF6}, {_T("szlig"), 0xDF},
			{_T("iquest"), 0xBF}, {_T("iexcl"), 0xA1}, {_T("ndash"), 0x2013}, {_T("mdash"), 0x2014}
		};
		CString sOut;
		for (int i = 0; i < s.GetLength(); ++i) {
			if (s[i] == _T('&')) {
				const int iEnd = s.Find(_T(';'), i);
				if (iEnd > i + 1 && iEnd - i <= 9) {
					const CString sName(s.Mid(i + 1, iEnd - i - 1));
					WCHAR ch = 0;
					if (sName[0] == _T('#'))
						ch = (WCHAR)((sName.GetLength() > 1 && (sName[1] == _T('x') || sName[1] == _T('X'))) ? _tcstoul(sName.Mid(2), NULL, 16) : _tcstoul(sName.Mid(1), NULL, 10));
					else
						for (const auto &named : s_aNamed)
							if (sName == named.name)
								ch = named.ch;
					if (ch != 0) {
						sOut += ch;
						i = iEnd;
						continue;
					}
				}
			}
			sOut += s[i];
		}
		return sOut;
	}

	CString TitleOf(const CStringA &sHtml)
	{
		CStringA sLower(sHtml.Left(64 * 1024));
		sLower.MakeLower();
		const int iStart = sLower.Find("<title");
		const int iOpen = iStart < 0 ? -1 : sLower.Find('>', iStart);
		const int iEnd = iOpen < 0 ? -1 : sLower.Find("</title", iOpen);
		if (iEnd < 0)
			return CString();
		return DecodeEntities(FromUtf8(sHtml.Mid(iOpen + 1, iEnd - iOpen - 1))).Trim();
	}

	CString SafeName(CString sName)
	{
		for (int i = 0; i < sName.GetLength(); ++i)
			if (sName[i] < 32 || _tcschr(_T("\\/:*?\"<>|"), sName[i]) != NULL)
				sName.SetAt(i, _T(' '));
		while (sName.Replace(_T("  "), _T(" ")) > 0)
			;
		sName.Trim(_T(" ."));
		return sName.Left(150);
	}

	void Analyze(const CString &sUrl, SVideoInfo &info, int iDepth)
	{
		CStringA sText;
		if (!GetText(sUrl, sText, info.error))
			return;
		CStringA sStart(sText.Left(2048));
		sStart.TrimLeft();
		if (sStart.Left(7) == "#EXTM3U")
			ParseHls(sText, sUrl, info);
		else if (sStart.Find("<MPD") >= 0)
			ParseDash(sText, sUrl, info);
		else {
			const CString sList(iDepth == 0 ? ListInPage(sText, sUrl) : CString());
			if (sList.IsEmpty()) {
				info.notFound = true;
				return;
			}
			info.title = TitleOf(sText);
			Analyze(sList, info, 1);
		}
	}

	// ---------------------------------------------------------------- download

	struct SPieces
	{
		std::vector<CString> urls;
		CString	ext;
	};

	// The pieces of a track and the kind of file they make. false: sError says why not.
	bool PiecesOf(const SVideoTrack &track, bool bAudio, SPieces &out, CString &sError, EPegasoString &eWhy)
	{
		if (track.known) {
			out.urls = track.pieces;
			out.ext = track.ext;
			return !out.urls.empty();
		}
		CStringA sText;
		if (!GetText(track.url, sText, sError))
			return false;
		SHlsPieces pieces;
		ParseHlsPieces(sText, track.url, pieces);
		if (pieces.encrypted) {
			eWhy = PS_VD_PROTECTED;
			return false;
		}
		if (pieces.live) {
			eWhy = PS_VD_LIVE;
			return false;
		}
		if (pieces.ranges || pieces.urls.empty()) {
			sError = pieces.ranges ? _T("the pieces of this video are parts of one file; that kind of list is not taken") : _T("the list names no pieces");
			return false;
		}
		out.urls = pieces.urls;
		out.ext = pieces.hasMap ? (bAudio ? _T(".m4a") : _T(".mp4")) : (bAudio ? _T(".aac") : _T(".ts"));
		return true;
	}
}

bool PegasoVideoIsList(const CString &sUrl)
{
	CString sPath(sUrl);
	const int iQuery = sPath.FindOneOf(_T("?#"));
	if (iQuery >= 0)
		sPath.Truncate(iQuery);
	sPath.MakeLower();
	return sPath.Right(5) == _T(".m3u8") || sPath.Right(4) == _T(".mpd");
}

void PegasoVideoAnalyze(const CString &sUrl, SVideoInfo &info)
{
	Analyze(sUrl, info, 0);
}

CString PegasoVideoFfmpeg()
{
	CString sPath(OptUtf8ToStr(URLDecode(theApp.GetProfileString(_T("eMule"), _T("FfmpegPath"), _T("")))));
	if (!sPath.Trim().IsEmpty())
		return ::PathFileExists(sPath) ? sPath : CString();
	sPath = thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR) + _T("ffmpeg.exe");
	if (::PathFileExists(sPath))
		return sPath;
	TCHAR szFound[MAX_PATH] = {};
	return ::SearchPath(NULL, _T("ffmpeg.exe"), NULL, _countof(szFound), szFound, NULL) != 0 ? CString(szFound) : CString();
}

// ---------------------------------------------------------------- CPegasoVideoDlg

struct CPegasoVideoDlg::SLook
{
	CString	url;
	SVideoInfo info;
	std::atomic<bool> done{false};
	std::atomic<bool> dropped{false};	// nobody waits for it any more
};

struct CPegasoVideoDlg::SJob
{
	// set before the thread starts
	SVideoTrack video;
	SVideoTrack audio;
	bool	hasAudio = false;
	CString	tempBase;		// folder and beginning of the names of the files it makes
	CString	ffmpeg;
	// while it runs
	std::atomic<bool> cancel{false};
	std::atomic<bool> done{false};
	std::atomic<int> total{0};
	std::atomic<int> got{0};
	std::atomic<long long> bytes{0};
	std::atomic<bool> joining{false};
	// when it is done
	CString	error;
	EPegasoString why = PS_COUNT;	// a reason with a text of its own
	CString	videoFile;		// the one file, or the video
	CString	audioFile;		// empty unless the sound could not be joined
};

namespace
{
	CPegasoVideoDlg *s_pOpen;

	bool Fetch(CPegasoVideoDlg::SJob &job, const SPieces &pieces, const CString &sFile);

	bool RunFfmpeg(CPegasoVideoDlg::SJob &job, const CString &sArguments)
	{
		CString sCommand;
		sCommand.Format(_T("\"%s\" %s"), (LPCTSTR)job.ffmpeg, (LPCTSTR)sArguments);
		STARTUPINFO si = {};
		si.cb = sizeof si;
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};
		if (!::CreateProcess(NULL, sCommand.GetBuffer(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
			return false;
		DWORD dwExit = 1;
		for (;;) {
			if (::WaitForSingleObject(pi.hProcess, 250) == WAIT_OBJECT_0) {
				::GetExitCodeProcess(pi.hProcess, &dwExit);
				break;
			}
			if (job.cancel) {
				::TerminateProcess(pi.hProcess, 1);
				break;
			}
		}
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
		return dwExit == 0;
	}

	bool Fetch(CPegasoVideoDlg::SJob &job, const SPieces &pieces, const CString &sFile)
	{
		CFile file;
		if (!file.Open(sFile, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary)) {
			job.error = _T("the file cannot be written: ") + sFile;
			return false;
		}
		for (const CString &sUrl : pieces.urls) {
			std::vector<BYTE> data;
			CString sError;
			bool bOk = false;
			for (int iTry = 0; iTry < 4 && !bOk && !job.cancel; ++iTry) {
				if (iTry > 0)
					::Sleep(1500 * iTry);
				data.clear();
				bOk = PegasoHttpGet(sUrl, data, sError, MAX_PIECE);
			}
			if (job.cancel)
				return false;
			if (!bOk) {
				job.error.Format(_T("piece %d of %d: %s"), job.got + 1, (int)job.total, (LPCTSTR)sError);
				return false;
			}
			try {
				file.Write(data.data(), (UINT)data.size());
			} catch (CFileException *ex) {
				ex->Delete();
				job.error = _T("the disk is full, or the file cannot be written");
				return false;
			}
			job.bytes += (long long)data.size();
			++job.got;
		}
		file.Close();
		return true;
	}

	void Work(std::shared_ptr<CPegasoVideoDlg::SJob> pJob)
	{
		CPegasoVideoDlg::SJob &job = *pJob;
		SPieces video, audio;
		bool bOk = PiecesOf(job.video, false, video, job.error, job.why)
			&& (!job.hasAudio || PiecesOf(job.audio, true, audio, job.error, job.why));
		CString sVideo, sAudio;
		if (bOk) {
			job.total = (int)(video.urls.size() + audio.urls.size());
			sVideo = job.tempBase + _T("v") + video.ext;
			sAudio = job.tempBase + _T("a") + audio.ext;
			bOk = Fetch(job, video, sVideo) && (!job.hasAudio || Fetch(job, audio, sAudio));
		}
		if (bOk && !job.ffmpeg.IsEmpty() && (job.hasAudio || video.ext == _T(".ts"))) {
			// one file, without touching picture or sound
			job.joining = true;
			const CString sJoined(job.tempBase + _T("j.mp4"));
			CString sArguments;
			if (job.hasAudio)
				sArguments.Format(_T("-y -loglevel error -i \"%s\" -i \"%s\" -map 0:v:0 -map 1:a:0 -c copy \"%s\""), (LPCTSTR)sVideo, (LPCTSTR)sAudio, (LPCTSTR)sJoined);
			else
				sArguments.Format(_T("-y -loglevel error -i \"%s\" -c copy \"%s\""), (LPCTSTR)sVideo, (LPCTSTR)sJoined);
			WIN32_FILE_ATTRIBUTE_DATA made = {};
			if (RunFfmpeg(job, sArguments) && ::GetFileAttributesEx(sJoined, GetFileExInfoStandard, &made) && (made.nFileSizeLow != 0 || made.nFileSizeHigh != 0)) {
				::DeleteFile(sVideo);
				if (job.hasAudio)
					::DeleteFile(sAudio);
				sVideo = sJoined;
				sAudio.Empty();
			} else
				::DeleteFile(sJoined);	// it did not work: the files stay as they came
			job.joining = false;
		}
		if (bOk && !job.cancel) {
			job.videoFile = sVideo;
			if (job.hasAudio && !sAudio.IsEmpty() && ::PathFileExists(sAudio))
				job.audioFile = sAudio;
		} else {
			::DeleteFile(sVideo);
			::DeleteFile(sAudio);
		}
		job.done = true;
	}
}

IMPLEMENT_DYNAMIC(CPegasoVideoDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoVideoDlg, CDialog)
	ON_BN_CLICKED(IDC_VD_LOOK, OnBnClickedLook)
	ON_BN_CLICKED(IDC_VD_GO, OnBnClickedGo)
	ON_CBN_SELCHANGE(IDC_VD_QUALITY, OnCbnSelChangeQuality)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CPegasoVideoDlg::CPegasoVideoDlg(CWnd *pParent)
	: CDialog(CPegasoVideoDlg::IDD, pParent)
{
}

CPegasoVideoDlg::~CPegasoVideoDlg()
{
	if (m_look)
		m_look->dropped = true;
	if (m_job)
		m_job->cancel = true;	// the thread cleans up after itself
}

void CPegasoVideoDlg::Open(CWnd *pParent, const CString &sUrl)
{
	if (s_pOpen == NULL) {
		s_pOpen = new CPegasoVideoDlg(pParent);
		if (!s_pOpen->Create(CPegasoVideoDlg::IDD, pParent)) {
			delete s_pOpen;
			s_pOpen = NULL;
			return;
		}
	}
	s_pOpen->ShowWindow(SW_RESTORE);
	s_pOpen->SetForegroundWindow();
	if (!sUrl.IsEmpty() && !s_pOpen->m_job) {
		s_pOpen->SetDlgItemText(IDC_VD_URL, sUrl);
		s_pOpen->Look(sUrl);
	}
}

BOOL CPegasoVideoDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	SetWindowText(GetPegasoString(PS_VD_MENU));
	SetDlgItemText(IDC_VD_URL_LBL, GetPegasoString(PS_VD_URL));
	SetDlgItemText(IDC_VD_LOOK, GetPegasoString(PS_VD_LOOK));
	SetDlgItemText(IDC_VD_QUALITY_LBL, GetPegasoString(PS_VD_QUALITY));
	SetDlgItemText(IDC_VD_LANG_LBL, GetPegasoString(PS_VD_LANG));
	SetDlgItemText(IDC_VD_NAME_LBL, GetPegasoString(PS_VD_NAME));
	SetDlgItemText(IDC_VD_GO, GetPegasoString(PS_WS_DOWNLOAD));
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));
	SetDlgItemText(IDC_VD_STATUS, GetPegasoString(PS_VD_HINT));
	static_cast<CProgressCtrl*>(GetDlgItem(IDC_VD_PROGRESS))->SetRange32(0, 1000);
	EnableChoices(false);
	SetTimer(TIMER_POLL, 300, NULL);
	return TRUE;
}

void CPegasoVideoDlg::EnableChoices(bool bEnable)
{
	GetDlgItem(IDC_VD_QUALITY)->EnableWindow(bEnable);
	GetDlgItem(IDC_VD_LANG)->EnableWindow(bEnable);
	GetDlgItem(IDC_VD_NAME)->EnableWindow(bEnable);
	GetDlgItem(IDC_VD_GO)->EnableWindow(bEnable);
}

void CPegasoVideoDlg::OnOK()
{
	OnBnClickedLook();
}

void CPegasoVideoDlg::OnBnClickedLook()
{
	if (m_job)
		return;
	CString sUrl;
	GetDlgItemText(IDC_VD_URL, sUrl);
	if (CWebDownloadQueue::IsWebUrl(sUrl.Trim()))
		Look(sUrl);
}

void CPegasoVideoDlg::Look(const CString &sUrl)
{
	if (theNetGuard.IsBlocked()) {
		SetDlgItemText(IDC_VD_STATUS, GetPegasoString(PS_CC_HELD));
		return;
	}
	if (m_look)
		m_look->dropped = true;
	m_look = std::make_shared<SLook>();
	m_look->url = sUrl;
	m_sUrl = sUrl;
	EnableChoices(false);
	GetDlgItem(IDC_VD_LOOK)->EnableWindow(FALSE);
	SetDlgItemText(IDC_VD_STATUS, GetPegasoString(PS_VD_LOOKING));
	const std::shared_ptr<SLook> pLook(m_look);
	try {
		std::thread([pLook] {
			PegasoVideoAnalyze(pLook->url, pLook->info);
			pLook->done = true;
		}).detach();
	} catch (...) {
		pLook->info.error = _T("cannot start a thread");
		pLook->done = true;
	}
}

void CPegasoVideoDlg::ShowChoices()
{
	m_info = m_look->info;
	m_look.reset();
	GetDlgItem(IDC_VD_LOOK)->EnableWindow(TRUE);
	CComboBox *pQuality = static_cast<CComboBox*>(GetDlgItem(IDC_VD_QUALITY));
	pQuality->ResetContent();
	static_cast<CComboBox*>(GetDlgItem(IDC_VD_LANG))->ResetContent();
	if (!m_info.error.IsEmpty() || m_info.notFound || m_info.videos.empty() || m_info.isProtected || m_info.live) {
		SetDlgItemText(IDC_VD_STATUS, !m_info.error.IsEmpty() ? m_info.error
			: CString(GetPegasoString(m_info.isProtected ? PS_VD_PROTECTED : (m_info.live ? PS_VD_LIVE : PS_VD_NONE))));
		return;
	}
	for (const SVideoTrack &track : m_info.videos)
		pQuality->AddString(track.label);
	pQuality->SetCurSel(0);
	FillLanguages();
	// a name to start from: the title of the page, or the last word of the address
	CString sName(SafeName(m_info.title));
	if (sName.IsEmpty()) {
		CString sPath(m_sUrl);
		const int iQuery = sPath.FindOneOf(_T("?#"));
		if (iQuery >= 0)
			sPath.Truncate(iQuery);
		sPath.TrimRight(_T('/'));
		sName = SafeName(CString(OptUtf8ToStr(URLDecode(sPath.Mid(sPath.ReverseFind(_T('/')) + 1)))));
		const int iDot = sName.ReverseFind(_T('.'));
		if (iDot > 0)
			sName.Truncate(iDot);
	}
	SetDlgItemText(IDC_VD_NAME, sName.IsEmpty() ? CString(_T("video")) : sName);
	CString sFound;
	sFound.Format(GetPegasoString(PS_VD_FOUND), (int)m_info.videos.size(), (int)max((size_t)1, m_info.audios.size()));
	if (!m_info.audios.empty() && PegasoVideoFfmpeg().IsEmpty())
		sFound += CString(_T("\r\n")) + GetPegasoString(PS_VD_NOFFMPEG);
	SetDlgItemText(IDC_VD_STATUS, sFound);
	EnableChoices(true);
}

void CPegasoVideoDlg::FillLanguages()
{
	CComboBox *pLang = static_cast<CComboBox*>(GetDlgItem(IDC_VD_LANG));
	pLang->ResetContent();
	m_audioShown.clear();
	const int iQuality = static_cast<CComboBox*>(GetDlgItem(IDC_VD_QUALITY))->GetCurSel();
	const CString sGroup(iQuality >= 0 && (size_t)iQuality < m_info.videos.size() ? m_info.videos[(size_t)iQuality].group : CString());
	for (size_t i = 0; i < m_info.audios.size(); ++i)
		if (sGroup.IsEmpty() || m_info.audios[i].group.IsEmpty() || m_info.audios[i].group == sGroup) {
			pLang->AddString(m_info.audios[i].label);
			m_audioShown.push_back((int)i);
		}
	if (m_audioShown.empty()) {
		pLang->AddString(GetPegasoString(PS_VD_AUDIO_IN));
		m_audioShown.push_back(-1);
	}
	// the language of the program first, if it is there
	int iSel = 0;
	const CString sWanted(CString(GetPegasoString(PS_VD_LANGCODE)));
	for (size_t i = 0; i < m_audioShown.size(); ++i)
		if (m_audioShown[i] >= 0 && m_info.audios[(size_t)m_audioShown[i]].lang.Left(2).CompareNoCase(sWanted) == 0) {
			iSel = (int)i;
			break;
		}
	pLang->SetCurSel(iSel);
}

void CPegasoVideoDlg::OnCbnSelChangeQuality()
{
	FillLanguages();
}

void CPegasoVideoDlg::OnBnClickedGo()
{
	if (m_job || theNetGuard.IsBlocked())
		return;
	const int iQuality = static_cast<CComboBox*>(GetDlgItem(IDC_VD_QUALITY))->GetCurSel();
	const int iLang = static_cast<CComboBox*>(GetDlgItem(IDC_VD_LANG))->GetCurSel();
	CString sName;
	GetDlgItemText(IDC_VD_NAME, sName);
	if (iQuality < 0 || (size_t)iQuality >= m_info.videos.size() || iLang < 0 || (size_t)iLang >= m_audioShown.size() || SafeName(sName).IsEmpty())
		return;
	m_job = std::make_shared<SJob>();
	m_job->video = m_info.videos[(size_t)iQuality];
	if (m_audioShown[(size_t)iLang] >= 0) {
		m_job->hasAudio = true;
		m_job->audio = m_info.audios[(size_t)m_audioShown[(size_t)iLang]];
	}
	m_job->tempBase.Format(_T("%spegaso-video-%08x-"), thePrefs.GetTempDir(), ::GetTickCount());
	m_job->ffmpeg = PegasoVideoFfmpeg();
	EnableChoices(false);
	GetDlgItem(IDC_VD_LOOK)->EnableWindow(FALSE);
	GetDlgItem(IDC_VD_URL)->EnableWindow(FALSE);
	SetDlgItemText(IDCANCEL, GetResString(IDS_CANCEL));
	AddLogLine(false, _T("Video download: %s (%s) from %s"), (LPCTSTR)sName, (LPCTSTR)m_job->video.label, (LPCTSTR)m_sUrl);
	try {
		std::thread(Work, m_job).detach();
	} catch (...) {
		m_job->error = _T("cannot start a thread");
		m_job->done = true;
	}
}

void CPegasoVideoDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TIMER_POLL) {
		CDialog::OnTimer(nIDEvent);
		return;
	}
	if (m_look && m_look->done)
		ShowChoices();
	if (!m_job)
		return;
	if (m_job->done) {
		Finish();
		return;
	}
	const int iTotal = m_job->total;
	if (iTotal > 0) {
		static_cast<CProgressCtrl*>(GetDlgItem(IDC_VD_PROGRESS))->SetPos((int)((long long)m_job->got * 1000 / iTotal));
		CString sStatus;
		if (m_job->joining)
			sStatus = GetPegasoString(PS_VD_JOINING);
		else
			sStatus.Format(GetPegasoString(PS_VD_PROGRESS), (int)m_job->got, iTotal, (LPCTSTR)CastItoXBytes((uint64)(long long)m_job->bytes));
		CString sShown;
		GetDlgItemText(IDC_VD_STATUS, sShown);
		if (sShown != sStatus)
			SetDlgItemText(IDC_VD_STATUS, sStatus);
	}
}

void CPegasoVideoDlg::Finish()
{
	const std::shared_ptr<SJob> pJob(m_job);
	m_job.reset();
	GetDlgItem(IDC_VD_LOOK)->EnableWindow(TRUE);
	GetDlgItem(IDC_VD_URL)->EnableWindow(TRUE);
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));
	static_cast<CProgressCtrl*>(GetDlgItem(IDC_VD_PROGRESS))->SetPos(0);
	EnableChoices(!m_info.videos.empty());
	if (pJob->videoFile.IsEmpty()) {
		const bool bCancelled = pJob->cancel;
		const CString sWhy(bCancelled ? CString() : (pJob->why != PS_COUNT ? CString(GetPegasoString(pJob->why)) : pJob->error));
		SetDlgItemText(IDC_VD_STATUS, bCancelled ? CString(GetPegasoString(PS_VD_HINT)) : sWhy);
		if (!sWhy.IsEmpty())
			LogWarning(LOG_DEFAULT, _T("Video download failed: %s - %s"), (LPCTSTR)sWhy, (LPCTSTR)m_sUrl);
		return;
	}
	// to the folder of its type, or to the download folder, under the name the user gave it
	CString sName;
	GetDlgItemText(IDC_VD_NAME, sName);
	sName = SafeName(sName);
	const CString sExt(pJob->videoFile.Mid(pJob->videoFile.ReverseFind(_T('.'))));
	if (sName.Right(sExt.GetLength()).CompareNoCase(sExt) == 0)
		sName.Truncate(sName.GetLength() - sExt.GetLength());
	CString sFolder(PegasoAuto::TypeFolder(sName + sExt));
	if (sFolder.IsEmpty())
		sFolder = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
	CString sTarget(sFolder + sName + sExt);
	CString sBase(sName);
	for (int i = 2; ::PathFileExists(sTarget) && i < 1000; ++i) {
		sBase.Format(_T("%s (%d)"), (LPCTSTR)sName, i);
		sTarget = sFolder + sBase + sExt;
	}
	if (!::MoveFileEx(pJob->videoFile, sTarget, MOVEFILE_COPY_ALLOWED)) {
		SetDlgItemText(IDC_VD_STATUS, GetErrorMessage(::GetLastError()) + _T(" - ") + pJob->videoFile);
		return;
	}
	PegasoMarkFromInternet(sTarget, m_sUrl);
	CString sStatus;
	if (!pJob->audioFile.IsEmpty()) {
		// the sound could not be joined: next to the video, with the language in its name
		CString sLang(SafeName(pJob->audio.lang));
		const CString sAudioTarget(sFolder + sBase + (sLang.IsEmpty() ? CString(_T(".audio")) : _T(".") + sLang) + pJob->audioFile.Mid(pJob->audioFile.ReverseFind(_T('.'))));
		::MoveFileEx(pJob->audioFile, sAudioTarget, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
		PegasoMarkFromInternet(sAudioTarget, m_sUrl);
		sStatus.Format(GetPegasoString(PS_VD_DONE_SPLIT), (LPCTSTR)sTarget, (LPCTSTR)sAudioTarget);
	} else
		sStatus.Format(GetPegasoString(PS_VD_DONE), (LPCTSTR)sTarget);
	SetDlgItemText(IDC_VD_STATUS, sStatus);
	AddLogLine(true, _T("Video download finished: %s"), (LPCTSTR)sTarget);
	if (theApp.sharedfiles != NULL)
		theApp.sharedfiles->Reload();
}

void CPegasoVideoDlg::OnCancel()
{
	if (m_job) {
		m_job->cancel = true;	// the window stays: it says when the download has stopped
		SetDlgItemText(IDC_VD_STATUS, GetPegasoString(PS_RN_WAIT));
		return;
	}
	DestroyWindow();
}

void CPegasoVideoDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (s_pOpen == this)
		s_pOpen = NULL;
	delete this;
}
