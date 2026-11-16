// Pegaso: finished videos get the name of the film or series they are, looked up in the
// databases the user chooses, and may be moved to another folder.
//
//   films:  Name (year).ext            TheMovieDB, OMDb
//   series: Name - 1x05.ext            TheMovieDB, TheTVDB, TVmaze, AniDB
//
// What a file is - film or episode, of what, which season and episode - is read from its
// name. The database only gives the proper name (and the year of a film).
// TVmaze and AniDB need no key. TheMovieDB, OMDb and TheTVDB give one to whoever signs up;
// Pegaso ships with none, the user types his own.
//
// A file which is renamed or moved stays what it was for the networks: the ed2k hash and the
// torrent are about its contents, not its name. The file known to eMule is given its new
// name and place (and shared on its own if the new folder is not a shared one), and a file
// of a torrent is renamed by the BitTorrent engine itself, which goes on seeding it.
//
// preferences.ini, section [eMule]: RenameSeriesDb, RenameMoviesDb, RenameSeriesDir,
// RenameMoviesDir, RenameClean (1), RenameKeyTmdb, RenameKeyOmdb, RenameKeyTvdb
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "resource.h"

enum ERenameDb
{
	RDB_TVMAZE,
	RDB_TMDB,
	RDB_TVDB,
	RDB_ANIDB,
	RDB_OMDB
};

struct SVideoName
{
	bool	series = false;
	CString	title;
	int		season = 0;
	int		episode = 0;
	int		year = 0;		// of a film; 0 = not known
};

struct SRenameJob;
struct SMoveJob;

namespace PegasoRename
{
	bool	IsVideo(const CString &sPath);
	// what the name of a file tells (sFileName: without folder, with or without extension)
	SVideoName Parse(const CString &sFileName);
	// the name of the file as Pegaso writes it, extension included (sExt: with its dot)
	CString	Build(const SVideoName &name, const CString &sExt);
	// before a file is moved (complete paths): the new place must be free; its folder is made
	bool	Prepare(const CString &sOldPath, const CString &sNewPath, CString &sError);
	// after it was moved: eMule learns the new name and place, and the file stays shared
	void	Moved(const CString &sOldPath, const CString &sNewPath);
	// After a video was moved out of a folder inside the download folders: if no other video
	// is left in it, the folder goes. What a torrent of the list still needs is kept for it
	// elsewhere, so that it goes on seeding; the rest goes to the Recycle Bin with the folder.
	// Returns true if the folder is gone.
	bool	CleanFolder(const CString &sFolder);
}

class CPegasoRenameDlg : public CDialog
{
	DECLARE_DYNAMIC(CPegasoRenameDlg)

	enum
	{
		IDD = IDD_PEGASO_RENAME
	};

public:
	explicit CPegasoRenameDlg(CWnd *pParent = NULL);
	virtual ~CPegasoRenameDlg();

	// The window does not hold Pegaso while it is open: moving many files takes its time.
	static void Open(CWnd *pParent);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();		// Enter looks the names up
	virtual void OnCancel();
	virtual void PostNcDestroy();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedKeys();
	afx_msg void OnBnClickedApply();
	afx_msg void OnBnClickedMoviesDir();
	afx_msg void OnBnClickedSeriesDir();
	afx_msg void OnLvnEndLabelEdit(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnTimer(UINT_PTR nIDEvent);

private:
	struct SRow
	{
		CString	path;		// where the file is
		SVideoName name;	// read from the name of the file, then completed by the database
		CString	target;		// the new name, extension included
		CString	state;
		bool	found = false;
	};

	void	Scan();
	void	ShowRow(int iRow);
	void	SavePrefs();
	void	NextMove();
	void	StartMove();
	void	CompleteMove();
	void	MoveTick();
	void	EnableButtons(bool bEnable);

	CListCtrl m_list;
	std::vector<SRow> m_rows;
	std::shared_ptr<SRenameJob> m_job;

	// Files are moved one after another, each by a thread of its own, while this window and
	// the rest of Pegaso go on working.
	enum EMoveState
	{
		MS_IDLE,
		MS_PAUSING,		// the torrent of the file is letting go of it
		MS_MOVING
	};
	EMoveState m_eMove;
	std::vector<int> m_queue;	// rows to move
	std::vector<CString> m_left;	// folders videos were moved out of
	size_t	m_uNext;
	ULONGLONG m_uTotal;			// bytes of all the files of the queue
	ULONGLONG m_uFinished;		// bytes of the files which are through
	int		m_iDone;
	int		m_iFailed;
	CString	m_sMoviesDir;
	CString	m_sSeriesDir;
	std::shared_ptr<SMoveJob> m_move;
	CString	m_sFrom;
	CString	m_sTo;
	ULONGLONG m_uSize;
	std::string m_torrent;		// the torrent the file belongs to; empty if none
	int		m_iTorrentFile;
	bool	m_bWasPaused;
	DWORD	m_dwSince;
};

class CPegasoRenameKeysDlg : public CDialog
{
	DECLARE_DYNAMIC(CPegasoRenameKeysDlg)

	enum
	{
		IDD = IDD_PEGASO_RENAME_KEYS
	};

public:
	explicit CPegasoRenameKeysDlg(CWnd *pParent = NULL);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedTmdb();
	afx_msg void OnBnClickedOmdb();
	afx_msg void OnBnClickedTvdb();
};
