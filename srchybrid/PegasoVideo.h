// Pegaso: videos published as a list of pieces.
//
// A video on the web is often not one file but a list (HLS, ".m3u8"; DASH, ".mpd") which names
// the same film in several qualities and with several sound tracks, each cut into hundreds of
// short pieces. Given the address of such a list - or of a page which carries one in plain
// sight in its HTML - Pegaso reads it, lets the user choose the quality, the language and the
// name of the file, fetches the pieces and puts them together.
//
// What it takes: lists anybody can read, of videos which are not encrypted. What it does not:
// encrypted videos (HLS keys, DASH content protection), live broadcasts, and sites which do
// not publish the list but hand it to their own player through a private service, a login or
// an access token. Nothing here is written for one site in particular.
//
// Video and sound travel apart whenever there is a choice of language. They are joined into
// one file by ffmpeg, if there is one on this computer: preferences.ini [eMule] FfmpegPath, or
// ffmpeg.exe next to Pegaso.exe, or in the PATH. Without it the sound is left in a file of its
// own next to the video. Pegaso does not bring ffmpeg along.
//
// The requests go the way of every other web request of Pegaso (PegasoBridge.h).
#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include "resource.h"

struct SVideoTrack
{
	CString	label;				// for the user
	CString	lang;
	CString	group;				// HLS: the group of sound tracks a quality goes with
	CString	url;				// HLS: its own list of pieces, read when it is downloaded
	std::vector<CString> pieces;	// DASH: its pieces, the first of all first
	CString	ext;				// of the file its pieces make, with the dot
	int		height = 0;
	int		bandwidth = 0;
	bool	known = false;		// 'pieces' is filled in
};

struct SVideoInfo
{
	std::vector<SVideoTrack> videos;	// best first
	std::vector<SVideoTrack> audios;	// empty: the sound comes inside the video
	CString	title;				// of the page, if it came from one
	CString	error;
	bool	notFound = false;	// nothing of the kind there
	bool	isProtected = false;
	bool	live = false;
};

// Any thread; may take seconds. sUrl: a list, or a page which names one.
void PegasoVideoAnalyze(const CString &sUrl, SVideoInfo &info);
// Whether an address is a list by its looks (".m3u8", ".mpd").
bool PegasoVideoIsList(const CString &sUrl);
CString PegasoVideoFfmpeg();	// its path, or empty; main thread

class CPegasoVideoDlg : public CDialog
{
	DECLARE_DYNAMIC(CPegasoVideoDlg)

	enum
	{
		IDD = IDD_PEGASO_VIDEO
	};

public:
	explicit CPegasoVideoDlg(CWnd *pParent = NULL);
	virtual ~CPegasoVideoDlg();
	// Opens the window (there is one at most). With an address, looks at it right away.
	static void Open(CWnd *pParent, const CString &sUrl = CString());
	struct SLook;	// looking at an address
	struct SJob;	// fetching a video

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();		// Enter looks at the address
	virtual void OnCancel();
	virtual void PostNcDestroy();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedLook();
	afx_msg void OnBnClickedGo();
	afx_msg void OnCbnSelChangeQuality();
	afx_msg void OnTimer(UINT_PTR nIDEvent);

private:
	void	Look(const CString &sUrl);
	void	ShowChoices();
	void	FillLanguages();
	void	Finish();
	void	EnableChoices(bool bEnable);

	std::shared_ptr<SLook> m_look;
	std::shared_ptr<SJob> m_job;
	SVideoInfo m_info;
	std::vector<int> m_audioShown;	// rows of the language list -> m_info.audios; -1: the sound of the video
	CString	m_sUrl;
};
