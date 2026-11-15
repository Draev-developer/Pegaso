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

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const DWORD REQUEST_INTERVAL = SEC2MS(10);	// the request again, up to three times
	const int REQUEST_MAX = 3;
	const DWORD PUNCH_INTERVAL = SEC2MS(1);
	const DWORD PUNCH_TIME = SEC2MS(20);
	const DWORD DIAL_DELAY = SEC2MS(1);			// the downloader calls a second after it knows where
	const DWORD ATTEMPT_LIFE = SEC2MS(45);
	const DWORD KEEPALIVE_INTERVAL = SEC2MS(60);

	struct SAttempt
	{
		bool	bRequester = false;
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

bool PegasoNat::CanReach(const CUpDownClient *pClient)
{
	return pClient != NULL && pClient->HasLowID() && ServerIntroduces(pClient->GetServerIP(), pClient->GetServerPort());
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
	SendRequest(attempt);
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
	for (SAttempt &attempt : s_attempts)
		if (!attempt.bDone && !attempt.bDialed && attempt.uIP == uIP && attempt.dwEndpoint != 0 && attempt.uUDPPort != uPort) {
			AddDebugLogLine(false, _T("NAT traversal: %s answers from port %u instead of %u"), (LPCTSTR)ipstr(uIP), uPort, attempt.uUDPPort);
			attempt.uUDPPort = uPort;
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
			if (attempt.bRequester && dwNow - attempt.dwLastRequest >= REQUEST_INTERVAL) {
				if (attempt.iRequests >= REQUEST_MAX || !LoggedInWithHolePunch()) {
					it = s_attempts.erase(it);
					continue;
				}
				SendRequest(attempt);
			}
		} else {
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
