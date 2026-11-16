// Pegaso: IPv6 for eD2K and Kad sources.
//
// The wire format follows the "eD2K / Kademlia IPv6 Extensions" specification published by
// eMule Qt (docs/protocol/ipv6-spec.md in its repository), so that Pegaso, eMule Qt and the
// other clients which speak it understand each other:
//
//   hello            CT_MOD_MISCOPTIONS (0xAA, bit 0x04), CT_MOD_YOUR_IP (0xAD), CT_MOD_IP_V6 (0xAE)
//   server login     CT_MOD_IP_V6; OP_SERVERIDENT: ST_IPV6_STATUS (0xAB)
//   server sources   records with the client ID 0xFFFFFFFF carry 16 more bytes, the IPv6
//   server callback  OP_CALLBACKREQUESTED_IPV6 (0x26)
//   Kad              the "ip6" string tag of a published source
//
// What it is for: a client behind a carrier NAT or a router it cannot open has a low ID and
// can only be reached through callbacks - but with a public IPv6 it can simply be called.
//
// How it fits into eMule, whose code keeps the address of a client in 32 bits everywhere: an
// IPv6 address gets a stand-in from 240.0.0.0/4, a range that is never a real address and
// never goes on the wire. Ban list, client list and credits work with the stand-in as they
// do with any address; only connecting and displaying turn it back into the IPv6 address.
//
// IPv6 is not used while Pegaso is bound to a network adapter (VPN protection) or goes through
// a proxy: both are about IPv4 and IPv6 traffic would leave beside them.
//
// preferences.ini, [eMule]: IPv6Enabled (1), IPv6Address (our address, if Pegaso cannot know
// it), IPv6AssumeReachable (0: tell peers our address only once something proved that
// connections get in).
#pragma once
#include <in6addr.h>

class CUpDownClient;
class CPartFile;

#define CT_MOD_MISCOPTIONS		0xAA
#define ST_IPV6_STATUS			0xAB
#define CT_MOD_YOUR_IP			0xAD
#define CT_MOD_IP_V6			0xAE
#define MODMISC_IPV6			0x00000004
#define MODMISC_NATT_QUIC		0x00000010
#define OP_CALLBACKREQUESTED_IPV6 0x26
#define IPV6_SOURCE_SENTINEL	0xFFFFFFFFu
#define IPV6ST_REACHABLE		0x02
#define IPV6ST_PROBED			0x04

namespace Pegaso6
{
	// ---- stand-ins. Addresses as eMule keeps them (network byte order in a uint32).
	inline bool IsPseudo(uint32 uIP)	{ return (uIP & 0xFF) == 0xF0; }
	uint32	PseudoOf(const in6_addr &addr);			// 0 if the address is not one to talk to
	bool	AddrOf(uint32 uPseudo, in6_addr &rAddr);
	CString	Text(uint32 uPseudo);					// "2a01:4f8::1"
	uint32	PlaceholderID(uint32 uPseudo);			// a low ID for a client known by its IPv6 only
	bool	IsAcceptable(const in6_addr &addr);		// global unicast (anything unicast if LAN addresses are not filtered)
	bool	FromHex(const CString &sHex, in6_addr &rAddr);	// 32 hexadecimal digits, as in Kad
	CString	ToHex(const in6_addr &addr);

	// ---- our side
	bool	Enabled();
	bool	Allowed();								// IPv6 would not go round a VPN or a proxy (BitTorrent asks too)
	bool	Own(in6_addr &rAddr);					// our public IPv6, if there is one
	bool	PeerGateOpen();							// peers may be told: it is known to be reachable
	void	NoteInboundHello(uint32 uPeerPseudo);	// a peer got in over IPv6
	void	NoteServerVerdict(uint8 uStatus);		// ST_IPV6_STATUS of the server
	void	Process();								// main thread, regularly: address changes, the listener
	void	Stop();

	// ---- sources
	// A source known by its IPv6 (from a server or a callback request). NULL if it cannot be used.
	CUpDownClient* NewSource(CPartFile *pFile, const in6_addr &addr, uint16 uPort);
	// Kad: the "ip6" tag of the search result being handed to the download queue (0 = none).
	void	SetPendingKad(uint32 uPseudo);
	uint32	PendingKad();
}
