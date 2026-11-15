// Pegaso: QUIC as a socket layer between two clients which cannot call each other (low ID to
// low ID), over the UDP path their NATs were opened on.
//
// The dialect is the one eMuleAI introduced and aMule and eMule Qt speak (described in eMule
// Qt's docs/protocol/natt-quic.md): QUIC v1 inside "0xB2 0x01" datagrams of the client UDP
// port, TLS 1.3 with the ALPN "ed2k-ai-natt-quic-v1", one stream each way whose first 37
// bytes prove who is talking ("EAQN1" + user hash + expected hash). Once a link is up, the
// ordinary eD2K connection - hello, requests, blocks, RC4 obfuscation - runs inside it
// unchanged; no eD2K packet knows about it.
//
// This file is the transport: links, frames and timers. Who is to be reached, and when, is
// PegasoNat's business.
#pragma once

class CAsyncSocketEx;

namespace PegasoQuic
{
	// Frame types after the protocol byte 0xB2
	enum
	{
		FRAME_QUIC = 0x01,
		FRAME_CAPS = 0x02,
		FRAME_CAPS_ACK = 0x03,
		FRAME_KEY = 0xFF
	};

	class CLink;

	// Who uses a link as its byte stream (main thread).
	struct ILinkSink
	{
		virtual void OnLinkReadable() = 0;
		virtual void OnLinkWritable() = 0;
		virtual void OnLinkClosed() = 0;
	};

	bool	IsAvailable();	// the transport runs: TLS is set up and NAT traversal is not turned off

	// A datagram of the client UDP socket whose first byte is 0xB2 (after de-obfuscation).
	void	OnFrame(const BYTE *pData, UINT uLen, uint32 uIP, uint16 uPort);

	// Calls the peer. pDone(link, true) once both proofs are through, pDone(link, false) if it
	// did not come to that; the link is the caller's from then on until it is closed.
	typedef void (*LINKDONE)(CLink *pLink, bool bUp, void *pUser);
	CLink*	Dial(uint32 uIP, uint16 uPort, const uchar *pPeerHash, LINKDONE pDone, void *pUser);
	// Waits for the peer to call: for 20 seconds, from any port of that address.
	void	Expect(uint32 uIP, const uchar *pPeerHash, LINKDONE pDone, void *pUser);
	// Tells the peer what we can do (and learns its hash from the answer).
	void	SendCaps(uint32 uIP, uint16 uPort, const uchar *pPeerHash, const uchar *pFileHash);
	// Sends one frame; obfuscated if the hash of the peer is known.
	void	SendFrame(uint8 uType, const BYTE *pData, UINT uLen, uint32 uIP, uint16 uPort, const uchar *pPeerHash);

	void	Process();		// main thread, often: timers of all links
	void	Shutdown();

	// The byte stream of a link.
	int		LinkSend(CLink *pLink, const void *pData, int iLen);	// any thread; bytes taken, -1 = not now
	int		LinkReceive(CLink *pLink, void *pData, int iLen);		// main thread; -1 = nothing yet, 0 = closed
	void	LinkSetSink(CLink *pLink, ILinkSink *pSink);
	void	LinkClose(CLink *pLink);								// the link is gone after this
	uint32	LinkIP(const CLink *pLink);
	uint16	LinkPort(const CLink *pLink);
	const uchar* LinkPeerHash(const CLink *pLink);

	// Puts a link under an eMule socket: from then on the socket reads and writes the link.
	// The socket owns the link. Call before Create().
	bool	AttachToSocket(CAsyncSocketEx *pSocket, CLink *pLink);
}
