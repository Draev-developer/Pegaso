// Pegaso: opening the ports on the router with NAT-PMP and PCP, next to eMule's UPnP.
//
// eMule only speaks UPnP IGD. Many routers answer NAT-PMP (RFC 6886) or its successor PCP
// (RFC 6887) instead, or as well: one small UDP question to the gateway. This asks the
// gateway for the TCP and the UDP port whenever eMule starts its UPnP set-up, and:
//
//   - a mapping only counts if the router gave exactly the port that was asked for (eD2K and
//     Kad announce that port; any other one is of no use and is given back);
//   - mappings expire, so they are renewed at half of their lifetime, and released at exit;
//   - if the router itself has no public address - it sits behind another router or a
//     carrier NAT (CGNAT) - no port can be opened from here: the user is told so, once,
//     instead of being left with a low ID and no explanation.
//
// The gateway is the next hop of the default route (of the bound adapter, if Pegaso is bound
// to one). For tests, "PortMapGateway=<address>" in preferences.ini overrides it.
#pragma once

class CPegasoPortMap
{
public:
	void	Start(uint16 uTCPPort, uint16 uUDPPort);	// main thread; the gateway is asked in the background
	void	Process();									// main thread, called regularly: reports, renews
	void	Stop();										// at shutdown: the mappings are released
	bool	IsMapped() const;							// the ports are open through NAT-PMP or PCP
	static bool HasFixedGateway();						// "PortMapGateway" is set: only that address is asked, UPnP is left out

	// Any thread: something found out that the router has no public address (also eMule's UPnP).
	void	NoteNoPublicAddress(const CStringA &sWanIP);
	// Any thread: UPnP found the port forwarded to another device.
	void	NoteConflict(uint16 uPort, bool bTCP, const CStringA &sOtherIP);
};

extern CPegasoPortMap thePortMap;

// Private, carrier NAT (100.64/10), link-local, loopback or no address at all. Host byte order.
bool	PegasoIsNotPublicAddress(uint32 uIP);
