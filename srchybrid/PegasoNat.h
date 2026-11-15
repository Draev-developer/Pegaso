// Pegaso: two clients which cannot call each other (low ID to low ID) get together.
//
// eMule gives up on such a pair ("LowID->LowID"). Here somebody both can talk to tells each
// of them the public UDP address of the other; both send a few empty datagrams there
// (OP_HOLEPUNCH), which opens their NATs for each other, and over that path the downloader
// calls the source with QUIC (PegasoQuic.h). The eD2K connection then runs inside as always.
//
// Who introduces them, in this version: the eD2K server both are logged in to, if it speaks
// the hole punch protocol of ed2k-server 0.9.84 and later (it says so with the flag 0x20000
// when we log in): TCP 0x60 request, 0x61 answer to both sides, 0x62 refusal, and a UDP
// keep-alive 0x9F from which the server learns our UDP port as the outside world sees it.
// The wire format follows eMule Qt's docs/protocol/natt-rendezvous.md.
//
// preferences.ini, [eMule]: NatTraversal (1).
#pragma once

class CUpDownClient;

#define OP_HOLEPUNCH			0xA1	// client UDP, empty
#define OP_NAT_REQUEST			0x60	// client -> server TCP: <target id 4><our udp port 2>
#define OP_NAT_ENDPOINT			0x61	// server -> both: <ip 4><tcp 2><udp 2><hash 16><role 1>[<peer id 4>]
#define OP_NAT_FAILED			0x62	// server -> requester: <target id 4><reason 1>
#define OP_NAT_KEEPALIVE		0x9F	// client -> server UDP (TCP port + 4): <user hash 16>
#define SRV_TCPFLG_HOLEPUNCH	0x00020000
#define CT_EMULE_UDPPORTS_LOGIN	0xF9

namespace PegasoNat
{
	bool	IsOn();											// NAT traversal runs here
	void	OnServerFlags(uint32 uFlags);					// of the server we just logged in to
	bool	ServerIntroduces(uint32 uServerIP, uint16 uServerPort);	// a low ID source of that server can be reached
	bool	CanReach(const CUpDownClient *pClient);
	void	Request(CUpDownClient *pClient);				// asks for the client; it is connected once the link is up
	void	OnServerPacket(uint8 uOpcode, const BYTE *pData, uint32 uSize);
	void	OnHolePunch(uint32 uIP, uint16 uPort);
	void	OnClientGone(const CUpDownClient *pClient);
	void	Process();										// main thread, regularly
}
