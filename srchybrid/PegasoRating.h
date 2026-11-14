// Pegaso: verdicts on files - "is this file what it says?"
//
// Whoever has (part of) a file can tell the others: it is correct, the quality is poor,
// it is a fake, or it does not match its description (e.g. the name says Spanish and the
// audio is English). The verdict travels on what eMule already has - the rating and the
// comment of a file, sent to downloading clients and published as a Kad note - so every
// eMule client sees it too, as a rating plus a comment starting with a tag:
//
//     [OK]   [BAD QUALITY]   [FAKE]   [MISMATCH: language=en]   [MISMATCH: content]
//     [MISMATCH: quality]
//
// Ratings of clients which know nothing about the tags count as well: 1 is eMule's
// "invalid / corrupt / fake", 2 "poor", 3 to 5 are taken as correct.
#pragma once
#include "resource.h"

class CAbstractFile;
class CKnownFile;
class CPartFile;

enum EPegasoVerdict
{
	PV_NONE,
	PV_OK,
	PV_BAD_QUALITY,
	PV_FAKE,
	PV_MISMATCH
};

enum EPegasoMismatch
{
	PM_LANGUAGE,
	PM_CONTENT,
	PM_QUALITY
};

struct SPegasoVerdict
{
	EPegasoVerdict	verdict = PV_NONE;
	EPegasoMismatch	mismatch = PM_LANGUAGE;
	CString			language;	// the real language, a short code such as "en"; may be empty
	CString			text;		// free text of the user, without the tag
};

struct SPegasoVerdictCount
{
	UINT	ok = 0;
	UINT	bad = 0;
	UINT	fake = 0;
	UINT	mismatch = 0;
	CString	languages;	// real languages reported, e.g. "en, fr"

	UINT	Total() const		{ return ok + bad + fake + mismatch; }
	UINT	Negative() const	{ return fake + mismatch; }
	// several users call it fake or wrongly described, and they outnumber those who call it correct
	bool	IsSuspicious() const { return Negative() >= 2 && Negative() > ok; }
};

CString	PegasoEncodeVerdict(const SPegasoVerdict &verdict);		// comment text: tag plus free text
UINT	PegasoRatingOfVerdict(EPegasoVerdict verdict);			// eMule rating 0..5
SPegasoVerdict PegasoDecodeVerdict(const CString &sComment, UINT uRating);

// What the others say: Kad notes and, for a download, its sources.
SPegasoVerdictCount PegasoCountVerdicts(const CAbstractFile *pFile);
CString	PegasoVerdictSummary(const SPegasoVerdictCount &count);	// empty if there is none

// Called when the ratings of a download changed: warns once if it became suspicious.
void	PegasoCheckDownloadVerdicts(CPartFile *pFile);
// A download which was warned about (shown marked in the download list).
bool	PegasoIsFlaggedDownload(const CPartFile *pFile);
// Main thread, called regularly: asks Kad for the notes of the downloads, one at a time.
void	PegasoRatingProcess();

class CPegasoRateDlg : public CDialog
{
	DECLARE_DYNAMIC(CPegasoRateDlg)

	enum
	{
		IDD = IDD_PEGASO_RATE
	};

public:
	explicit CPegasoRateDlg(CKnownFile *pFile, CWnd *pParent = NULL);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnChanged();

private:
	CKnownFile *m_pFile;
};
