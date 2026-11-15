// Pegaso: VPN protection.
//
// The user may tie Pegaso to one network adapter (typically the one of a VPN):
//  - ed2k/Kad and BitTorrent sockets are bound to the address of that adapter, so their
//    traffic cannot leave through another one;
//  - web downloads go through WinHTTP, which cannot be bound; they follow the routing of
//    the system and are stopped by the kill switch;
//  - with the kill switch on, all transfers stop while the adapter is not connected.
//
// preferences.ini, section [eMule]: PegasoBindAdapter (name, empty = off), PegasoKillSwitch (1)
// Main thread only.
#pragma once
#include <vector>

struct SNetAdapter
{
	CString	name;			// as shown in the network settings of Windows
	CString	description;
	CString	ipv4;			// empty if it has no address
	bool	up;
};

class CNetGuard
{
public:
	CNetGuard();

	static std::vector<SNetAdapter> ListAdapters();
	static CString AdapterInPrefs();
	static bool KillSwitchInPrefs();

	void	Init();			// at startup, before any socket is created
	void	ApplyPrefs(const CString &sAdapter, bool bKillSwitch);
	void	Process();		// called from CDownloadQueue::Process()

	// Kaspersky VPN, if it is installed: the name of its adapter and its program. It can carry
	// one program alone ("Apps that always connect via VPN" in its split tunnelling), but it
	// has no way for another program to connect it or to change its settings: Pegaso ties
	// itself to its adapter, tells the user what to set there and opens it.
	static bool FindKaspersky(CString &sAdapter, CString &sProgram);
	static bool KasperskyInstalled();	// looked up every few seconds at most
	bool	IsKaspersky() const;	// tied to the adapter of Kaspersky VPN
	void	KasperskyDialog(CWnd *pParent);

	bool	IsActive() const							{ return !m_sAdapter.IsEmpty(); }
	// true while nothing may be transferred (adapter down and kill switch on)
	bool	IsBlocked() const							{ return m_bBlocked; }
	// address the sockets have to be bound to; empty if Pegaso is not tied to an adapter
	const CString& GetBindAddress() const				{ return m_sBindAddress; }

private:
	void	Refresh(bool bLog);

	CString	m_sAdapter;
	CString	m_sBindAddress;
	DWORD	m_dwLastCheck;
	bool	m_bKillSwitch;
	bool	m_bBlocked;
	bool	m_bUp;
};

extern CNetGuard theNetGuard;
