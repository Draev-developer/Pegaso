// Pegaso: the sources of the downloads survive a restart, see PegasoSources.h
#include "stdafx.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "emule.h"
#include "PegasoSources.h"
#include "PartFile.h"
#include "UpDownClient.h"
#include "DownloadQueue.h"
#include "IPFilter.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Log.h"
#include "Opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const size_t MAX_PER_FILE = 20;
	const time_t MAX_AGE = 24 * 3600;
	LPCTSTR const FILENAME = _T("sources.dat");

	struct SSource
	{
		time_t	tSeen = 0;		// when it last answered
		uint32	uId = 0;		// address, the way eMule keeps the ID of a client which can be called
		uint16	uPort = 0;
		uint16	uKadPort = 0;
		uint8	uOptions = 0;	// encryption: 1 supports, 2 asks for, 4 requires
		uchar	hash[MDX_DIGEST_SIZE] = {};
		int		iRank = 0;		// only while saving: the better ones first

		bool	Same(const SSource &other) const	{ return uId == other.uId && uPort == other.uPort; }
	};
	typedef std::map<CString, std::vector<SSource> > SourceMap;	// by the hash of the file

	bool		s_bLoaded;
	DWORD		s_dwLastSave;
	SourceMap	s_carried;	// what was read at startup, still young enough to be written again

	CString FilePath()
	{
		return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + FILENAME;
	}

	// The sources of a download worth remembering: they answered in this session.
	void Collect(const CPartFile *pFile, std::vector<SSource> &rOut)
	{
		const time_t tNow = time(NULL);
		for (POSITION pos = pFile->srclist.GetHeadPosition(); pos != NULL;) {
			const CUpDownClient *pSource = pFile->srclist.GetNext(pos);
			if (!pSource->IsEd2kClient() || pSource->HasLowID() || pSource->GetUserPort() == 0 || pSource->IsBanned())
				continue;
			SSource src;
			switch (pSource->GetDownloadState()) {
			case DS_DOWNLOADING:
				src.iRank = 0;
				break;
			case DS_ONQUEUE:
				src.iRank = 1;
				break;
			case DS_CONNECTED:
			case DS_REQHASHSET:
			case DS_REMOTEQUEUEFULL:
				src.iRank = 2;
				break;
			default:
				continue;	// never answered, has nothing we need, failed...
			}
			src.tSeen = tNow;
			src.uId = pSource->GetUserIDHybrid();
			src.uPort = pSource->GetUserPort();
			src.uKadPort = pSource->GetKadPort();
			src.uOptions = (uint8)((pSource->SupportsCryptLayer() ? 1 : 0) | (pSource->RequestsCryptLayer() ? 2 : 0) | (pSource->RequiresCryptLayer() ? 4 : 0));
			if (pSource->HasValidHash())
				md4cpy(src.hash, pSource->GetUserHash());
			rOut.push_back(src);
		}
		std::stable_sort(rOut.begin(), rOut.end(), [](const SSource &a, const SSource &b) { return a.iRank < b.iRank; });
		if (rOut.size() > MAX_PER_FILE)
			rOut.resize(MAX_PER_FILE);
	}

	bool IsKept(const CPartFile *pFile)
	{
		return !pFile->IsTorrent() && !pFile->IsWebFile() && pFile->GetStatus() != PS_COMPLETING && pFile->GetStatus() != PS_COMPLETE;
	}

	void Load()
	{
		FILE *fp = _tfsopen(FilePath(), _T("rt"), _SH_DENYWR);
		if (fp == NULL)
			return;
		const time_t tNow = time(NULL);
		UINT uAdded = 0, uFiles = 0;
		char line[256];
		while (fgets(line, sizeof line, fp) != NULL) {
			// file hash;seen;id;port;kad port;options;user hash
			char szFile[40] = {}, szUser[40] = {};
			long long llSeen = 0;
			unsigned uId = 0, uPort = 0, uKadPort = 0, uOptions = 0;
			if (sscanf_s(line, "%32[0-9A-Fa-f];%lld;%u;%u;%u;%u;%32[0-9A-Fa-f]", szFile, (unsigned)sizeof szFile, &llSeen, &uId, &uPort, &uKadPort, &uOptions, szUser, (unsigned)sizeof szUser) < 6)
				continue;
			uchar fileHash[MDX_DIGEST_SIZE];
			if (strlen(szFile) != 32 || !strmd4(szFile, fileHash) || uPort == 0 || uPort > 0xFFFF || uKadPort > 0xFFFF || ::IsLowID(uId))
				continue;
			if (llSeen > tNow || tNow - llSeen > MAX_AGE)
				continue;
			CPartFile *pFile = theApp.downloadqueue->GetFileByID(fileHash);
			if (pFile == NULL || !IsKept(pFile))
				continue;
			SSource src;
			src.tSeen = (time_t)llSeen;
			src.uId = uId;
			src.uPort = (uint16)uPort;
			src.uKadPort = (uint16)uKadPort;
			src.uOptions = (uint8)(uOptions & 7);
			if (strlen(szUser) == 32)
				strmd4(szUser, src.hash);
			std::vector<SSource> &list = s_carried[md4str(fileHash)];
			if (list.size() >= MAX_PER_FILE || std::any_of(list.begin(), list.end(), [&](const SSource &o) { return o.Same(src); }))
				continue;
			uFiles += static_cast<UINT>(list.empty());
			list.push_back(src);

			// the address goes through the IP filter as it is today
			if (theApp.ipfilter != NULL && theApp.ipfilter->IsFiltered(htonl(src.uId)))
				continue;
			CUpDownClient *pSource = new CUpDownClient(pFile, src.uPort, src.uId, 0, 0, false);
			pSource->SetSourceFrom(SF_LINK);
			pSource->SetKadPort(src.uKadPort);
			if (!isnulmd4(src.hash))
				pSource->SetUserHash(src.hash);
			pSource->SetConnectOptions(src.uOptions, true, false);
			if (theApp.downloadqueue->CheckAndAddSource(pFile, pSource))	// deletes the source if it is refused
				++uAdded;
		}
		fclose(fp);
		if (uAdded > 0)
			AddLogLine(false, _T("Sources remembered from the last session: %u, for %u downloads"), uAdded, uFiles);
	}
}

void PegasoSourcesSave()
{
	if (!s_bLoaded || theApp.downloadqueue == NULL)
		return;	// nothing was read yet: what is on disk is still to be used
	const time_t tNow = time(NULL);
	std::string out;
	for (POSITION pos = NULL;;) {
		const CPartFile *pFile = theApp.downloadqueue->GetFileNext(pos);
		if (pFile == NULL)
			break;
		if (IsKept(pFile)) {
			std::vector<SSource> list;
			Collect(pFile, list);
			const CString sFile(md4str(pFile->GetFileHash()));
			// what came from the last session and was not reached yet keeps its age
			const auto it = s_carried.find(sFile);
			if (it != s_carried.end())
				for (const SSource &old : it->second) {
					if (list.size() >= MAX_PER_FILE)
						break;
					if (tNow - old.tSeen <= MAX_AGE && std::none_of(list.begin(), list.end(), [&](const SSource &o) { return o.Same(old); }))
						list.push_back(old);
				}
			const CStringA sFileA(sFile);
			for (const SSource &src : list) {
				CStringA sLine;
				sLine.Format("%s;%lld;%u;%u;%u;%u;%s\n", (LPCSTR)sFileA, (long long)src.tSeen, src.uId, src.uPort, src.uKadPort, src.uOptions
					, isnulmd4(src.hash) ? "" : (LPCSTR)CStringA(md4str(src.hash)));
				out += (LPCSTR)sLine;
			}
		}
		if (pos == NULL)
			break;
	}
	// written to a new file which then takes the place of the old one
	const CString sPath(FilePath());
	const CString sTemp(sPath + _T(".new"));
	FILE *fp = _tfsopen(sTemp, _T("wb"), _SH_DENYWR);
	if (fp == NULL)
		return;
	const bool bWritten = out.empty() || fwrite(out.data(), 1, out.size(), fp) == out.size();
	fclose(fp);
	if (bWritten)
		::MoveFileEx(sTemp, sPath, MOVEFILE_REPLACE_EXISTING);
	else
		::DeleteFile(sTemp);
}

void PegasoSourcesProcess()
{
	if (theApp.downloadqueue == NULL || !theApp.IsRunning())
		return;
	const DWORD dwNow = ::GetTickCount();
	if (!s_bLoaded) {
		s_bLoaded = true;
		s_dwLastSave = dwNow;
		Load();
		return;
	}
	if (dwNow - s_dwLastSave >= MIN2MS(5)) {
		s_dwLastSave = dwNow;
		PegasoSourcesSave();
	}
}
