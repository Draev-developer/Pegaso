// Pegaso: low ID to low ID rendezvous, see PegasoNat.h
#include "stdafx.h"
#include <list>
#include "emule.h"
#include "PegasoNat.h"
#include "PegasoQuic.h"
#include "UpDownClient.h"
#include "ClientList.h"
#include "ClientUDPSocket.h"
#include "ListenSocket.h"
#include "ServerConnect.h"
#include "Server.h"
#include "Packets.h"
#include "Statistics.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Log.h"
#include "Opcodes.h"
#include "DownloadQueue.h"
#include "IPFilter.h"
#include "PartFile.h"
#include "SafeFile.h"
#include "kademlia/kademlia/Kademlia.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const DWORD REQUEST_INTERVAL = SEC2MS(10);	// the request to the server again, up to three times
	const DWORD BUDDY_INTERVAL = SEC2MS(5);		// the same through the buddy of the source
	const DWORD DIRECT_INTERVAL = SEC2MS(2);	// and straight to a source whose UDP port is open
	const int BUDDY_PUNCHES = 6;
	const int REQUEST_MAX = 3;
	const DWORD PUNCH_INTERVAL = SEC2MS(1);
	const DWORD PUNCH_TIME = SEC2MS(20);
	const DWORD DIAL_DELAY = SEC2MS(1);			// the downloader calls a second after it knows where
	const DWORD ATTEMPT_LIFE = SEC2MS(45);
	const DWORD KEEPALIVE_INTERVAL = SEC2MS(60);

	enum EPath { PATH_SERVER, PATH_BUDDY, PATH_DIRECT };

	struct SAttempt
	{
		bool	bRequester = false;
		EPath	ePath = PATH_SERVER;
		uint32	uGuessIP = 0;			// buddy path: the address the source published; its punch tells the port
		uint32	uBuddyIP = 0;
		uint16	uBuddyPort = 0;
		uchar	buddyID[16] = {};
		uchar	fileHash[16] = {};
		uint8	uCryptTarget = 0;		// direct path: the request goes obfuscated
		CUpDownClient *pClient = NULL;	// requester: whom we want; cleared if the client goes away
		uint32	uTargetID = 0;			// requester: the low ID asked for
		uchar	peerHash[16] = {};
		uint32	uIP = 0;				// the peer's public address, once known
		uint16	uUDPPort = 0;
		DWORD	dwCreated = 0;
		DWORD	dwLastRequest = 0;
		int		iRequests = 0;
		DWORD	dwEndpoint = 0;			// when the address became known
		DWORD	dwLastPunch = 0;
		bool	bDialed = false;
		bool	bDone = false;
	};
	std::list<SAttempt> s_attempts;		// a list: links hold pointers to the elements
	uint32	s_uServerFlags;
	DWORD	s_dwLastKeepAlive;
	uint8	s_uPendingKadNat;

	bool LoggedInWithHolePunch()
	{
		return (s_uServerFlags & SRV_TCPFLG_HOLEPUNCH) != 0 && theApp.serverconnect != NULL && theApp.serverconnect->IsConnected();
	}

	// The server learns our UDP port, as it looks from outside, from where this comes.
	void SendKeepAlive()
	{
		const CServer *pServer = theApp.serverconnect->GetCurrentServer();
		if (pServer == NULL || theApp.clientudp == NULL)
			return;
		Packet *pPacket = new Packet(OP_NAT_KEEPALIVE, 16, OP_EDONKEYPROT);
		md4cpy(pPacket->pBuffer, thePrefs.GetUserHash());
		theStats.AddUpDataOverheadServer(pPacket->size);
		theApp.clientudp->SendPacket(pPacket, pServer->GetIP(), (uint16)(pServer->GetPort() + 4), false, NULL, false, 0);
		s_dwLastKeepAlive = ::GetTickCount();
	}

	void SendRequest(SAttempt &attempt)
	{
		SendKeepAlive();	// right before, so that the server has our fresh port
		Packet *pPacket = new Packet(OP_NAT_REQUEST, 6);
		PokeUInt32(pPacket->pBuffer, attempt.uTargetID);
		PokeUInt16(pPacket->pBuffer + 4, thePrefs.GetUDPPort());
		theStats.AddUpDataOverheadServer(pPacket->size);
		theApp.serverconnect->SendPacket(pPacket);
		attempt.dwLastRequest = ::GetTickCount();
		++attempt.iRequests;
	}

	void SendPunch(SAttempt &attempt)
	{
		Packet *pPacket = new Packet(OP_HOLEPUNCH, 0, OP_EMULEPROT);
		theStats.AddUpDataOverheadOther(2);
		theApp.clientudp->SendPacket(pPacket, attempt.uIP, attempt.uUDPPort, false, NULL, false, 0);
		attempt.dwLastPunch = ::GetTickCount();
	}

	void SendPunchTo(uint32 uIP, uint16 uPort)
	{
		Packet *pPacket = new Packet(OP_HOLEPUNCH, 0, OP_EMULEPROT);
		theStats.AddUpDataOverheadOther(2);
		theApp.clientudp->SendPacket(pPacket, uIP, uPort, false, NULL, false, 0);
	}

	// Through the buddy of the source: it passes this on over the TCP connection it keeps with
	// the source, together with our address as it saw it. The file hash is empty and an
	// opcode follows instead, which is how the source tells it from a stock reask.
	void SendEnvelope(SAttempt &attempt)
	{
		const uint32 uPublicIP = theApp.GetPublicIP();
		Packet *pPacket = new Packet(OP_REASKCALLBACKUDP, uPublicIP != 0 ? 73 : 66, OP_EMULEPROT);
		BYTE *p = (BYTE*)pPacket->pBuffer;
		md4cpy(p, attempt.buddyID);
		md4clr(p + 16);
		p[32] = OP_RENDEZVOUS;
		md4cpy(p + 33, thePrefs.GetUserHash());
		p[49] = (uint8)((GetMyConnectOptions(true, false) & 0x07) | CONNECT_OPT_NATT_ENDPOINT_HINT);
		md4cpy(p + 50, attempt.fileHash);
		if (uPublicIP != 0) {	// what we believe; the buddy prefers what it sees
			PokeUInt32(p + 66, uPublicIP);
			PokeUInt16(p + 70, thePrefs.GetUDPPort());
			p[72] = 2;	// QUIC
		}
		theStats.AddUpDataOverheadOther(pPacket->size);
		theApp.clientudp->SendPacket(pPacket, attempt.uBuddyIP, attempt.uBuddyPort, false, NULL, false, 0);
		attempt.dwLastRequest = ::GetTickCount();
		++attempt.iRequests;
	}

	// Straight to a source whose UDP port is open: the stock request makes it call our TCP port,
	// which nobody can; with the QUIC bit it punches and waits for our QUIC call instead.
	void SendDirect(SAttempt &attempt)
	{
		Packet *pPacket = new Packet(OP_DIRECTCALLBACKREQ, 19, OP_EMULEPROT);
		BYTE *p = (BYTE*)pPacket->pBuffer;
		PokeUInt16(p, thePrefs.GetPort());
		md4cpy(p + 2, thePrefs.GetUserHash());
		p[18] = (uint8)((GetMyConnectOptions(true, false) & 0x07) | CONNECT_OPT_NATT_QUIC);
		theStats.AddUpDataOverheadOther(pPacket->size);
		theApp.clientudp->SendPacket(pPacket, attempt.uIP, attempt.uUDPPort, attempt.uCryptTarget != 0, attempt.peerHash, false, 0);
		attempt.dwLastRequest = ::GetTickCount();
		++attempt.iRequests;
	}

	// ---- the link came up, or did not
	void OnDialed(PegasoQuic::CLink *pLink, bool bUp, void *pUser)
	{
		SAttempt *pAttempt = (SAttempt*)pUser;
		pAttempt->bDone = true;
		CUpDownClient *pClient = pAttempt->pClient;
		pAttempt->pClient = NULL;
		if (!bUp)
			return;		// the client's connection attempt times out as any other
		if (pClient == NULL || !pClient->PegasoConnectOverLink(pLink))
			PegasoQuic::LinkClose(pLink);
	}

	void OnAccepted(PegasoQuic::CLink *pLink, bool bUp, void *pUser)
	{
		SAttempt *pAttempt = (SAttempt*)pUser;
		pAttempt->bDone = true;
		if (!bUp)
			return;
		// like a connection which came in over TCP: a socket without a client; the hello tells who it is
		CClientReqSocket *pSocket = new CClientReqSocket;
		if (!pSocket->Create()) {
			pSocket->Safe_Delete();
			PegasoQuic::LinkClose(pLink);
			return;
		}
		pSocket->PegasoNoOverlappedSend();
		if (!PegasoQuic::AttachToSocket(pSocket, pLink)) {
			pSocket->Safe_Delete();
			PegasoQuic::LinkClose(pLink);
		}
	}

	void EndpointKnown(SAttempt &attempt, uint32 uIP, uint16 uUDPPort, const uchar *pHash)
	{
		attempt.uIP = uIP;
		attempt.uUDPPort = uUDPPort;
		if (pHash != NULL)
			md4cpy(attempt.peerHash, pHash);
		attempt.dwEndpoint = ::GetTickCount();
		attempt.dwCreated = attempt.dwEndpoint;	// its time starts again
		SendPunch(attempt);
		PegasoQuic::SendCaps(uIP, uUDPPort, isnulmd4(attempt.peerHash) ? NULL : attempt.peerHash, NULL);
		if (!attempt.bRequester)
			PegasoQuic::Expect(uIP, isnulmd4(attempt.peerHash) ? NULL : attempt.peerHash, OnAccepted, &attempt);
		AddDebugLogLine(false, _T("NAT traversal: %s %s:%u"), attempt.bRequester ? _T("the source we asked for is at") : _T("asked for by"), (LPCTSTR)ipstr(uIP), uUDPPort);
	}

	// Somebody asked for us, through our buddy or straight: we open our NAT towards it and wait
	// for its QUIC call.
	bool AcceptNotice(uint32 uIP, uint16 uPort, const uchar *pHash, LPCTSTR pszHow)
	{
		if (!PegasoNat::IsOn() || uIP == 0 || uPort == 0 || !IsGoodIP(uIP) || theApp.clientudp == NULL)
			return false;
		if (theApp.ipfilter->IsFiltered(uIP) || theApp.clientlist->IsBannedClient(uIP))
			return false;
		for (const SAttempt &attempt : s_attempts)
			if (!attempt.bRequester && attempt.uIP == uIP)
				return true;	// a repeat
		if (s_attempts.size() >= 64)
			return false;
		AddDebugLogLine(false, _T("NAT traversal: %s"), pszHow);
		s_attempts.emplace_back();
		SAttempt &attempt = s_attempts.back();
		attempt.dwCreated = ::GetTickCount();
		EndpointKnown(attempt, uIP, uPort, pHash);
		return true;
	}

	bool KadFirewalled()
	{
		return theApp.IsFirewalled() && Kademlia::CKademlia::IsRunning();
	}
}

bool PegasoNat::IsOn()
{
	return PegasoQuic::IsAvailable();
}

void PegasoNat::OnServerFlags(uint32 uFlags)
{
	s_uServerFlags = uFlags;
	s_dwLastKeepAlive = 0;
	if ((uFlags & SRV_TCPFLG_HOLEPUNCH) != 0 && IsOn())
		AddDebugLogLine(false, _T("NAT traversal: this server introduces low ID clients to each other"));
}

bool PegasoNat::ServerIntroduces(uint32 uServerIP, uint16 uServerPort)
{
	return IsOn() && LoggedInWithHolePunch() && uServerIP != 0 && theApp.serverconnect->IsLocalServer(uServerIP, uServerPort);
}

namespace
{
	EPath PathFor(const CUpDownClient *pClient, bool &bAny)
	{
		bAny = false;
		if (pClient == NULL || !pClient->HasLowID() || !PegasoNat::IsOn())
			return PATH_SERVER;
		bAny = true;
		if (PegasoNat::ServerIntroduces(pClient->GetServerIP(), pClient->GetServerPort()))
			return PATH_SERVER;
		if (pClient->HasValidHash() && theApp.IsFirewalled() && theApp.clientudp != NULL && thePrefs.GetUDPPort() != 0) {
			if (pClient->PegasoNatQuic() && pClient->SupportsDirectUDPCallback() && pClient->GetConnectIP() != 0)
				return PATH_DIRECT;
			if (pClient->HasValidBuddyID() && pClient->GetBuddyIP() != 0 && pClient->GetBuddyPort() != 0
				&& pClient->GetRequestFile() != NULL && Kademlia::CKademlia::IsConnected())
				return PATH_BUDDY;
		}
		bAny = false;
		return PATH_SERVER;
	}
}

bool PegasoNat::CanReach(const CUpDownClient *pClient)
{
	bool bAny;
	PathFor(pClient, bAny);
	return bAny;
}

// A firewalled source Kad told us about while we are firewalled ourselves: stock eMule has no
// use for it. Types 3 and 5 have a buddy, type 6 takes UDP from anybody.
bool PegasoNat::KeepKadSource(uint8 uType)
{
	if (!IsOn() || theApp.clientudp == NULL || thePrefs.GetUDPPort() == 0)
		return false;
	return uType == 6 ? (s_uPendingKadNat & KADNAT_QUIC) != 0 : true;
}

void PegasoNat::SetPendingKadNat(uint8 uNat)
{
	s_uPendingKadNat = uNat;
}

uint8 PegasoNat::PendingKadNat()
{
	return s_uPendingKadNat;
}

bool PegasoNat::PublishKadNat()
{
	return IsOn() && theApp.IsFirewalled();
}

void PegasoNat::Request(CUpDownClient *pClient)
{
	for (const SAttempt &attempt : s_attempts)
		if (attempt.bRequester && attempt.pClient == pClient && !attempt.bDone)
			return;	// already on its way
	s_attempts.emplace_back();
	SAttempt &attempt = s_attempts.back();
	attempt.bRequester = true;
	attempt.pClient = pClient;
	attempt.uTargetID = pClient->GetUserIDHybrid();
	if (pClient->HasValidHash())
		md4cpy(attempt.peerHash, pClient->GetUserHash());
	attempt.dwCreated = ::GetTickCount();
	bool bAny;
	attempt.ePath = PathFor(pClient, bAny);
	switch (attempt.ePath) {
	case PATH_SERVER:
		SendRequest(attempt);
		break;
	case PATH_DIRECT:
		attempt.uCryptTarget = pClient->ShouldReceiveCryptUDPPackets() ? 1 : 0;
		attempt.uIP = pClient->GetConnectIP();
		attempt.uUDPPort = pClient->GetKadPort();
		SendDirect(attempt);
		EndpointKnown(attempt, attempt.uIP, attempt.uUDPPort, NULL);	// its UDP port is open: that is where it is
		break;
	case PATH_BUDDY:
		attempt.uBuddyIP = pClient->GetBuddyIP();
		attempt.uBuddyPort = pClient->GetBuddyPort();
		md4cpy(attempt.buddyID, pClient->GetBuddyID());
		md4cpy(attempt.fileHash, pClient->GetRequestFile()->GetFileHash());
		attempt.uGuessIP = pClient->GetConnectIP();
		if (attempt.uGuessIP != 0 && pClient->GetKadPort() != 0)	// opens our NAT before its punch comes
			for (int i = 0; i < BUDDY_PUNCHES; ++i)
				SendPunchTo(attempt.uGuessIP, pClient->GetKadPort());
		SendEnvelope(attempt);
		AddDebugLogLine(false, _T("NAT traversal: asking the buddy %s:%u of a firewalled source for it"), (LPCTSTR)ipstr(attempt.uBuddyIP), attempt.uBuddyPort);
	}
}

// Our buddy passed on a reask with an empty file hash: pData starts after that hash.
//   <OP_RENDEZVOUS><hash of the asker 16><its options 1><file hash 16>[<its ip 4><udp port 2><hint 1>]
bool PegasoNat::OnBuddyRendezvous(uint32 uIP, uint16 uPort, const BYTE *pData, uint32 uSize)
{
	if (uSize < 34 || pData[0] != OP_RENDEZVOUS)
		return false;
	// where the buddy saw it wins; what it says about itself only if that is of no use
	if ((!IsGoodIP(uIP) || uPort == 0) && uSize >= 40) {
		uIP = PeekUInt32(pData + 34);
		uPort = PeekUInt16(pData + 38);
	}
	return AcceptNotice(uIP, uPort, pData + 1, _T("our buddy says somebody wants to get through to us"));
}

// We are the buddy and just passed such a reask on: a hint for the asker where the client we
// serve is, and a punch. pPacket is the datagram as it came, <buddy id 16><empty 16>...
void PegasoNat::OnBuddyRelay(uint32 uIP, uint16 uPort, const BYTE *pPacket, uint32 uSize, const CUpDownClient *pServed)
{
	if (uSize < 66 || !isnulmd4(pPacket + 16) || pPacket[32] != OP_RENDEZVOUS || (pPacket[49] & CONNECT_OPT_NATT_ENDPOINT_HINT) == 0)
		return;
	const uint16 uServedPort = pServed->GetKadPort() != 0 ? pServed->GetKadPort() : pServed->GetUDPPort();
	if (uServedPort == 0 || pServed->GetIP() == 0 || !IsGoodIP(uIP) || theApp.ipfilter->IsFiltered(uIP))
		return;
	Packet *pHint = new Packet(OP_NATT_ENDPOINT_HINT, 56, OP_EMULEPROT);
	BYTE *p = (BYTE*)pHint->pBuffer;
	p[0] = 1;
	md4cpy(p + 1, pServed->GetUserHash());
	md4cpy(p + 17, pServed->GetBuddyID());	// as the asker sent it
	md4cpy(p + 33, pPacket + 50);
	PokeUInt32(p + 49, pServed->GetIP());
	PokeUInt16(p + 53, uServedPort);
	p[55] = (uint8)((pServed->SupportsCryptLayer() ? 1 : 0) | (pServed->RequestsCryptLayer() ? 2 : 0) | (pServed->RequiresCryptLayer() ? 4 : 0));
	theStats.AddUpDataOverheadOther(pHint->size);
	theApp.clientudp->SendPacket(pHint, uIP, uPort, false, NULL, false, 0);
	SendPunchTo(uIP, uPort);
}

// The buddy of a source we asked for tells where the source is.
void PegasoNat::OnEndpointHint(const BYTE *pData, uint32 uSize, uint32 uFromIP, uint16 uFromPort)
{
	if (uSize < 56 || pData[0] != 1)
		return;
	const uint32 uIP = PeekUInt32(pData + 49);
	const uint16 uPort = PeekUInt16(pData + 53);
	if (!IsGoodIP(uIP) || uPort == 0)
		return;
	for (SAttempt &attempt : s_attempts)
		if (attempt.bRequester && attempt.ePath == PATH_BUDDY && !attempt.bDone && attempt.dwEndpoint == 0
			&& attempt.uBuddyIP == uFromIP && attempt.uBuddyPort == uFromPort
			&& md4equ(attempt.buddyID, pData + 17) && md4equ(attempt.peerHash, pData + 1)
			&& (attempt.uGuessIP == 0 || attempt.uGuessIP == uIP))
		{
			EndpointKnown(attempt, uIP, uPort, NULL);
			return;
		}
}

// A direct callback request with the QUIC bit, while we cannot be called over TCP.
bool PegasoNat::OnDirectCallback(uint32 uIP, uint16 uPort, const uchar *pHash, uint8 uOptions)
{
	if ((uOptions & CONNECT_OPT_NATT_QUIC) == 0 || !KadFirewalled())
		return false;
	return AcceptNotice(uIP, uPort, pHash, _T("a firewalled client asks straight for a QUIC call"));
}

void PegasoNat::OnServerPacket(uint8 uOpcode, const BYTE *pData, uint32 uSize)
{
	if (!IsOn())
		return;
	if (uOpcode == OP_NAT_FAILED) {
		if (uSize < 5)
			return;
		const uint32 uTargetID = PeekUInt32(pData);
		for (SAttempt &attempt : s_attempts)
			if (attempt.bRequester && attempt.uTargetID == uTargetID && attempt.dwEndpoint == 0 && !attempt.bDone) {
				attempt.bDone = true;
				attempt.pClient = NULL;
				AddDebugLogLine(false, _T("NAT traversal: the server cannot introduce client %u (reason %u)"), uTargetID, pData[4]);
			}
		return;
	}
	if (uOpcode != OP_NAT_ENDPOINT || uSize < 25)
		return;
	const uint32 uIP = PeekUInt32(pData);
	const uint16 uUDPPort = PeekUInt16(pData + 6);
	const uchar *pHash = pData + 8;
	const bool bHasID = uSize >= 29;
	const uint32 uPeerID = bHasID ? PeekUInt32(pData + 25) : 0;
	if (uIP == 0 || uUDPPort == 0)
		return;

	// the answer to a request of ours: by the ID of the peer, else by its hash, else the only one waiting
	SAttempt *pMine = NULL;
	int iWaiting = 0;
	for (SAttempt &attempt : s_attempts) {
		if (!attempt.bRequester || attempt.bDone || attempt.dwEndpoint != 0)
			continue;
		++iWaiting;
		if (bHasID ? attempt.uTargetID == uPeerID : (!isnulmd4(attempt.peerHash) && md4equ(attempt.peerHash, pHash)))
			pMine = &attempt;
	}
	if (pMine == NULL && !bHasID && iWaiting == 1)
		for (SAttempt &attempt : s_attempts)
			if (attempt.bRequester && !attempt.bDone && attempt.dwEndpoint == 0 && isnulmd4(attempt.peerHash))
				pMine = &attempt;
	if (pMine != NULL) {
		EndpointKnown(*pMine, uIP, uUDPPort, pHash);
		return;
	}
	// anything else: somebody asked for us
	for (const SAttempt &attempt : s_attempts)
		if (!attempt.bRequester && !attempt.bDone && attempt.uIP == uIP)
			return;	// a repeat
	if (s_attempts.size() >= 64)
		return;
	s_attempts.emplace_back();
	SAttempt &attempt = s_attempts.back();
	attempt.dwCreated = ::GetTickCount();
	EndpointKnown(attempt, uIP, uUDPPort, pHash);
}

void PegasoNat::OnHolePunch(uint32 uIP, uint16 uPort)
{
	// from the address of a peer we are getting together with, but another port: its NAT gave
	// it that one towards us, so that is where it is
	for (SAttempt &attempt : s_attempts) {
		// through a buddy: the punch of the source is how we learn its port
		if (attempt.bRequester && attempt.ePath == PATH_BUDDY && !attempt.bDone && attempt.dwEndpoint == 0 && attempt.uGuessIP == uIP) {
			EndpointKnown(attempt, uIP, uPort, NULL);
			continue;
		}
		if (!attempt.bDone && !attempt.bDialed && attempt.uIP == uIP && attempt.dwEndpoint != 0 && attempt.uUDPPort != uPort) {
			AddDebugLogLine(false, _T("NAT traversal: %s answers from port %u instead of %u"), (LPCTSTR)ipstr(uIP), uPort, attempt.uUDPPort);
			attempt.uUDPPort = uPort;
		}
	}
}

void PegasoNat::OnClientGone(const CUpDownClient *pClient)
{
	for (SAttempt &attempt : s_attempts)
		if (attempt.pClient == pClient)
			attempt.pClient = NULL;
}

void PegasoNat::Process()
{
	const DWORD dwNow = ::GetTickCount();
	if (LoggedInWithHolePunch() && IsOn() && theApp.IsFirewalled() && (s_dwLastKeepAlive == 0 || dwNow - s_dwLastKeepAlive >= KEEPALIVE_INTERVAL))
		SendKeepAlive();

	for (auto it = s_attempts.begin(); it != s_attempts.end();) {
		SAttempt &attempt = *it;
		if (attempt.bDone && !attempt.bDialed) {
			it = s_attempts.erase(it);
			continue;
		}
		if (attempt.bDone || dwNow - attempt.dwCreated > ATTEMPT_LIFE) {
			// a link which is still being set up holds a pointer to the attempt: it is told first
			if (attempt.bDone || !attempt.bDialed || dwNow - attempt.dwCreated > ATTEMPT_LIFE + SEC2MS(20)) {
				it = s_attempts.erase(it);
				continue;
			}
		}
		if (attempt.bRequester && attempt.pClient == NULL && !attempt.bDialed) {
			it = s_attempts.erase(it);	// nobody wants it any more
			continue;
		}
		if (attempt.dwEndpoint == 0) {
			if (attempt.bRequester && attempt.ePath == PATH_BUDDY) {
				if (attempt.iRequests < REQUEST_MAX && dwNow - attempt.dwLastRequest >= BUDDY_INTERVAL)
					SendEnvelope(attempt);
			} else if (attempt.bRequester && dwNow - attempt.dwLastRequest >= REQUEST_INTERVAL) {
				if (attempt.iRequests >= REQUEST_MAX || !LoggedInWithHolePunch()) {
					it = s_attempts.erase(it);
					continue;
				}
				SendRequest(attempt);
			}
		} else {
			// a lost request leaves the source without knowing that we are about to call
			if (attempt.bRequester && attempt.ePath == PATH_DIRECT && !attempt.bDone && attempt.iRequests < REQUEST_MAX && dwNow - attempt.dwLastRequest >= DIRECT_INTERVAL)
				SendDirect(attempt);
			if (dwNow - attempt.dwEndpoint < PUNCH_TIME && dwNow - attempt.dwLastPunch >= PUNCH_INTERVAL)
				SendPunch(attempt);
			if (attempt.bRequester && !attempt.bDialed && dwNow - attempt.dwEndpoint >= DIAL_DELAY) {
				attempt.bDialed = true;
				if (PegasoQuic::Dial(attempt.uIP, attempt.uUDPPort, isnulmd4(attempt.peerHash) ? NULL : attempt.peerHash, OnDialed, &attempt) == NULL)
					attempt.bDone = true;
			}
		}
		++it;
	}
}
