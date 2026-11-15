// Pegaso: the fake detector - "does this file look like what its name says?"
//
// Every search result and every download gets a score from 0 (nothing against it) to 100
// (certainly not what it says), made of what can be seen without anybody having rated it:
//
//   - the name: a program with the name of a video ("video.avi.exe"), an archive with the
//     name of a video, an extension hidden by a text-direction character;
//   - the names the same file circulates under: of different kinds (a video for some, a
//     program for others) or unrelated to each other;
//   - the announced duration and bit rate against the size;
//   - the content, for a download: the first bytes against the extension;
//   - what the users say: the verdicts of PegasoRating and plain eMule ratings;
//   - eMule's own spam filter and disagreeing checksums.
//
// The score only warns - nothing is ever blocked. The lists show it in a "Trust" column and
// the reasons in the tool tip.
#pragma once

class CSearchFile;
class CPartFile;

struct SPegasoTrust
{
	int		score = 0;			// 0 .. 100
	bool	spam = false;		// eMule's spam filter says so
	bool	confirmed = false;	// something speaks for the file (content matches, users call it correct)
	CString	reasons;			// one per line

	bool	IsSuspicious() const	{ return score >= 50; }
};

// A search result (the row of the file, not one of its alternative names). sName: judge the
// file under that one of its names instead of the main one.
SPegasoTrust PegasoTrustOfSearchFile(const CSearchFile *pFile, const CString &sName = CString());
SPegasoTrust PegasoTrustOfDownload(CPartFile *pFile);

CString	PegasoTrustLabel(const SPegasoTrust &trust);							// "Suspicious (60)"
COLORREF PegasoTrustColor(const SPegasoTrust &trust, COLORREF crDefault);
// The reasons, as lines for the tool tip of a list ("" if there is nothing to say).
CString	PegasoTrustTip(const SPegasoTrust &trust);

// Main thread, called regularly: warns once in the log about a download which became suspicious.
void	PegasoTrustProcess();

// A column added to a list after its order was stored lands at the far right; this puts it
// at a display position once (pszFlag remembers that it was done).
void	PegasoPlaceColumn(LPCTSTR pszOrdersKey, LPCTSTR pszFlag, int iColumnCount, int iColumn, int iPosition);
