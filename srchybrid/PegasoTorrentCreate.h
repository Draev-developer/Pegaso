// Pegaso: making a torrent of a file or folder of the user, and seeding it.
//
// The files are read once to work out their hashes (which takes as long as they are large;
// a thread of its own does it), the .torrent file is written next to them, its magnet link
// is shown, and the torrent is added to the list as complete, so that it is offered at once.
// Trackers are optional: without them the torrent is found through the DHT.
//
// preferences.ini, section [eMule]: TorrentCreateTrackers
#pragma once
#include <memory>
#include "resource.h"

struct SCreateJob;

class CPegasoTorrentCreateDlg : public CDialog
{
	DECLARE_DYNAMIC(CPegasoTorrentCreateDlg)

	enum
	{
		IDD = IDD_PEGASO_CREATE
	};

public:
	explicit CPegasoTorrentCreateDlg(CWnd *pParent = NULL);
	virtual ~CPegasoTorrentCreateDlg();

	// The window does not hold Pegaso while it is open: large files take their time.
	static void Open(CWnd *pParent);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();		// creates the torrent
	virtual void OnCancel();
	virtual void PostNcDestroy();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedFile();
	afx_msg void OnBnClickedFolder();
	afx_msg void OnBnClickedCopy();
	afx_msg void OnTimer(UINT_PTR nIDEvent);

private:
	void	Say(const CString &sText);

	std::shared_ptr<SCreateJob> m_job;
	CString	m_sData;		// what the torrent is made of
	CString	m_sTorrent;		// the .torrent file
	bool	m_bSeed;
};
