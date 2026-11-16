// Pegaso: the look of the main window, see PegasoUi.h
#include "stdafx.h"
#include <gdiplus.h>
#include "emule.h"
#include "resource.h"
#include "emuleDlg.h"
#include "PegasoUi.h"
#include "PegasoStrings.h"
#include "MenuCmds.h"
#include "MuleToolBarCtrl.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "ServerConnect.h"
#include "DownloadQueue.h"
#include "SharedFileList.h"
#include "UploadQueue.h"
#include "NetGuard.h"
#include "TorrentQueue.h"
#include "TransferDlg.h"
#include "DownloadListCtrl.h"
#include "kademlia/kademlia/Kademlia.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const UINT_PTR TIMER_REFRESH = 1;
	const UINT IDC_PEGASO_FIELD = 0x5E01;

	ULONG_PTR s_gdiplus;
	CFont s_afont[PegasoUi::FONT_COUNT];

	bool StartGdiplus()
	{
		if (s_gdiplus == 0) {
			Gdiplus::GdiplusStartupInput input;
			if (Gdiplus::GdiplusStartup(&s_gdiplus, &input, NULL) != Gdiplus::Ok)
				s_gdiplus = 0;
		}
		return s_gdiplus != 0;
	}

	Gdiplus::Color ToColor(COLORREF cr)
	{
		return Gdiplus::Color(255, GetRValue(cr), GetGValue(cr), GetBValue(cr));
	}

	CString NoAmpersand(UINT uID)
	{
		CString s(GetResString(uID));
		s.Remove(_T('&'));
		return s;
	}

	void Text(CDC &dc, const CString &s, CRect rc, PegasoUi::EFont eFont, COLORREF cr, UINT uFormat = DT_LEFT)
	{
		CFont *pOld = dc.SelectObject(PegasoUi::Font(eFont));
		dc.SetTextColor(cr);
		dc.DrawText(s, &rc, uFormat | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		dc.SelectObject(pOld);
	}

	// paints into a bitmap and copies it over when it goes out of scope: no flicker
	class CBuffer
	{
	public:
		CBuffer(CDC &dcTarget, const CRect &rc)
			: m_dcTarget(dcTarget), m_rc(rc)
		{
			dc.CreateCompatibleDC(&dcTarget);
			m_bitmap.CreateCompatibleBitmap(&dcTarget, rc.Width(), rc.Height());
			m_pOld = dc.SelectObject(&m_bitmap);
			dc.SetBkMode(TRANSPARENT);
		}
		~CBuffer()
		{
			m_dcTarget.BitBlt(m_rc.left, m_rc.top, m_rc.Width(), m_rc.Height(), &dc, 0, 0, SRCCOPY);
			dc.SelectObject(m_pOld);
		}
		CDC dc;
	private:
		CDC &m_dcTarget;
		CRect m_rc;
		CBitmap m_bitmap;
		CBitmap *m_pOld;
	};
}

int PegasoUi::Scale(int n)
{
	static int s_iDpi;
	if (s_iDpi == 0) {
		HDC hdc = ::GetDC(NULL);
		s_iDpi = ::GetDeviceCaps(hdc, LOGPIXELSY);
		::ReleaseDC(NULL, hdc);
		if (s_iDpi <= 0)
			s_iDpi = 96;
	}
	return ::MulDiv(n, s_iDpi, 96);
}

CFont* PegasoUi::Font(EFont eFont)
{
	if (s_afont[FONT_TEXT].m_hObject == NULL) {
		static const struct { int iSize; int iWeight; } s_aSpec[FONT_COUNT] = {{14, FW_NORMAL}, {14, FW_SEMIBOLD}, {12, FW_NORMAL}, {19, FW_BOLD}};
		for (int i = 0; i < FONT_COUNT; ++i)
			s_afont[i].CreateFont(-Scale(s_aSpec[i].iSize), 0, 0, 0, s_aSpec[i].iWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));
	}
	return &s_afont[eFont];
}

void PegasoUi::RoundRect(CDC &dc, const CRect &rc, int iRadius, COLORREF crFill, COLORREF crBorder)
{
	if (rc.Width() <= 0 || rc.Height() <= 0)
		return;
	if (!StartGdiplus()) {	// without GDI+: the same, with rough corners
		CPen pen(crBorder != CLR_NONE ? PS_SOLID : PS_NULL, 1, crBorder != CLR_NONE ? crBorder : 0);
		CBrush brush;
		if (crFill != CLR_NONE)
			brush.CreateSolidBrush(crFill);
		CPen *pOldPen = dc.SelectObject(&pen);
		CGdiObject *pOldBrush = crFill != CLR_NONE ? dc.SelectObject(&brush) : dc.SelectStockObject(NULL_BRUSH);
		dc.RoundRect(rc, CPoint(2 * iRadius, 2 * iRadius));
		dc.SelectObject(pOldBrush);
		dc.SelectObject(pOldPen);
		return;
	}
	Gdiplus::Graphics g(dc.GetSafeHdc());
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
	const Gdiplus::REAL fInset = crBorder != CLR_NONE ? 0.5f : 0.0f;
	const Gdiplus::REAL x = rc.left + fInset, y = rc.top + fInset, w = rc.Width() - 2 * fInset, h = rc.Height() - 2 * fInset;
	const Gdiplus::REAL d = (Gdiplus::REAL)min(2 * iRadius, min(rc.Width(), rc.Height()));
	Gdiplus::GraphicsPath path;
	if (d <= 0)
		path.AddRectangle(Gdiplus::RectF(x, y, w, h));
	else {
		path.AddArc(x, y, d, d, 180, 90);
		path.AddArc(x + w - d, y, d, d, 270, 90);
		path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
		path.AddArc(x, y + h - d, d, d, 90, 90);
		path.CloseFigure();
	}
	if (crFill != CLR_NONE) {
		Gdiplus::SolidBrush brush(ToColor(crFill));
		g.FillPath(&brush, &path);
	}
	if (crBorder != CLR_NONE) {
		Gdiplus::Pen pen(ToColor(crBorder), 1.0f);
		g.DrawPath(&pen, &path);
	}
}

void PegasoUi::ProgressBar(CDC &dc, const CRect &rc, double dPercent, COLORREF crBar)
{
	const int iHeight = Scale(6);
	CRect rcTrack(rc.left, rc.CenterPoint().y - iHeight / 2, rc.right, rc.CenterPoint().y - iHeight / 2 + iHeight);
	if (rcTrack.Width() <= 0)
		return;
	RoundRect(dc, rcTrack, iHeight / 2, CR_TRACK);
	dPercent = max(0.0, min(100.0, dPercent));
	int iFilled = (int)(rcTrack.Width() * dPercent / 100.0 + 0.5);
	if (dPercent > 0 && iFilled < iHeight)
		iFilled = iHeight;	// something to see from the first byte on
	if (iFilled > 0) {
		rcTrack.right = rcTrack.left + min(iFilled, rcTrack.Width());
		RoundRect(dc, rcTrack, iHeight / 2, crBar);
	}
}

// ---------------------------------------------------------------- the bar on the left

BEGIN_MESSAGE_MAP(CPegasoSideBar, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_TIMER()
END_MESSAGE_MAP()

int CPegasoSideBar::Width()
{
	return PegasoUi::Scale(212);
}

bool CPegasoSideBar::Create(CWnd *pParent)
{
	if (!CWnd::Create(AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW)), _T(""), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, CRect(0, 0, Width(), 100), pParent, 0x5E00))
		return false;
	SetTimer(TIMER_REFRESH, 1000, NULL);
	return true;
}

void CPegasoSideBar::SetSelected(int iItem)
{
	if (iItem != m_iSelected) {
		m_iSelected = iItem;
		if (m_hWnd != NULL)
			Invalidate(FALSE);
	}
}

CString CPegasoSideBar::Snapshot() const
{
	if (theApp.IsClosing() || theApp.serverconnect == NULL || theApp.downloadqueue == NULL || theApp.sharedfiles == NULL || theApp.uploadqueue == NULL)
		return CString();
	CString s;
	s.Format(_T("%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
		(int)theApp.downloadqueue->GetFileCount(), (int)theApp.sharedfiles->GetCount(), (int)theApp.uploadqueue->GetUploadQueueLength(),
		(int)theApp.serverconnect->IsConnected(), (int)theApp.serverconnect->IsConnecting(), (int)(theApp.serverconnect->IsConnected() && theApp.serverconnect->IsLowID()),
		(int)Kademlia::CKademlia::IsRunning(), (int)Kademlia::CKademlia::IsConnected(), (int)Kademlia::CKademlia::IsFirewalled(),
		(int)theTorrents.IsRunning(), (int)theNetGuard.IsActive() + 2 * (int)theNetGuard.IsBlocked() + 4 * (int)CNetGuard::KasperskyInstalled());
	return s;
}

void CPegasoSideBar::Refresh()
{
	if (m_hWnd == NULL)
		return;
	const CString &sNow(Snapshot());
	if (sNow != m_sState) {
		m_sState = sNow;
		Invalidate(FALSE);
	}
}

void CPegasoSideBar::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TIMER_REFRESH)
		Refresh();
	else
		CWnd::OnTimer(nIDEvent);
}

void CPegasoSideBar::Layout()
{
	using PegasoUi::Scale;
	CRect rc;
	GetClientRect(&rc);
	const int iMargin = Scale(12);
	int y = Scale(68);
	const int iLines = 2 + (theNetGuard.IsActive() || CNetGuard::KasperskyInstalled() ? 1 : 0) + 1;	// eD2K, Kad, BitTorrent and, if in use, the VPN
	const int iCard = Scale(12) + Scale(22) + Scale(6) + iLines * Scale(19) + Scale(10) + Scale(36) + Scale(12);
	// in a low window the items stand closer, so that the card below them still fits
	const int iStep = max(Scale(30), min(Scale(42), (rc.bottom - iMargin - iCard - Scale(8) - y) / ITEM_COUNT));
	for (int i = 0; i < ITEM_COUNT; ++i) {
		m_arcItems[i].SetRect(iMargin, y, rc.right - iMargin - 1, y + iStep - Scale(4));
		y += iStep;
	}
	m_rcCard.SetRect(iMargin, max(y + Scale(8), rc.bottom - iMargin - iCard), rc.right - iMargin - 1, 0);
	m_rcCard.bottom = m_rcCard.top + iCard;
	m_rcConnect.SetRect(m_rcCard.left + Scale(12), m_rcCard.bottom - Scale(12) - Scale(36), m_rcCard.right - Scale(12), m_rcCard.bottom - Scale(12));
}

void CPegasoSideBar::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	Layout();
	Invalidate(FALSE);
}

int CPegasoSideBar::HitTest(CPoint pt) const
{
	for (int i = 0; i < ITEM_COUNT; ++i)
		if (m_arcItems[i].PtInRect(pt))
			return i;
	return m_rcConnect.PtInRect(pt) ? ITEM_COUNT : -1;
}

void CPegasoSideBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_bTracking) {
		TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, m_hWnd, 0};
		m_bTracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	const int iHover = HitTest(point);
	if (iHover != m_iHover) {
		m_iHover = iHover;
		Invalidate(FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

void CPegasoSideBar::OnMouseLeave()
{
	m_bTracking = false;
	if (m_iHover != -1) {
		m_iHover = -1;
		Invalidate(FALSE);
	}
}

void CPegasoSideBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	static const UINT s_auCommand[ITEM_COUNT] = {MP_PEGASO_NAV_DOWNLOADS, MP_HM_SEARCH, MP_HM_FILES, MP_PEGASO_NAV_UPLOADS, MP_HM_SRVR, MP_HM_KAD, MP_HM_STATS, MP_PEGASO_CONTACT};
	const int iHit = HitTest(point);
	if (iHit >= 0 && GetParent() != NULL)
		GetParent()->PostMessage(WM_COMMAND, iHit == ITEM_COUNT ? MP_HM_CON : s_auCommand[iHit], 0);
	CWnd::OnLButtonDown(nFlags, point);
}

CString CPegasoSideBar::Label(int iItem)
{
	switch (iItem) {
	case ITEM_DOWNLOADS:
		return GetPegasoString(PS_UI_DOWNLOADS);
	case ITEM_SEARCH:
		return NoAmpersand(IDS_EM_SEARCH);
	case ITEM_SHARED:
		return NoAmpersand(IDS_EM_FILES);
	case ITEM_UPLOADS:
		return GetPegasoString(PS_UI_UPLOADS);
	case ITEM_SERVERS:
		return GetPegasoString(PS_UI_SERVERS);
	case ITEM_KAD:
		return NoAmpersand(IDS_EM_KADEMLIA);
	case ITEM_CONTACT:
		return GetPegasoString(PS_UI_CONTACT);
	}
	return NoAmpersand(IDS_EM_STATISTIC);
}

void CPegasoSideBar::OnPaint()
{
	using namespace PegasoUi;
	CPaintDC dcPaint(this);
	CRect rc;
	GetClientRect(&rc);
	Layout();
	CBuffer buffer(dcPaint, rc);
	CDC &dc = buffer.dc;
	dc.FillSolidRect(&rc, CR_SURFACE);
	dc.FillSolidRect(rc.right - 1, rc.top, 1, rc.Height(), CR_LINE);

	// the name of the program
	const int iIcon = Scale(28);
	HICON hIcon = (HICON)::LoadImage(AfxGetResourceHandle(), _T("AAAEMULEAPP"), IMAGE_ICON, iIcon, iIcon, LR_SHARED);
	if (hIcon != NULL)
		::DrawIconEx(dc.GetSafeHdc(), Scale(22), Scale(20), hIcon, iIcon, iIcon, 0, NULL, DI_NORMAL);
	Text(dc, PEGASO_NAME, CRect(Scale(22) + iIcon + Scale(10), Scale(13), rc.right, Scale(13) + Scale(26)), FONT_TITLE, CR_TEXT);
	Text(dc, PEGASO_CREATED_BY, CRect(Scale(22) + iIcon + Scale(10), Scale(38), rc.right - Scale(4), Scale(38) + Scale(16)), FONT_SMALL, CR_MUTED);

	const bool bReady = !m_sState.IsEmpty() || !Snapshot().IsEmpty();
	CString asLabel[ITEM_COUNT];
	for (int i = 0; i < ITEM_COUNT; ++i)
		asLabel[i] = Label(i);
	int aiCount[ITEM_COUNT];
	for (int i = 0; i < ITEM_COUNT; ++i)
		aiCount[i] = -1;
	if (bReady) {
		aiCount[ITEM_DOWNLOADS] = (int)theApp.downloadqueue->GetFileCount();
		aiCount[ITEM_SHARED] = (int)theApp.sharedfiles->GetCount();
		aiCount[ITEM_UPLOADS] = (int)theApp.uploadqueue->GetUploadQueueLength();
	}
	for (int i = 0; i < ITEM_COUNT; ++i) {
		const bool bSelected = i == m_iSelected;
		if (bSelected || i == m_iHover)
			RoundRect(dc, m_arcItems[i], Scale(8), bSelected ? CR_ACCENT_BG : CR_HOVER);
		CRect rcText(m_arcItems[i]);
		rcText.DeflateRect(Scale(12), 0);
		if (aiCount[i] > 0) {
			CString sCount;
			sCount.Format(_T("%d"), aiCount[i]);
			Text(dc, sCount, rcText, bSelected ? FONT_SEMIBOLD : FONT_SMALL, bSelected ? CR_ACCENT_DARK : CR_MUTED, DT_RIGHT);
			rcText.right -= Scale(34);
		}
		Text(dc, asLabel[i], rcText, bSelected ? FONT_SEMIBOLD : FONT_TEXT, bSelected ? CR_ACCENT_DARK : CR_TEXT);
	}
	if (!bReady)
		return;

	// the networks
	const bool bEd2k = theApp.serverconnect->IsConnected();
	const bool bKad = Kademlia::CKademlia::IsConnected();
	const bool bConnecting = theApp.serverconnect->IsConnecting() || (Kademlia::CKademlia::IsRunning() && !bKad);
	const bool bAny = bEd2k || bKad;
	RoundRect(dc, m_rcCard, Scale(10), CLR_NONE, CR_LINE);
	int y = m_rcCard.top + Scale(12);
	const int x = m_rcCard.left + Scale(12);
	const int iDot = Scale(10);
	RoundRect(dc, CRect(x, y + (Scale(22) - iDot) / 2, x + iDot, y + (Scale(22) - iDot) / 2 + iDot), iDot / 2, bAny ? CR_ACCENT : (bConnecting ? CR_IDLE : CR_BORDER));
	Text(dc, NoAmpersand(bAny ? IDS_CONNECTED : (bConnecting ? IDS_CONNECTING : IDS_DISCONNECTED)),
		CRect(x + iDot + Scale(8), y, m_rcCard.right - Scale(8), y + Scale(22)), FONT_SEMIBOLD, CR_TEXT);
	y += Scale(22) + Scale(6);

	CString asLine[4];
	int iLines = 0;
	asLine[iLines++] = _T("eD2K: ") + (bEd2k ? NoAmpersand(theApp.serverconnect->IsLowID() ? IDS_IDLOW : IDS_IDHIGH)
		: NoAmpersand(theApp.serverconnect->IsConnecting() ? IDS_CONNECTING : IDS_DISCONNECTED));
	asLine[iLines++] = _T("Kad: ") + (bKad ? NoAmpersand(Kademlia::CKademlia::IsFirewalled() ? IDS_FIREWALLED : IDS_KADOPEN)
		: NoAmpersand(Kademlia::CKademlia::IsRunning() ? IDS_CONNECTING : IDS_DISCONNECTED));
	asLine[iLines++] = GetPegasoString(theTorrents.IsRunning() ? PS_UI_BT_ON : PS_UI_BT_OFF);
	if (theNetGuard.IsActive())
		asLine[iLines++] = GetPegasoString(theNetGuard.IsBlocked() ? PS_UI_VPN_BLOCKED : PS_UI_VPN_OK);
	else if (CNetGuard::KasperskyInstalled())
		asLine[iLines++] = GetPegasoString(PS_UI_VPN_UNTIED);	// there is a VPN here and Pegaso does not use it: say so
	for (int i = 0; i < iLines; ++i) {
		Text(dc, asLine[i], CRect(x, y, m_rcCard.right - Scale(8), y + Scale(19)), FONT_SMALL, CR_MUTED);
		y += Scale(19);
	}

	const bool bHover = m_iHover == ITEM_COUNT;
	const bool bIdle = !bAny && !bConnecting;
	// to connect is the one thing to do while nothing is connected: that button stands out
	RoundRect(dc, m_rcConnect, Scale(8), bIdle ? (bHover ? CR_ACCENT_DARK : CR_ACCENT) : (bHover ? CR_HOVER : CR_SURFACE), bIdle ? CLR_NONE : CR_BORDER);
	Text(dc, NoAmpersand(bAny ? IDS_MAIN_BTN_DISCONNECT : (bConnecting ? IDS_MAIN_BTN_CANCEL : IDS_MAIN_BTN_CONNECT)), m_rcConnect,
		bIdle ? FONT_SEMIBOLD : FONT_TEXT, bIdle ? RGB(255, 255, 255) : CR_TEXT, DT_CENTER);
}

// ---------------------------------------------------------------- the bar above

BEGIN_MESSAGE_MAP(CPegasoTopBar, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_CTLCOLOR()
	ON_WM_TIMER()
END_MESSAGE_MAP()

namespace
{
	const int ROW_FIELD = 64;	// the field and the buttons
	const int ROW_TITLE = 46;	// the name of the section and its filters

	CDownloadListCtrl* DownloadList()
	{
		if (theApp.IsClosing() || theApp.emuledlg == NULL || theApp.emuledlg->transferwnd == NULL || theApp.emuledlg->transferwnd->m_pwndTransfer == NULL)
			return NULL;
		CDownloadListCtrl &list = theApp.emuledlg->transferwnd->GetDownloadList();
		return list.m_hWnd != NULL ? &list : NULL;
	}
}

int CPegasoTopBar::Height()
{
	return PegasoUi::Scale(ROW_FIELD + ROW_TITLE);
}

void CPegasoTopBar::SetSection(const CString &sTitle, bool bFilters)
{
	if (sTitle != m_sTitle || bFilters != m_bFilters) {
		m_sTitle = sTitle;
		m_bFilters = bFilters;
		if (m_hWnd != NULL) {
			Layout();
			Invalidate(FALSE);
		}
	}
}

CString CPegasoTopBar::FilterText(int iFilter) const
{
	static const EPegasoString s_aText[4] = {PS_UI_FILTER_ALL, PS_UI_FILTER_ACTIVE, PS_UI_FILTER_PAUSED, PS_UI_FILTER_DONE};
	CString s;
	s.Format(_T("%s  %d"), GetPegasoString(s_aText[iFilter]), m_aiCounts[iFilter]);
	return s;
}

int CPegasoTopBar::HitTest(CPoint pt) const
{
	for (int i = 0; i < BTN_COUNT; ++i)
		if (m_arcButtons[i].PtInRect(pt))
			return i;
	if (m_bFilters)
		for (int i = 0; i < 4; ++i)
			if (m_arcFilters[i].PtInRect(pt))
				return BTN_COUNT + i;
	return -1;
}

// The counts of the filters follow the downloads; a download which changed its state leaves
// the list it no longer belongs to.
void CPegasoTopBar::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != 1) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	CDownloadListCtrl *pList = m_bFilters ? DownloadList() : NULL;
	if (pList == NULL)
		return;
	int aiCounts[4];
	pList->PegasoCounts(aiCounts);
	if (memcmp(aiCounts, m_aiCounts, sizeof aiCounts) != 0) {
		memcpy(m_aiCounts, aiCounts, sizeof aiCounts);
		if (pList->PegasoFilter() != CDownloadListCtrl::PEGASO_ALL)
			pList->PegasoRefilter();
		Layout();
		Invalidate(FALSE);
	}
}

bool CPegasoTopBar::Create(CWnd *pParent)
{
	if (!CWnd::Create(AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_ARROW)), _T(""), WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, CRect(0, 0, 100, Height()), pParent, 0x5E02))
		return false;
	m_brEdit.CreateSolidBrush(PegasoUi::CR_SURFACE);
	if (!m_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, IDC_PEGASO_FIELD))
		return false;
	m_edit.SetFont(PegasoUi::Font(PegasoUi::FONT_TEXT));
	Localize();
	SetTimer(1, 1000, NULL);
	return true;
}

void CPegasoTopBar::Localize()
{
	if (m_edit.m_hWnd != NULL)
		m_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)(LPCWSTR)CStringW(GetPegasoString(PS_UI_SEARCH_HINT)));
	if (m_hWnd != NULL) {
		Layout();
		Invalidate(FALSE);
	}
}

CString CPegasoTopBar::ButtonText(int iButton) const
{
	switch (iButton) {
	case BTN_ADD:
		return GetPegasoString(PS_UI_ADD_LINK);
	case BTN_TOOLS:
		return NoAmpersand(IDS_TOOLS);
	}
	return NoAmpersand(IDS_EM_PREFS);
}

void CPegasoTopBar::Layout()
{
	using PegasoUi::Scale;
	CRect rc;
	GetClientRect(&rc);
	const int iHeight = Scale(38);
	const int iTop = (Scale(ROW_FIELD) - iHeight) / 2;
	int iRight = rc.right - Scale(20);
	CClientDC dc(this);
	{	// the filters, from the right edge
		CFont *pOld = dc.SelectObject(PegasoUi::Font(PegasoUi::FONT_TEXT));
		const int iPill = Scale(32);
		const int iPillTop = Scale(ROW_FIELD) + (Scale(ROW_TITLE) - iPill) / 2 - Scale(4);
		int x = rc.right - Scale(20);
		for (int i = 4; --i >= 0;) {
			const int iWidth = dc.GetTextExtent(FilterText(i)).cx + Scale(28);
			m_arcFilters[i].SetRect(x - iWidth, iPillTop, x, iPillTop + iPill);
			x -= iWidth + Scale(8);
		}
		dc.SelectObject(pOld);
	}
	for (int i = BTN_COUNT; --i >= 0;) {
		CFont *pOld = dc.SelectObject(PegasoUi::Font(i == BTN_ADD ? PegasoUi::FONT_SEMIBOLD : PegasoUi::FONT_TEXT));
		const int iWidth = dc.GetTextExtent(ButtonText(i)).cx + Scale(32);
		dc.SelectObject(pOld);
		m_arcButtons[i].SetRect(iRight - iWidth, iTop, iRight, iTop + iHeight);
		iRight -= iWidth + Scale(10);
	}
	m_rcField.SetRect(Scale(20), iTop, max(Scale(20) + Scale(120), iRight - Scale(2)), iTop + iHeight);
	if (m_edit.m_hWnd != NULL) {
		const int iText = Scale(20);
		m_edit.SetWindowPos(NULL, m_rcField.left + Scale(14), m_rcField.top + (iHeight - iText) / 2, m_rcField.Width() - Scale(28), iText, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void CPegasoTopBar::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	Layout();
	Invalidate(FALSE);
}

HBRUSH CPegasoTopBar::OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor)
{
	if (pWnd != NULL && pWnd->m_hWnd == m_edit.m_hWnd) {
		pDC->SetBkColor(PegasoUi::CR_SURFACE);
		pDC->SetTextColor(PegasoUi::CR_TEXT);
		return m_brEdit;
	}
	return CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CPegasoTopBar::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_bTracking) {
		TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, m_hWnd, 0};
		m_bTracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	const int iHover = HitTest(point);
	if (iHover != m_iHover) {
		m_iHover = iHover;
		Invalidate(FALSE);
	}
	CWnd::OnMouseMove(nFlags, point);
}

void CPegasoTopBar::OnMouseLeave()
{
	m_bTracking = false;
	if (m_iHover != -1) {
		m_iHover = -1;
		Invalidate(FALSE);
	}
}

void CPegasoTopBar::OnLButtonDown(UINT nFlags, CPoint point)
{
	static const UINT s_auCommand[BTN_COUNT] = {MP_HM_DIRECT_DOWNLOAD, TBBTN_TOOLS, MP_HM_PREFS};
	for (int i = 0; i < BTN_COUNT; ++i)
		if (m_arcButtons[i].PtInRect(point) && GetParent() != NULL)
			GetParent()->PostMessage(WM_COMMAND, s_auCommand[i], 0);
	if (m_rcField.PtInRect(point))
		m_edit.SetFocus();
	const int iHit = HitTest(point);
	if (iHit >= BTN_COUNT) {
		CDownloadListCtrl *pList = DownloadList();
		if (pList != NULL) {
			pList->PegasoSetFilter(iHit - BTN_COUNT);
			Invalidate(FALSE);
		}
	}
	CWnd::OnLButtonDown(nFlags, point);
}

// Enter in the field: a link is downloaded, anything else is searched for.
BOOL CPegasoTopBar::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_edit.m_hWnd) {
		if (pMsg->wParam == VK_RETURN) {
			CString sText;
			m_edit.GetWindowText(sText);
			if (!sText.Trim().IsEmpty()) {
				m_edit.SetWindowText(_T(""));
				m_sSubmitted = sText;	// handled once this key press is done with: it may open a window
				theApp.emuledlg->PostMessage(WM_COMMAND, MP_PEGASO_SUBMIT, 0);
			}
			return TRUE;
		}
		if (pMsg->wParam == VK_ESCAPE) {
			m_edit.SetWindowText(_T(""));
			return TRUE;
		}
	}
	return CWnd::PreTranslateMessage(pMsg);
}

void CPegasoTopBar::OnPaint()
{
	using namespace PegasoUi;
	CPaintDC dcPaint(this);
	CRect rc;
	GetClientRect(&rc);
	CBuffer buffer(dcPaint, rc);
	CDC &dc = buffer.dc;
	dc.FillSolidRect(&rc, ::GetSysColor(COLOR_BTNFACE));	// as the sections below it
	RoundRect(dc, m_rcField, Scale(10), CR_SURFACE, CR_BORDER);
	for (int i = 0; i < BTN_COUNT; ++i) {
		const bool bHover = i == m_iHover;
		if (i == BTN_ADD)
			RoundRect(dc, m_arcButtons[i], Scale(10), bHover ? CR_ACCENT_DARK : CR_ACCENT);
		else
			RoundRect(dc, m_arcButtons[i], Scale(10), bHover ? CR_HOVER : CR_SURFACE, CR_BORDER);
		Text(dc, ButtonText(i), m_arcButtons[i], i == BTN_ADD ? FONT_SEMIBOLD : FONT_TEXT, i == BTN_ADD ? RGB(255, 255, 255) : CR_TEXT, DT_CENTER);
	}

	// the section which is shown, and for the downloads the states to narrow them down to
	CRect rcTitle(Scale(22), Scale(ROW_FIELD) - Scale(4), m_bFilters ? m_arcFilters[0].left - Scale(12) : rc.right - Scale(20), rc.bottom - Scale(4));
	Text(dc, m_sTitle, rcTitle, FONT_TITLE, CR_TEXT);
	if (m_bFilters) {
		const CDownloadListCtrl *pList = DownloadList();
		const int iCurrent = pList != NULL ? pList->PegasoFilter() : 0;
		for (int i = 0; i < 4; ++i) {
			if (m_arcFilters[i].left < rcTitle.left)
				continue;	// no room
			const bool bCurrent = i == iCurrent;
			const bool bHover = m_iHover == BTN_COUNT + i;
			if (bCurrent)
				RoundRect(dc, m_arcFilters[i], m_arcFilters[i].Height() / 2, CR_TEXT);
			else
				RoundRect(dc, m_arcFilters[i], m_arcFilters[i].Height() / 2, bHover ? CR_HOVER : CR_SURFACE, CR_BORDER);
			Text(dc, FilterText(i), m_arcFilters[i], FONT_TEXT, bCurrent ? RGB(255, 255, 255) : CR_TEXT, DT_CENTER);
		}
	}
}

// ---------------------------------------------------------------- contact

IMPLEMENT_DYNAMIC(CPegasoContactDlg, CDialog)

BEGIN_MESSAGE_MAP(CPegasoContactDlg, CDialog)
	ON_BN_CLICKED(IDC_CT_WRITE, OnBnClickedWrite)
	ON_BN_CLICKED(IDC_CT_COPY, OnBnClickedCopy)
END_MESSAGE_MAP()

CPegasoContactDlg::CPegasoContactDlg(CWnd *pParent)
	: CDialog(IDD_PEGASO_CONTACT, pParent)
{
}

BOOL CPegasoContactDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	SetWindowText(GetPegasoString(PS_UI_CONTACT));
	SetDlgItemText(IDC_CT_TEXT, GetPegasoString(PS_CT_TEXT));
	SetDlgItemText(IDC_CT_ADDRESS, PEGASO_CONTACT);
	SetDlgItemText(IDC_CT_WRITE, GetPegasoString(PS_CT_WRITE));
	SetDlgItemText(IDC_CT_COPY, GetPegasoString(PS_CT_COPY));
	SetDlgItemText(IDCANCEL, GetPegasoString(PS_WS_CLOSE));
	return TRUE;
}

// the mail program of the user, with the address and the version already written
void CPegasoContactDlg::OnBnClickedWrite()
{
	::ShellExecute(NULL, _T("open"), _T("mailto:") PEGASO_CONTACT _T("?subject=") PEGASO_NAME _T("%20") PEGASO_VERSION_NAME, NULL, NULL, SW_SHOWNORMAL);
}

// for those who write from the web: the address, to paste there
void CPegasoContactDlg::OnBnClickedCopy()
{
	::CopyTextToClipboard(CString(PEGASO_CONTACT));
	SetDlgItemText(IDC_CT_STATUS, GetPegasoString(PS_CT_COPIED));
}
