// Pegaso: what is done without being asked each time, and bringing downloads from other programs.
//
//  - Watched folder: .torrent files, and .txt files with one link per line, left in it are
//    added to the downloads; what was taken is moved to its "added" subfolder.
//  - Folders by type: a finished download without a category of its own goes to the folder set
//    for its type (video, audio, images, documents, archives, programs). The file stays shared;
//    a torrent goes whole, to the folder of its largest file, and stays seeded.
//    preferences.ini, [eMule]: WatchFolder, TypeFolderVideo, TypeFolderAudio, TypeFolderImage,
//    TypeFolderDoc, TypeFolderArchive, TypeFolderProgram (URL-encoded UTF-8; empty = off).
//  - Import from eMule: its folder of unfinished downloads becomes one more temporary folder of
//    Pegaso, which goes on with those downloads where they are.
//  - Import from qBittorrent: the torrents of its BT_backup folder are added with what
//    qBittorrent knew of them - where the files are, which ones are wanted, what is there.
#pragma once
#include "resource.h"

namespace PegasoAuto
{
	enum { TYPE_COUNT = 6 };
	CString	TypeLabel(int iType);					// in the language of the program
	CString	TypeFolderSetting(int iType);			// as the user wrote it
	void	SetTypeFolderSetting(int iType, const CString &sFolder);
	CString	WatchFolder();
	void	SetWatchFolder(const CString &sFolder);

	// The folder a finished file of this name goes to, with its backslash and already made;
	// empty if none is set for its type. Any thread.
	CString	TypeFolder(LPCTSTR pszFileName);

	void	Process();								// main thread, from CDownloadQueue::Process()

	CString	GuessEmuleTemp();
	CString	GuessQBittorrent();
	// Each gives back the number of downloads added, and says the rest in the text.
	int		ImportEmule(const CString &sFolder, CString &sReport);
	int		ImportQBittorrent(const CString &sFolder, CString &sReport);
}

class CPegasoFoldersDlg : public CDialog
{
	DECLARE_DYNAMIC(CPegasoFoldersDlg)

	enum
	{
		IDD = IDD_PEGASO_FOLDERS
	};

public:
	explicit CPegasoFoldersDlg(CWnd *pParent = NULL);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedBrowseWatch();
	afx_msg void OnBnClickedBrowseType(UINT uId);
};

class CPegasoImportDlg : public CDialog
{
	DECLARE_DYNAMIC(CPegasoImportDlg)

	enum
	{
		IDD = IDD_PEGASO_IMPORT
	};

public:
	explicit CPegasoImportDlg(CWnd *pParent = NULL);

protected:
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedBrowseEmule();
	afx_msg void OnBnClickedBrowseQb();
	afx_msg void OnBnClickedEmule();
	afx_msg void OnBnClickedQb();
};
