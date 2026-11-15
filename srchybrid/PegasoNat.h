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
// Through Kad, for sources no server knows of:
//  - the buddy of a firewalled source (Kad source types 3 and 5). We send it a reask
//    (OP_REASKCALLBACKUDP) with an empty file hash and OP_RENDEZVOUS inside; any stock buddy
//    passes it on to the source with our address as it saw it, the source punches towards us
//    and its punch tells us where it is. A buddy which knows this also sends us a hint.
//  - straight, to a firewalled source whose UDP port is open (type 6) and which says in its
//    Kad record ("nat" tag) that it does QUIC: OP_DIRECTCALLBACKREQ with the option 0x40.
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
#define OP_RENDEZVOUS			0xA0	// inside a reask passed on by a buddy, where the file hash is empty
#define OP_NATT_ENDPOINT_HINT	0xAA	// client UDP, buddy -> asker: <1><target hash 16><buddy id 16><file hash 16><ip 4><port 2><opts 1>
#define CONNECT_OPT_NATT_ENDPOINT_HINT	0x20
#define CONNECT_OPT_NATT_QUIC	0x40
#define KADNAT_TAG				"nat"	// Kad source record, uint8
#define KADNAT_QUIC				0x01

namespace PegasoNat
{
	bool	IsOn();											// NAT traversal runs here
	void	OnServerFlags(uint32 uFlags);					// of the server we just logged in to
	bool	ServerIntroduces(uint32 uServerIP, uint16 uServerPort);	// a low ID source of that server can be reached
	bool	CanReach(const CUpDownClient *pClient);
	void	Request(CUpDownClient *pClient);				// asks for the client; it is connected once the link is up
	void	OnServerPacket(uint8 uOpcode, const BYTE *pData, uint32 uSize);
	void	OnHolePunch(uint32 uIP, uint16 uPort);
	// Kad
	bool	KeepKadSource(uint8 uType);						// firewalled ourselves: a firewalled Kad source is of use
	void	SetPendingKadNat(uint8 uNat);					// the "nat" tag of the Kad source being handed over
	uint8	PendingKadNat();
	bool	PublishKadNat();								// our own Kad source records carry the tag
	bool	OnBuddyRendezvous(uint32 uIP, uint16 uPort, const BYTE *pData, uint32 uSize);
	void	OnBuddyRelay(uint32 uIP, uint16 uPort, const BYTE *pPacket, uint32 uSize, const CUpDownClient *pServed);
	void	OnEndpointHint(const BYTE *pData, uint32 uSize, uint32 uFromIP, uint16 uFromPort);
	bool	OnDirectCallback(uint32 uIP, uint16 uPort, const uchar *pHash, uint8 uOptions);
	void	OnClientGone(const CUpDownClient *pClient);
	void	Process();										// main thread, regularly
}
