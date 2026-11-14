// Pegaso: verdicts on files. See PegasoRating.h
#include "stdafx.h"
#include <map>
#include <set>
#include "emule.h"
#include "emuleDlg.h"
#include "PegasoRating.h"
#include "PegasoStrings.h"
#include "AbstractFile.h"
#include "KnownFile.h"
#include "PartFile.h"
#include "UpDownClient.h"
#include "DownloadQueue.h"
#include "SearchList.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Log.h"
#include "Opcodes.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/kademlia/Entry.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/kademlia/Search.h"
#include "kademlia/kademlia/Tag.h"
#include "kademlia/utils/UInt128.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	struct SLanguage
	{
		LPCTSTR	code;
		LPCTSTR	name;
	};

	// the codes travel in the comment; the names are shown in their own language
	const SLanguage s_aLanguages[] =
	{
		{_T(""), _T("?")},
		{_T("es"), L"Español (España)"},
		{_T("lat"), L"Español (Latinoamérica)"},
		{_T("en"), _T("English")},
		{_T("fr"), L"Français"},
		{_T("it"), _T("Italiano")},
		{_T("de"), _T("Deutsch")},
		{_T("pt"), L"Português"},
		{_T("ru"), L"Русский"},
		{_T("ca"), L"Català"},
		{_T("ja"), L"日本語"},
		{_T("zh"), L"中文"},
		{_T("ko"), L"한국어"},
		{_T("ar"), L"العربية"},
		{_T("other"), _T("...")}
	};

	// files already warned about / known as suspicious, by hash
	std::map<CString, bool> s_flagged;
	std::set<CString> s_notesAsked;
	DWORD s_dwLastNotes;

	void Count(SPegasoVerdictCount &count, const CString &sComment, UINT uRating)
	{
		const SPegasoVerdict v(PegasoDecodeVerdict(sComment, uRating));
		switch (v.verdict) {
		case PV_OK:
			++count.ok;
			break;
		case PV_BAD_QUALITY:
			++count.bad;
			break;
		case PV_FAKE:
			++count.fake;
			break;
		case PV_MISMATCH:
			++count.mismatch;
			if (v.mismatch == PM_LANGUAGE && !v.language.IsEmpty()) {
				const CString sFind(_T(", ") + v.language + _T(","));
				if ((_T(", ") + count.languages + _T(",")).Find(sFind) < 0) {
					if (!count.languages.IsEmpty())
						count.languages += _T(", ");
					count.languages += v.language;
				}
			}
		default:
			break;
		}
	}
}

UINT PegasoRatingOfVerdict(EPegasoVerdict verdict)
{
	switch (verdict) {
	case PV_OK:
		return 4;	// good
	case PV_BAD_QUALITY:
	case PV_MISMATCH:
		return 2;	// poor
	case PV_FAKE:
		return 1;	// invalid / corrupt / fake
	default:
		return 0;
	}
}

CString PegasoEncodeVerdict(const SPegasoVerdict &verdict)
{
	CString sTag;
	switch (verdict.verdict) {
	case PV_OK:
		sTag = _T("[OK]");
		break;
	case PV_BAD_QUALITY:
		sTag = _T("[BAD QUALITY]");
		break;
	case PV_FAKE:
		sTag = _T("[FAKE]");
		break;
	case PV_MISMATCH:
		switch (verdict.mismatch) {
		case PM_CONTENT:
			sTag = _T("[MISMATCH: content]");
			break;
		case PM_QUALITY:
			sTag = _T("[MISMATCH: quality]");
			break;
		default:
			if (verdict.language.IsEmpty())
				sTag = _T("[MISMATCH: language]");
			else
				sTag.Format(_T("[MISMATCH: language=%s]"), (LPCTSTR)verdict.language);
		}
		break;
	default:
		break;
	}
	CString sText(verdict.text);
	sText.Trim();
	if (!sTag.IsEmpty() && !sText.IsEmpty())
		sTag += _T(' ');
	return (sTag + sText).Left(MAXFILECOMMENTLEN);
}

SPegasoVerdict PegasoDecodeVerdict(const CString &sComment, UINT uRating)
{
	SPegasoVerdict v;
	v.text = sComment;
	v.text.Trim();
	const int iEnd = (v.text.GetLength() > 2 && v.text[0] == _T('[')) ? v.text.Find(_T(']')) : -1;
	if (iEnd > 1 && iEnd <= 40) {
		CString sTag(v.text.Mid(1, iEnd - 1));
		sTag.MakeLower();
		sTag.Remove(_T(' '));
		if (sTag == _T("ok"))
			v.verdict = PV_OK;
		else if (sTag == _T("badquality"))
			v.verdict = PV_BAD_QUALITY;
		else if (sTag == _T("fake"))
			v.verdict = PV_FAKE;
		else if (sTag.Left(9) == _T("mismatch:")) {
			v.verdict = PV_MISMATCH;
			const CString sWhat(sTag.Mid(9));
			if (sWhat == _T("content"))
				v.mismatch = PM_CONTENT;
			else if (sWhat == _T("quality"))
				v.mismatch = PM_QUALITY;
			else if (sWhat.Left(8) == _T("language")) {
				v.mismatch = PM_LANGUAGE;
				if (sWhat.GetLength() > 9 && sWhat[8] == _T('='))
					v.language = sWhat.Mid(9, 8).SpanIncluding(_T("abcdefghijklmnopqrstuvwxyz-"));
			} else
				v.verdict = PV_NONE;
		}
		if (v.verdict != PV_NONE) {
			v.text = v.text.Mid(iEnd + 1);
			v.text.Trim();
			return v;
		}
	}
	// no tag: a plain eMule rating
	if (uRating == 1)
		v.verdict = PV_FAKE;
	else if (uRating == 2)
		v.verdict = PV_BAD_QUALITY;
	else if (uRating >= 3 && uRating <= 5)
		v.verdict = PV_OK;
	return v;
}

SPegasoVerdictCount PegasoCountVerdicts(const CAbstractFile *pFile)
{
	SPegasoVerdictCount count;
	if (pFile == NULL)
		return count;
	const CKadEntryPtrList &notes = pFile->getNotes();
	for (POSITION pos = notes.GetHeadPosition(); pos != NULL;) {
		const Kademlia::CEntry *pEntry = notes.GetNext(pos);
		Count(count, pEntry->GetStrTagValue(Kademlia::CKadTagNameString(TAG_DESCRIPTION))
			, (UINT)pEntry->GetIntTagValue(Kademlia::CKadTagNameString(TAG_FILERATING)));
	}
	if (pFile->IsPartFile()) {
		const CPartFile *pPartFile = static_cast<const CPartFile*>(pFile);
		for (POSITION pos = pPartFile->srclist.GetHeadPosition(); pos != NULL;) {
			const CUpDownClient *pSource = pPartFile->srclist.GetNext(pos);
			if (pSource->HasFileRating() || pSource->HasFileComment())
				Count(count, pSource->GetFileComment(), pSource->GetFileRating());
		}
	}
	return count;
}

CString PegasoVerdictSummary(const SPegasoVerdictCount &count)
{
	CString sOut;
	const struct
	{
		UINT uCount;
		EPegasoString id;
	} aParts[] = {{count.ok, PS_RATE_SUM_OK}, {count.bad, PS_RATE_SUM_BAD}, {count.fake, PS_RATE_SUM_FAKE}, {count.mismatch, PS_RATE_SUM_MISMATCH}};
	for (const auto &part : aParts)
		if (part.uCount > 0) {
			if (!sOut.IsEmpty())
				sOut += _T("  -  ");
			sOut.AppendFormat(GetPegasoString(part.id), part.uCount);
			if (part.id == PS_RATE_SUM_MISMATCH && !count.languages.IsEmpty())
				sOut.AppendFormat(_T(" (%s)"), (LPCTSTR)count.languages);
		}
	return sOut;
}

void PegasoCheckDownloadVerdicts(CPartFile *pFile)
{
	if (pFile == NULL || pFile->IsWebFile() || pFile->IsTorrent() || theApp.IsClosing())
		return;
	const SPegasoVerdictCount count(PegasoCountVerdicts(pFile));
	if (!count.IsSuspicious())
		return;
	bool &bWarned = s_flagged[md4str(pFile->GetFileHash())];
	if (bWarned)
		return;
	bWarned = true;
	CString sText;
	sText.Format(GetPegasoString(PS_RATE_WARN), count.Negative(), (LPCTSTR)pFile->GetFileName());
	const CString sSummary(PegasoVerdictSummary(count));
	LogWarning(LOG_STATUSBAR, _T("%s  [%s]"), (LPCTSTR)sText, (LPCTSTR)sSummary);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->ShowNotifier(sText, TBN_IMPORTANTEVENT);
}

bool PegasoIsFlaggedDownload(const CPartFile *pFile)
{
	if (s_flagged.empty())
		return false;
	const auto it = s_flagged.find(md4str(pFile->GetFileHash()));
	return it != s_flagged.end() && it->second;
}

void PegasoRatingProcess()
{
	const DWORD dwNow = ::GetTickCount();
	if (dwNow - s_dwLastNotes < SEC2MS(30))
		return;
	s_dwLastNotes = dwNow;
	if (!Kademlia::CKademlia::IsConnected() || theApp.downloadqueue == NULL)
		return;
	POSITION pos = NULL;
	for (INT_PTR i = theApp.downloadqueue->GetFileCount(); --i >= 0;) {
		CPartFile *pFile = theApp.downloadqueue->GetFileNext(pos);
		if (pFile == NULL)
			break;
		if (pFile->IsWebFile() || pFile->IsTorrent() || pFile->IsStopped() || pFile->GetStatus() == PS_COMPLETE)
			continue;
		const CString sHash(md4str(pFile->GetFileHash()));
		if (s_notesAsked.count(sHash))
			continue;
		if (Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::NOTES, true, Kademlia::CUInt128(pFile->GetFileHash()))) {
			s_notesAsked.insert(sHash);
			theApp.searchlist->SetNotesSearchStatus(pFile->GetFileHash(), true);
			pFile->SetKadCommentSearchRunning(true);
		}
		break;	// one lookup at a time
	}
}

// ---------------------------------------------------------------- CPegasoRateDlg

IMPLEMENT_DYNAMIC(CPegasoRateDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoRateDlg, CDialog)
	ON_BN_CLICKED(IDC_RT_OK, OnChanged)
	ON_BN_CLICKED(IDC_RT_BAD, OnChanged)
	ON_BN_CLICKED(IDC_RT_FAKE, OnChanged)
	ON_BN_CLICKED(IDC_RT_MISMATCH, OnChanged)
	ON_BN_CLICKED(IDC_RT_NONE, OnChanged)
	ON_CBN_SELCHANGE(IDC_RT_WHAT, OnChanged)
END_MESSAGE_MAP()

CPegasoRateDlg::CPegasoRateDlg(CKnownFile *pFile, CWnd *pParent)
	: CDialog(CPegasoRateDlg::IDD, pParent)
	, m_pFile(pFile)
{
}

BOOL CPegasoRateDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);

	SetWindowText(GetPegasoString(PS_RATE_TITLE));
	SetDlgItemText(IDC_RT_FILE, m_pFile->GetFileName());
	SetDlgItemText(IDC_RT_OK, GetPegasoString(PS_RATE_OK));
	SetDlgItemText(IDC_RT_BAD, GetPegasoString(PS_RATE_BAD));
	SetDlgItemText(IDC_RT_FAKE, GetPegasoString(PS_RATE_FAKE));
	SetDlgItemText(IDC_RT_MISMATCH, GetPegasoString(PS_RATE_MISMATCH));
	SetDlgItemText(IDC_RT_NONE, GetPegasoString(PS_RATE_NONE));
	SetDlgItemText(IDC_RT_WHAT_LBL, GetPegasoString(PS_RATE_WHAT));
	SetDlgItemText(IDC_RT_LANG_LBL, GetPegasoString(PS_RATE_LANG));
	SetDlgItemText(IDC_RT_TEXT_LBL, GetPegasoString(PS_RATE_TEXT));
	SetDlgItemText(IDC_RT_HINT, GetPegasoString(PS_RATE_HINT));
	SetDlgItemText(IDOK, GetResString(IDS_TREEOPTIONS_OK));
	SetDlgItemText(IDCANCEL, GetResString(IDS_CANCEL));

	CComboBox *pWhat = static_cast<CComboBox*>(GetDlgItem(IDC_RT_WHAT));
	pWhat->AddString(GetPegasoString(PS_RATE_WHAT_LANG));
	pWhat->AddString(GetPegasoString(PS_RATE_WHAT_CONTENT));
	pWhat->AddString(GetPegasoString(PS_RATE_WHAT_QUALITY));
	CComboBox *pLang = static_cast<CComboBox*>(GetDlgItem(IDC_RT_LANG));
	for (const SLanguage &lang : s_aLanguages)
		pLang->AddString(lang.name);

	// what this user said before
	const SPegasoVerdict mine(PegasoDecodeVerdict(m_pFile->GetFileComment(), m_pFile->GetFileRating()));
	int iRadio;
	switch (mine.verdict) {
	case PV_OK:
		iRadio = IDC_RT_OK;
		break;
	case PV_BAD_QUALITY:
		iRadio = IDC_RT_BAD;
		break;
	case PV_FAKE:
		iRadio = IDC_RT_FAKE;
		break;
	case PV_MISMATCH:
		iRadio = IDC_RT_MISMATCH;
		break;
	default:
		iRadio = IDC_RT_NONE;
	}
	CheckRadioButton(IDC_RT_OK, IDC_RT_NONE, iRadio);
	pWhat->SetCurSel((int)mine.mismatch);
	int iLang = 0;
	for (int i = 0; i < _countof(s_aLanguages); ++i)
		if (mine.language == s_aLanguages[i].code)
			iLang = i;
	pLang->SetCurSel(iLang);
	static_cast<CEdit*>(GetDlgItem(IDC_RT_TEXT))->SetLimitText(MAXFILECOMMENTLEN - 30);
	SetDlgItemText(IDC_RT_TEXT, mine.text);

	const CString sOthers(PegasoVerdictSummary(PegasoCountVerdicts(m_pFile)));
	CString sLine;
	if (sOthers.IsEmpty())
		sLine = GetPegasoString(PS_RATE_OTHERS_NONE);
	else
		sLine.Format(GetPegasoString(PS_RATE_OTHERS), (LPCTSTR)sOthers);
	SetDlgItemText(IDC_RT_OTHERS, sLine);

	OnChanged();
	return TRUE;
}

void CPegasoRateDlg::OnChanged()
{
	const BOOL bMismatch = IsDlgButtonChecked(IDC_RT_MISMATCH) == BST_CHECKED;
	const BOOL bLanguage = bMismatch && static_cast<CComboBox*>(GetDlgItem(IDC_RT_WHAT))->GetCurSel() == (int)PM_LANGUAGE;
	GetDlgItem(IDC_RT_WHAT_LBL)->EnableWindow(bMismatch);
	GetDlgItem(IDC_RT_WHAT)->EnableWindow(bMismatch);
	GetDlgItem(IDC_RT_LANG_LBL)->EnableWindow(bLanguage);
	GetDlgItem(IDC_RT_LANG)->EnableWindow(bLanguage);
	GetDlgItem(IDC_RT_TEXT)->EnableWindow(IsDlgButtonChecked(IDC_RT_NONE) != BST_CHECKED);
}

void CPegasoRateDlg::OnOK()
{
	SPegasoVerdict v;
	switch (GetCheckedRadioButton(IDC_RT_OK, IDC_RT_NONE)) {
	case IDC_RT_OK:
		v.verdict = PV_OK;
		break;
	case IDC_RT_BAD:
		v.verdict = PV_BAD_QUALITY;
		break;
	case IDC_RT_FAKE:
		v.verdict = PV_FAKE;
		break;
	case IDC_RT_MISMATCH:
		v.verdict = PV_MISMATCH;
		break;
	default:
		v.verdict = PV_NONE;
	}
	if (v.verdict != PV_NONE) {
		const int iWhat = static_cast<CComboBox*>(GetDlgItem(IDC_RT_WHAT))->GetCurSel();
		v.mismatch = (iWhat == (int)PM_CONTENT) ? PM_CONTENT : (iWhat == (int)PM_QUALITY) ? PM_QUALITY : PM_LANGUAGE;
		const int iLang = static_cast<CComboBox*>(GetDlgItem(IDC_RT_LANG))->GetCurSel();
		if (iLang > 0 && iLang < _countof(s_aLanguages))
			v.language = s_aLanguages[iLang].code;
		GetDlgItemText(IDC_RT_TEXT, v.text);
	}
	m_pFile->SetFileComment(PegasoEncodeVerdict(v));
	m_pFile->SetFileRating(PegasoRatingOfVerdict(v.verdict));
	m_pFile->UpdateFileRatingCommentAvail();
	CDialog::OnOK();
}
