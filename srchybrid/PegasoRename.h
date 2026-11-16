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
// RenameMoviesDir, RenameKeyTmdb, RenameKeyOmdb, RenameKeyTvdb
#pragma once
#include <memory>
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

namespace PegasoRename
{
	bool	IsVideo(const CString &sPath);
	// what the name of a file tells (sFileName: without folder, with or without extension)
	SVideoName Parse(const CString &sFileName);
	// the name of the file as Pegaso writes it, extension included (sExt: with its dot)
	CString	Build(const SVideoName &name, const CString &sExt);
	// Gives a finished download another name or folder (complete paths) and keeps it shared
	// on eD2K/Kad and seeded on BitTorrent. Main thread.
	bool	MoveFinished(const CString &sOldPath, const CString &sNewPath, CString &sError);
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

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();		// Enter looks the names up

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

	CListCtrl m_list;
	std::vector<SRow> m_rows;
	std::shared_ptr<SRenameJob> m_job;
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
};
