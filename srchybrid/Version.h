#ifndef __VERSION_H__
#define __VERSION_H__

#if _MSC_VER > 1000
#pragma once
#endif

#ifndef _T
#define _T(x)	x
#endif

#define _chSTR(x)	_T(#x)
#define chSTR(x)	_chSTR(x)

// *) Specify the version of emule only here with the following defines.
// *) When changing any of those version nr. defines you also have to rebuild the language DLLs.
//
// General format:
//	<major>.<minor>.<update>.<build>
//
// Fields:
//	<major>		major number  (e.g. 0)
//	<minor>		minor number  (e.g. 30)
//	<update>	update number (e.g. 0='a'  1='b'  2='c'  3='d'  4='e'  5='f' ...)
//	<build>		build number  (1 or higher)
//
// Currently used:
//  <major>.<minor>.<update> is used for the displayed version (GUI) and the version check number
//	<major>.<minor>			 is used for the protocol(!) version
//  -------
//  <build>                  not used for any checks in the program
#define VERSION_MJR		0
#define VERSION_MIN		72
#define VERSION_UPDATE	0
#define VERSION_BUILD	28
// NOTE: Do not forget to update manifests in "res\eMule*.manifest"

#if defined _M_X64
#define VERSION_PLATFORM _T(" x64")
#elif defined _M_ARM64
#define VERSION_PLATFORM _T(" arm64")
#else //x86
#define VERSION_PLATFORM _T("")
#endif

// NOTE: This version string is also used by the language DLLs!
#define	SZ_VERSION_NAME		chSTR(VERSION_MJR) _T(".") chSTR(VERSION_MIN) _T(".") chSTR(VERSION_UPDATE)

// Pegaso: product identity.
// The eMule version above is the protocol version announced to the ed2k/Kad network and
// must keep tracking the eMule release this code is based on; Pegaso is announced as a mod.
#define PEGASO_NAME			_T("Pegaso")
#define PEGASO_VERSION_MJR	0
#define PEGASO_VERSION_MIN	5
#define PEGASO_VERSION_UPD	3
#if PEGASO_VERSION_UPD > 0
#define PEGASO_VERSION_NAME	chSTR(PEGASO_VERSION_MJR) _T(".") chSTR(PEGASO_VERSION_MIN) _T(".") chSTR(PEGASO_VERSION_UPD)
#else
#define PEGASO_VERSION_NAME	chSTR(PEGASO_VERSION_MJR) _T(".") chSTR(PEGASO_VERSION_MIN)
#endif
#define PEGASO_MOD_VERSION	PEGASO_NAME _T(" ") PEGASO_VERSION_NAME
#define PEGASO_AUTHOR		_T("Draev")
#define PEGASO_CREATED_BY	_T("Created by ") PEGASO_AUTHOR
#define PEGASO_CONTACT		_T("draev.pegaso@gmail.com")	// where the users write to
#define PEGASO_URL			_T("https://github.com/Draev-developer/Pegaso")	// project page: source, releases, issues

#endif /* !__VERSION_H__ */
