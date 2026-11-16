// Pegaso: the look of the main window.
//
// A bar on the left side takes the place of the toolbar of eMule: the sections of the
// program, and at its foot the state of the networks with the connect button. Above the
// section which is shown, a bar with one field - type there to search, or paste a link to
// download it - and the buttons for links, tools and options.
#pragma once

namespace PegasoUi
{
	// colours
	const COLORREF CR_GROUND = RGB(0xF4, 0xF6, 0xF9);		// behind everything
	const COLORREF CR_SURFACE = RGB(0xFF, 0xFF, 0xFF);
	const COLORREF CR_TEXT = RGB(0x1B, 0x1F, 0x27);
	const COLORREF CR_MUTED = RGB(0x5A, 0x63, 0x72);
	const COLORREF CR_LINE = RGB(0xDD, 0xE2, 0xEA);
	const COLORREF CR_BORDER = RGB(0xC5, 0xCC, 0xD8);
	const COLORREF CR_HOVER = RGB(0xF1, 0xF4, 0xF8);
	const COLORREF CR_ACCENT = RGB(0x1F, 0x5F, 0xD0);
	const COLORREF CR_ACCENT_DARK = RGB(0x16, 0x4A, 0xA6);
	const COLORREF CR_ACCENT_BG = RGB(0xE4, 0xED, 0xFB);
	const COLORREF CR_TRACK = RGB(0xE3, 0xE7, 0xEE);
	const COLORREF CR_IDLE = RGB(0x8A, 0x93, 0xA3);

	enum EFont { FONT_TEXT, FONT_SEMIBOLD, FONT_SMALL, FONT_TITLE, FONT_COUNT };

	int		Scale(int n);		// 96 dpi pixels to pixels of this screen
	CFont*	Font(EFont eFont);
	// A rectangle with round corners, smooth. CLR_NONE: no fill, or no border.
	void	RoundRect(CDC &dc, const CRect &rc, int iRadius, COLORREF crFill, COLORREF crBorder = CLR_NONE);
	// The bar of a download: how much there is, thin.
	void	ProgressBar(CDC &dc, const CRect &rc, double dPercent, COLORREF crBar);
}

class CPegasoSideBar : public CWnd
{
public:
	// ITEM_CONTACT is not a section of the window: it opens the contact window
	enum EItem { ITEM_DOWNLOADS, ITEM_SEARCH, ITEM_SHARED, ITEM_UPLOADS, ITEM_SERVERS, ITEM_KAD, ITEM_STATS, ITEM_CONTACT, ITEM_COUNT };

	bool	Create(CWnd *pParent);
	static int Width();
	static CString Label(int iItem);
	void	SetSelected(int iItem);
	void	Refresh();			// looks at the state of the program again

protected:
	int		m_iSelected = ITEM_DOWNLOADS;
	int		m_iHover = -1;		// an item, ITEM_COUNT for the connect button
	bool	m_bTracking = false;
	CRect	m_arcItems[ITEM_COUNT];
	CRect	m_rcCard, m_rcConnect;
	CString	m_sState;			// what was painted last, to repaint only on changes

	CString	Snapshot() const;
	void	Layout();
	int		HitTest(CPoint pt) const;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*)		{ return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
};

class CPegasoTopBar : public CWnd
{
public:
	enum EButton { BTN_ADD, BTN_TOOLS, BTN_PREFS, BTN_COUNT };

	bool	Create(CWnd *pParent);
	static int Height();
	void	Localize();
	CString	m_sSubmitted;	// what Enter was pressed on in the field
	void	SetSection(const CString &sTitle, bool bFilters);	// the section shown below; the downloads can be narrowed down by state
	virtual BOOL PreTranslateMessage(MSG *pMsg);

protected:
	CEdit	m_edit;
	CBrush	m_brEdit;
	CRect	m_rcField;
	CRect	m_arcButtons[BTN_COUNT];
	CString	m_sTitle;
	bool	m_bFilters = false;
	CRect	m_arcFilters[4];
	int		m_aiCounts[4] = {};
	int		m_iHover = -1;			// a button, or BTN_COUNT + a filter
	bool	m_bTracking = false;

	void	Layout();
	CString	ButtonText(int iButton) const;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*)		{ return TRUE; }
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg HBRUSH OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	int		HitTest(CPoint pt) const;
	CString	FilterText(int iFilter) const;
};

// Contact: where the users send what they would like Pegaso to do.
class CPegasoContactDlg : public CDialog
{
	DECLARE_DYNAMIC(CPegasoContactDlg)

public:
	explicit CPegasoContactDlg(CWnd *pParent = NULL);

protected:
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedWrite();
	afx_msg void OnBnClickedCopy();
};
