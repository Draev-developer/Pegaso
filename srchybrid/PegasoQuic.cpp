// Pegaso: the QUIC NAT traversal transport, see PegasoQuic.h
#include "stdafx.h"
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#define NGTCP2_STATICLIB
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/rand.h>
#include "emule.h"
#include "PegasoQuic.h"
#include "AsyncSocketEx.h"
#include "ClientUDPSocket.h"
#include "Packets.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Log.h"
#include "Opcodes.h"
#include "Statistics.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


namespace PegasoQuic
{
	namespace
	{
		const char ALPN[] = "ed2k-ai-natt-quic-v1";
		const char SNI[] = "emuleai-natt";
		const size_t CID_LEN = 8;
		const size_t MAX_SEND_DATAGRAM = 1200;
		const uint64 STREAM_WINDOW = 512 * 1024;
		const uint64 CONN_WINDOW = 4 * 1024 * 1024;
		const uint64 MS = 1000000ull;	// nanoseconds
		const uint64 IDLE_TIMEOUT = 60000 * MS;
		const uint64 KEEP_ALIVE = 20000 * MS;
		const uint64 HANDSHAKE_TIMEOUT = 10000 * MS;
		const uint64 HANDSHAKE_RESEND = 250 * MS;
		const int HANDSHAKE_RESEND_MAX = 12;
		const uint64 PLAIN_FALLBACK = 2000 * MS;
		const uint64 KEY_FRAME_INTERVAL = 10000 * MS;
		const uint64 EXPECT_TTL = 20000 * MS;
		const size_t MAX_LINKS = 64;
		const size_t PROOF_SIZE = 5 + 16 + 16;
		const size_t CAPS_SIZE = 54;
		const size_t BLOCK = 64 * 1024;			// of the send buffer
		const uint64 MAX_UNACKED = 512 * 1024;	// how much a link takes before it says "not now"

		uint64 NowNs()
		{
			static LARGE_INTEGER freq;
			if (freq.QuadPart == 0)
				::QueryPerformanceFrequency(&freq);
			LARGE_INTEGER now;
			::QueryPerformanceCounter(&now);
			return (uint64)(now.QuadPart / freq.QuadPart) * 1000000000ull + (uint64)(now.QuadPart % freq.QuadPart) * 1000000000ull / (uint64)freq.QuadPart;
		}

		void Random(uint8_t *pDest, size_t uLen)
		{
			RAND_bytes(pDest, (int)uLen);
		}

		// ---- TLS
		SSL_CTX *s_pClientCtx, *s_pServerCtx;
		bool s_bTlsTried, s_bTlsOK;

		int SelectAlpn(SSL*, const unsigned char **ppOut, unsigned char *pOutLen, const unsigned char *pIn, unsigned int uInLen, void*)
		{
			for (unsigned int i = 0; i < uInLen;) {
				const unsigned int n = pIn[i];
				if (i + 1 + n > uInLen)
					break;
				if (n == sizeof ALPN - 1 && memcmp(pIn + i + 1, ALPN, n) == 0) {
					*ppOut = pIn + i + 1;
					*pOutLen = (unsigned char)n;
					return SSL_TLSEXT_ERR_OK;
				}
				i += 1 + n;
			}
			return SSL_TLSEXT_ERR_ALERT_FATAL;
		}

		bool CommonSetup(SSL_CTX *pCtx)
		{
			// no hybrid post-quantum key share: it does not fit in one Initial and the other stacks do not know it
			return SSL_CTX_set_min_proto_version(pCtx, TLS1_3_VERSION) == 1 && SSL_CTX_set_max_proto_version(pCtx, TLS1_3_VERSION) == 1
				&& SSL_CTX_set_ciphersuites(pCtx, "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256") == 1
				&& SSL_CTX_set1_groups_list(pCtx, "X25519:P-256") == 1;
		}

		// Nobody checks the certificate (who the peer is comes from the proof on the stream); it only has to be there.
		bool InstallSelfSigned(SSL_CTX *pCtx)
		{
			EVP_PKEY *pKey = EVP_EC_gen("P-256");
			X509 *pCert = X509_new();
			bool bOK = false;
			if (pKey != NULL && pCert != NULL) {
				X509_set_version(pCert, 2);
				ASN1_INTEGER_set(X509_get_serialNumber(pCert), 1);
				X509_gmtime_adj(X509_getm_notBefore(pCert), -24 * 3600);
				X509_gmtime_adj(X509_getm_notAfter(pCert), 365 * 24 * 3600);
				X509_set_pubkey(pCert, pKey);
				X509_NAME *pName = X509_get_subject_name(pCert);
				X509_NAME_add_entry_by_txt(pName, "CN", MBSTRING_ASC, (const unsigned char*)"ed2k-natt", -1, -1, 0);
				X509_set_issuer_name(pCert, pName);
				bOK = X509_sign(pCert, pKey, EVP_sha256()) > 0
					&& SSL_CTX_use_certificate(pCtx, pCert) == 1 && SSL_CTX_use_PrivateKey(pCtx, pKey) == 1;
			}
			X509_free(pCert);
			EVP_PKEY_free(pKey);
			return bOK;
		}

		bool InitTls()
		{
			if (s_bTlsTried)
				return s_bTlsOK;
			s_bTlsTried = true;
			if (ngtcp2_crypto_ossl_init() != 0)
				return false;
			s_pClientCtx = SSL_CTX_new(TLS_client_method());
			s_pServerCtx = SSL_CTX_new(TLS_server_method());
			if (s_pClientCtx == NULL || s_pServerCtx == NULL || !CommonSetup(s_pClientCtx) || !CommonSetup(s_pServerCtx))
				return false;
			SSL_CTX_set_verify(s_pClientCtx, SSL_VERIFY_NONE, NULL);
			SSL_CTX_set_options(s_pClientCtx, SSL_OP_NO_TICKET);
			SSL_CTX_set_session_cache_mode(s_pClientCtx, SSL_SESS_CACHE_OFF);
			SSL_CTX_set_options(s_pServerCtx, SSL_OP_NO_TICKET | SSL_OP_CIPHER_SERVER_PREFERENCE);
			SSL_CTX_set_num_tickets(s_pServerCtx, 0);
			SSL_CTX_set_session_cache_mode(s_pServerCtx, SSL_SESS_CACHE_OFF);
			SSL_CTX_set_verify(s_pServerCtx, SSL_VERIFY_NONE, NULL);
			SSL_CTX_set_alpn_select_cb(s_pServerCtx, SelectAlpn, NULL);
			s_bTlsOK = InstallSelfSigned(s_pServerCtx);
			if (!s_bTlsOK)
				ERR_clear_error();
			return s_bTlsOK;
		}

		// ---- what is known about endpoints: the hash of who is there (to obfuscate towards it)
		struct SPeerKey
		{
			uchar	hash[16];
			uint64	uSeen;
		};
		std::map<uint64, SPeerKey> s_keys;				// (ip, port) -> hash
		std::map<uint32, DWORD> s_capsAnswered;			// ip -> when a CAPS was last answered

		uint64 EndpointKey(uint32 uIP, uint16 uPort)
		{
			return ((uint64)uIP << 16) | uPort;
		}

		const uchar* KnownHash(uint32 uIP, uint16 uPort)
		{
			const auto it = s_keys.find(EndpointKey(uIP, uPort));
			return it != s_keys.end() ? it->second.hash : NULL;
		}

		void LearnHash(uint32 uIP, uint16 uPort, const uchar *pHash, bool bOverwrite)
		{
			if (isnulmd4(pHash))
				return;
			SPeerKey &key = s_keys[EndpointKey(uIP, uPort)];
			if (bOverwrite || isnulmd4(key.hash))
				md4cpy(key.hash, pHash);
			key.uSeen = NowNs();
		}

		struct SExpect
		{
			uint32	uIP;
			uchar	hash[16];
			bool	bKnown;
			LINKDONE pDone;
			void	*pUser;
			uint64	uUntil;
		};
		std::vector<SExpect> s_expected;
	}

	///////////////////////////////////////////////////////////////////////////////////////
	// A link

	class CLink
	{
	public:
		enum EState { HANDSHAKE, PROVING, UP, CLOSED };

		bool	m_bServer = false;
		uint32	m_uIP = 0;
		uint16	m_uPort = 0;
		uchar	m_peerHash[16] = {};
		bool	m_bPeerKnown = false;
		EState	m_state = HANDSHAKE;
		ngtcp2_conn *m_pConn = NULL;
		SSL		*m_pSsl = NULL;
		ngtcp2_crypto_ossl_ctx *m_pOssl = NULL;
		ngtcp2_crypto_conn_ref m_ref = {};
		ngtcp2_cid m_scid = {};
		std::vector<std::string> m_cids;	// the connection IDs datagrams for this link carry
		int64_t	m_iLocalStream = -1;
		int64_t	m_iRemoteStream = -1;

		// what goes out: fixed blocks, because the QUIC stack keeps pointing at the bytes until they are acknowledged
		std::mutex m_sendLock;
		std::deque<std::unique_ptr<BYTE[]> > m_blocks;
		uint64	m_uBlockBase = 0;	// stream offset of the first block
		uint64	m_uAcked = 0;
		uint64	m_uSubmitted = 0;
		uint64	m_uEnd = 0;
		bool	m_bWriterWaits = false;

		// what came in
		std::string m_recv;
		BYTE	m_proof[PROOF_SIZE];
		size_t	m_uProofGot = 0;
		bool	m_bProofOK = false;
		bool	m_bProofQueued = false;
		bool	m_bPeerFinished = false;

		ILinkSink *m_pSink = NULL;
		LINKDONE m_pDone = NULL;
		void	*m_pUser = NULL;
		bool	m_bHandedOver = false;	// the caller has it
		bool	m_bDead = false;		// to be deleted by Process()
		bool	m_bNotifyRead = false, m_bNotifyWrite = false, m_bNotifyClosed = false;

		uint64	m_uStart = 0;
		bool	m_bHeardPeer = false;
		int		m_iResendLeft = HANDSHAKE_RESEND_MAX;
		uint64	m_uNextResend = 0;
		std::string m_lastLong;
		uint64	m_uLastKeyFrame = 0;
		bool	m_bPlainTurn = false;
		bool	m_bKeepPlain = false;
		bool	m_bModeSet = false;
		CString	m_sWhy;

		~CLink()
		{
			if (m_pConn != NULL)
				ngtcp2_conn_del(m_pConn);
			if (m_pSsl != NULL) {
				SSL_set_app_data(m_pSsl, NULL);
				SSL_free(m_pSsl);
			}
			if (m_pOssl != NULL)
				ngtcp2_crypto_ossl_ctx_del(m_pOssl);
		}
	};

	namespace
	{
		std::set<CLink*> s_links;
		std::map<std::string, CLink*> s_byCid;
		HWND s_hTimerWnd;

		std::string CidKey(const ngtcp2_cid &cid)
		{
			return std::string((const char*)cid.data, cid.datalen);
		}

		void AddCid(CLink *pLink, const ngtcp2_cid &cid)
		{
			const std::string sKey(CidKey(cid));
			s_byCid[sKey] = pLink;
			pLink->m_cids.push_back(sKey);
		}

		void Forget(CLink *pLink)
		{
			for (const std::string &sKey : pLink->m_cids) {
				const auto it = s_byCid.find(sKey);
				if (it != s_byCid.end() && it->second == pLink)
					s_byCid.erase(it);
			}
			pLink->m_cids.clear();
		}

		void MakePath(const CLink *pLink, sockaddr_in &rRemote, sockaddr_in &rLocal, ngtcp2_path &rPath)
		{
			memset(&rRemote, 0, sizeof rRemote);
			rRemote.sin_family = AF_INET;
			rRemote.sin_addr.s_addr = pLink->m_uIP;
			rRemote.sin_port = htons(pLink->m_uPort);
			memset(&rLocal, 0, sizeof rLocal);
			rLocal.sin_family = AF_INET;
			rLocal.sin_port = htons(thePrefs.GetUDPPort());
			rPath.local.addr = (ngtcp2_sockaddr*)&rLocal;
			rPath.local.addrlen = sizeof rLocal;
			rPath.remote.addr = (ngtcp2_sockaddr*)&rRemote;
			rPath.remote.addrlen = sizeof rRemote;
			rPath.user_data = NULL;
		}

		// One datagram for the peer of a link: obfuscated if we know whom we talk to.
		void SendDatagram(CLink *pLink, const uint8_t *pData, size_t uLen, uint64 uNow)
		{
			const uchar *pHash = pLink->m_bPeerKnown ? pLink->m_peerHash : KnownHash(pLink->m_uIP, pLink->m_uPort);
			if (!pLink->m_bModeSet) {
				// decided once: what the peer could read when the link was made has to stay readable
				// (the obfuscation key holds our public address, which may only become known later)
				pLink->m_bModeSet = true;
				pLink->m_bKeepPlain = pHash == NULL || !thePrefs.IsCryptLayerEnabled() || theApp.GetPublicIP() == 0;
			}
			bool bPlain = pHash == NULL || pLink->m_bKeepPlain;
			if (!bPlain && !pLink->m_bHeardPeer && uNow - pLink->m_uStart > PLAIN_FALLBACK) {
				// nothing came back: perhaps the peer cannot read what we obfuscate (it knows another
				// address of ours). Every other datagram goes plain from now on.
				pLink->m_bPlainTurn = !pLink->m_bPlainTurn;
				bPlain = pLink->m_bPlainTurn;
			}
			// so that a peer which obfuscates only once it knows our hash can do so
			if ((pData[0] & 0x80) != 0 && (pLink->m_uLastKeyFrame == 0 || uNow - pLink->m_uLastKeyFrame >= KEY_FRAME_INTERVAL) && pHash != NULL) {
				pLink->m_uLastKeyFrame = uNow;
				SendFrame(FRAME_KEY, thePrefs.GetUserHash(), 16, pLink->m_uIP, pLink->m_uPort, pHash);
			}
			SendFrame(FRAME_QUIC, pData, (UINT)uLen, pLink->m_uIP, pLink->m_uPort, bPlain ? NULL : pHash);
			if ((pData[0] & 0x80) != 0)
				pLink->m_lastLong.assign((const char*)pData, uLen);
		}

		void Close(CLink *pLink, const CString &sWhy, bool bTellPeer)
		{
			if (pLink->m_state == CLink::CLOSED)
				return;
			if (bTellPeer && pLink->m_pConn != NULL && !ngtcp2_conn_in_closing_period2(pLink->m_pConn) && !ngtcp2_conn_in_draining_period2(pLink->m_pConn)) {
				uint8_t buf[MAX_SEND_DATAGRAM];
				ngtcp2_ccerr ccerr;
				ngtcp2_ccerr_default(&ccerr);
				ngtcp2_path_storage ps;
				ngtcp2_path_storage_zero(&ps);
				ngtcp2_pkt_info pi = {};
				const ngtcp2_ssize n = ngtcp2_conn_write_connection_close(pLink->m_pConn, &ps.path, &pi, buf, sizeof buf, &ccerr, NowNs());
				if (n > 0)
					SendDatagram(pLink, buf, (size_t)n, NowNs());
			}
			const bool bWasUp = pLink->m_state == CLink::UP;
			pLink->m_state = CLink::CLOSED;
			pLink->m_sWhy = sWhy;
			Forget(pLink);
			if (bWasUp && pLink->m_bHandedOver)
				pLink->m_bNotifyClosed = true;
			AddDebugLogLine(false, _T("QUIC link with %s:%u closed: %s"), (LPCTSTR)ipstr(pLink->m_uIP), pLink->m_uPort, (LPCTSTR)sWhy);
		}

		void HandleError(CLink *pLink, int rv, LPCTSTR pszWhere)
		{
			if (rv == NGTCP2_ERR_DRAINING || rv == NGTCP2_ERR_CLOSING)
				Close(pLink, _T("the peer closed the connection"), false);
			else if (rv == NGTCP2_ERR_IDLE_CLOSE)
				Close(pLink, _T("idle"), false);
			else if (rv == NGTCP2_ERR_DROP_CONN)
				Close(pLink, _T("dropped"), false);
			else {
				CString sWhy;
				sWhy.Format(_T("%s: %hs"), pszWhere, ngtcp2_strerror(rv));
				Close(pLink, sWhy, true);
			}
		}

		// ---- the stream going out
		void QueueBytes(CLink *pLink, const BYTE *pData, size_t uLen)	// m_sendLock held
		{
			while (uLen > 0) {
				const size_t uInLast = (size_t)((pLink->m_uEnd - pLink->m_uBlockBase) % BLOCK);
				if (pLink->m_blocks.empty() || (uInLast == 0 && pLink->m_uEnd - pLink->m_uBlockBase == pLink->m_blocks.size() * BLOCK))
					pLink->m_blocks.push_back(std::unique_ptr<BYTE[]>(new BYTE[BLOCK]));
				const size_t uRoom = BLOCK - (size_t)((pLink->m_uEnd - pLink->m_uBlockBase) % BLOCK);
				const size_t n = min(uRoom, uLen);
				memcpy(pLink->m_blocks.back().get() + (BLOCK - uRoom), pData, n);
				pLink->m_uEnd += n;
				pData += n;
				uLen -= n;
			}
		}

		void OpenLocalStream(CLink *pLink)
		{
			if (pLink->m_iLocalStream >= 0 || pLink->m_state != CLink::PROVING)
				return;
			int64_t iStream = -1;
			if (ngtcp2_conn_open_bidi_stream(pLink->m_pConn, &iStream, NULL) != 0)
				return;	// not yet allowed; the stack tells when
			pLink->m_iLocalStream = iStream;
			// the proof: who we are and whom we expect, the very first bytes of our stream
			BYTE proof[PROOF_SIZE] = {};
			memcpy(proof, "EAQN1", 5);
			md4cpy(proof + 5, thePrefs.GetUserHash());
			if (pLink->m_bPeerKnown)
				md4cpy(proof + 21, pLink->m_peerHash);
			std::lock_guard<std::mutex> guard(pLink->m_sendLock);
			QueueBytes(pLink, proof, sizeof proof);
			pLink->m_bProofQueued = true;
		}

		void CheckUp(CLink *pLink)
		{
			if (pLink->m_state == CLink::PROVING && pLink->m_bProofOK && pLink->m_bProofQueued)
				pLink->m_state = CLink::UP;	// reported by Process(), outside of the stack's callbacks
		}

		// ---- callbacks of the QUIC stack
		CLink* Self(void *pUser)	{ return (CLink*)pUser; }

		ngtcp2_conn* GetConn(ngtcp2_crypto_conn_ref *pRef)
		{
			return Self(pRef->user_data)->m_pConn;
		}

		int OnHandshakeCompleted(ngtcp2_conn*, void *pUser)
		{
			CLink *pLink = Self(pUser);
			const unsigned char *pAlpn = NULL;
			unsigned int uLen = 0;
			SSL_get0_alpn_selected(pLink->m_pSsl, &pAlpn, &uLen);
			if (pAlpn == NULL || uLen != sizeof ALPN - 1 || memcmp(pAlpn, ALPN, uLen) != 0)
				return NGTCP2_ERR_CALLBACK_FAILURE;
			pLink->m_state = CLink::PROVING;
			ngtcp2_conn_set_keep_alive_timeout(pLink->m_pConn, KEEP_ALIVE);
			OpenLocalStream(pLink);
			return 0;
		}

		int OnExtendStreams(ngtcp2_conn*, uint64_t, void *pUser)
		{
			OpenLocalStream(Self(pUser));
			return 0;
		}

		int OnRecvStreamData(ngtcp2_conn *pConn, uint32_t uFlags, int64_t iStream, uint64_t, const uint8_t *pData, size_t uLen, void *pUser, void*)
		{
			CLink *pLink = Self(pUser);
			if (pLink->m_iRemoteStream < 0)
				pLink->m_iRemoteStream = iStream;	// the first stream the peer writes on, whichever it is
			if (iStream != pLink->m_iRemoteStream) {
				ngtcp2_conn_extend_max_stream_offset(pConn, iStream, uLen);
				ngtcp2_conn_extend_max_offset(pConn, uLen);
				return 0;
			}
			if (pLink->m_uProofGot < PROOF_SIZE) {
				const size_t n = min(PROOF_SIZE - pLink->m_uProofGot, uLen);
				memcpy(pLink->m_proof + pLink->m_uProofGot, pData, n);
				pLink->m_uProofGot += n;
				pData += n;
				uLen -= n;
				ngtcp2_conn_extend_max_stream_offset(pConn, iStream, n);
				ngtcp2_conn_extend_max_offset(pConn, n);
				if (pLink->m_uProofGot == PROOF_SIZE) {
					// the peer says who it is and whom it wants: it has to be us, and whom we expected
					const bool bForUs = isnulmd4(pLink->m_proof + 21) || md4equ(pLink->m_proof + 21, thePrefs.GetUserHash());
					if (memcmp(pLink->m_proof, "EAQN1", 5) != 0 || !bForUs || isnulmd4(pLink->m_proof + 5)
						|| (pLink->m_bPeerKnown && !md4equ(pLink->m_proof + 5, pLink->m_peerHash)))
					{
						return NGTCP2_ERR_CALLBACK_FAILURE;
					}
					md4cpy(pLink->m_peerHash, pLink->m_proof + 5);
					pLink->m_bPeerKnown = true;
					pLink->m_bProofOK = true;
					CheckUp(pLink);
				}
			}
			if (uLen > 0) {
				pLink->m_recv.append((const char*)pData, uLen);
				pLink->m_bNotifyRead = true;
			}
			if (uFlags & NGTCP2_STREAM_DATA_FLAG_FIN) {
				pLink->m_bPeerFinished = true;
				pLink->m_bNotifyRead = true;
			}
			return 0;
		}

		int OnAcked(ngtcp2_conn*, int64_t iStream, uint64_t uOffset, uint64_t uLen, void *pUser, void*)
		{
			CLink *pLink = Self(pUser);
			if (iStream != pLink->m_iLocalStream)
				return 0;
			std::lock_guard<std::mutex> guard(pLink->m_sendLock);
			pLink->m_uAcked = max(pLink->m_uAcked, uOffset + uLen);
			while (!pLink->m_blocks.empty() && pLink->m_uBlockBase + BLOCK <= pLink->m_uAcked) {
				pLink->m_blocks.pop_front();
				pLink->m_uBlockBase += BLOCK;
			}
			if (pLink->m_bWriterWaits && pLink->m_uEnd - pLink->m_uAcked < MAX_UNACKED / 2) {
				pLink->m_bWriterWaits = false;
				pLink->m_bNotifyWrite = true;
			}
			return 0;
		}

		int OnStreamClose(ngtcp2_conn*, uint32_t, int64_t iStream, uint64_t, void *pUser, void*)
		{
			CLink *pLink = Self(pUser);
			if (iStream == pLink->m_iRemoteStream || iStream == pLink->m_iLocalStream) {
				pLink->m_bPeerFinished = true;
				pLink->m_bNotifyRead = true;
			}
			return 0;
		}

		void OnRand(uint8_t *pDest, size_t uLen, const ngtcp2_rand_ctx*)
		{
			Random(pDest, uLen);
		}

		int OnNewCid(ngtcp2_conn*, ngtcp2_cid *pCid, ngtcp2_stateless_reset_token *pToken, size_t uLen, void *pUser)
		{
			pCid->datalen = uLen;
			Random(pCid->data, uLen);
			Random(pToken->data, sizeof pToken->data);
			AddCid(Self(pUser), *pCid);
			return 0;
		}

		int OnRemoveCid(ngtcp2_conn*, const ngtcp2_cid *pCid, void *pUser)
		{
			const auto it = s_byCid.find(CidKey(*pCid));
			if (it != s_byCid.end() && it->second == Self(pUser))
				s_byCid.erase(it);
			return 0;
		}

		ngtcp2_callbacks MakeCallbacks(bool bServer)
		{
			ngtcp2_callbacks cb = {};
			if (bServer)
				cb.recv_client_initial = ngtcp2_crypto_recv_client_initial_cb;
			else {
				cb.client_initial = ngtcp2_crypto_client_initial_cb;
				cb.recv_retry = ngtcp2_crypto_recv_retry_cb;
			}
			cb.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
			cb.handshake_completed = OnHandshakeCompleted;
			cb.encrypt = ngtcp2_crypto_encrypt_cb;
			cb.decrypt = ngtcp2_crypto_decrypt_cb;
			cb.hp_mask = ngtcp2_crypto_hp_mask_cb;
			cb.recv_stream_data = OnRecvStreamData;
			cb.acked_stream_data_offset = OnAcked;
			cb.extend_max_local_streams_bidi = OnExtendStreams;
			cb.rand = OnRand;
			cb.update_key = ngtcp2_crypto_update_key_cb;
			cb.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
			cb.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
			cb.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
			cb.get_new_connection_id2 = OnNewCid;
			cb.remove_connection_id = OnRemoveCid;
			cb.get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb;
			cb.stream_close = OnStreamClose;
			return cb;
		}

		bool SetupTls(CLink *pLink)
		{
			pLink->m_pSsl = SSL_new(pLink->m_bServer ? s_pServerCtx : s_pClientCtx);
			if (pLink->m_pSsl == NULL || ngtcp2_crypto_ossl_ctx_new(&pLink->m_pOssl, pLink->m_pSsl) != 0)
				return false;
			if ((pLink->m_bServer ? ngtcp2_crypto_ossl_configure_server_session(pLink->m_pSsl) : ngtcp2_crypto_ossl_configure_client_session(pLink->m_pSsl)) != 0)
				return false;
			pLink->m_ref.get_conn = GetConn;
			pLink->m_ref.user_data = pLink;
			SSL_set_app_data(pLink->m_pSsl, &pLink->m_ref);
			if (pLink->m_bServer)
				SSL_set_accept_state(pLink->m_pSsl);
			else {
				SSL_set_connect_state(pLink->m_pSsl);
				unsigned char wire[sizeof ALPN];
				wire[0] = (unsigned char)(sizeof ALPN - 1);
				memcpy(wire + 1, ALPN, sizeof ALPN - 1);
				SSL_set_alpn_protos(pLink->m_pSsl, wire, sizeof wire);
				SSL_set_tlsext_host_name(pLink->m_pSsl, SNI);
			}
			return true;
		}

		void Settings(ngtcp2_settings &rSettings, ngtcp2_transport_params &rParams, uint64 uNow)
		{
			ngtcp2_settings_default(&rSettings);
			rSettings.initial_ts = uNow;
			rSettings.cc_algo = NGTCP2_CC_ALGO_BBR;
			rSettings.max_tx_udp_payload_size = MAX_SEND_DATAGRAM;
			rSettings.no_tx_udp_payload_size_shaping = 1;
			rSettings.no_pmtud = 1;
			rSettings.handshake_timeout = HANDSHAKE_TIMEOUT;
			ngtcp2_transport_params_default(&rParams);
			rParams.initial_max_streams_bidi = 1;
			rParams.initial_max_streams_uni = 0;
			rParams.initial_max_stream_data_bidi_local = STREAM_WINDOW;
			rParams.initial_max_stream_data_bidi_remote = STREAM_WINDOW;
			rParams.initial_max_stream_data_uni = 0;
			rParams.initial_max_data = CONN_WINDOW;
			rParams.max_idle_timeout = IDLE_TIMEOUT;
			rParams.max_udp_payload_size = 1452;
			rParams.active_connection_id_limit = 2;
		}

		// Puts on the wire what the stack has to say.
		void WriteOut(CLink *pLink, uint64 uNow)
		{
			if (pLink->m_pConn == NULL || pLink->m_state == CLink::CLOSED)
				return;
			uint8_t buf[MAX_SEND_DATAGRAM];
			for (int iPackets = 0; iPackets < 48; ++iPackets) {
				ngtcp2_vec vec[2] = {};
				size_t uVecs = 0;
				int64_t iStream = -1;
				if (pLink->m_iLocalStream >= 0) {
					std::lock_guard<std::mutex> guard(pLink->m_sendLock);
					if (pLink->m_uSubmitted < pLink->m_uEnd) {
						iStream = pLink->m_iLocalStream;
						uint64 uOffset = pLink->m_uSubmitted;
						for (; uVecs < 2 && uOffset < pLink->m_uEnd; ++uVecs) {
							const size_t uIndex = (size_t)((uOffset - pLink->m_uBlockBase) / BLOCK);
							const size_t uIn = (size_t)((uOffset - pLink->m_uBlockBase) % BLOCK);
							const size_t n = (size_t)min((uint64)(BLOCK - uIn), pLink->m_uEnd - uOffset);
							vec[uVecs].base = pLink->m_blocks[uIndex].get() + uIn;
							vec[uVecs].len = n;
							uOffset += n;
						}
					}
				}
				ngtcp2_path_storage ps;
				ngtcp2_path_storage_zero(&ps);
				ngtcp2_pkt_info pi = {};
				ngtcp2_ssize iData = -1;
				ngtcp2_ssize n = ngtcp2_conn_writev_stream(pLink->m_pConn, &ps.path, &pi, buf, sizeof buf, &iData
					, NGTCP2_WRITE_STREAM_FLAG_NONE, iStream, uVecs ? vec : NULL, uVecs, uNow);
				if (n == NGTCP2_ERR_STREAM_DATA_BLOCKED || n == NGTCP2_ERR_STREAM_SHUT_WR || n == NGTCP2_ERR_STREAM_NOT_FOUND) {
					// the window of the peer is full: acknowledgements and the rest still go out
					n = ngtcp2_conn_writev_stream(pLink->m_pConn, &ps.path, &pi, buf, sizeof buf, NULL
						, NGTCP2_WRITE_STREAM_FLAG_NONE, -1, NULL, 0, uNow);
					iData = -1;
				}
				if (n < 0) {
					HandleError(pLink, (int)n, _T("write"));
					return;
				}
				if (iData > 0) {
					std::lock_guard<std::mutex> guard(pLink->m_sendLock);
					pLink->m_uSubmitted += (uint64)iData;
				}
				if (n == 0)
					break;
				SendDatagram(pLink, buf, (size_t)n, uNow);
			}
			ngtcp2_conn_update_pkt_tx_time(pLink->m_pConn, uNow);
		}

		// Tells the owner of a link what happened to it. Outside of any call into the stack.
		void Notify(CLink *pLink)
		{
			if (pLink->m_bDead)
				return;
			if (!pLink->m_bHandedOver) {
				if (pLink->m_state == CLink::UP) {
					pLink->m_bHandedOver = true;
					LearnHash(pLink->m_uIP, pLink->m_uPort, pLink->m_peerHash, true);
					AddDebugLogLine(false, _T("QUIC link with %s:%u is up (%s)"), (LPCTSTR)ipstr(pLink->m_uIP), pLink->m_uPort, pLink->m_bServer ? _T("the peer called") : _T("we called"));
					if (pLink->m_pDone != NULL)
						pLink->m_pDone(pLink, true, pLink->m_pUser);
					else
						LinkClose(pLink);
				} else if (pLink->m_state == CLink::CLOSED) {
					pLink->m_bDead = true;
					if (pLink->m_pDone != NULL)
						pLink->m_pDone(pLink, false, pLink->m_pUser);
				}
				return;
			}
			ILinkSink *pSink = pLink->m_pSink;
			if (pSink == NULL)
				return;
			if (pLink->m_bNotifyRead || (!pLink->m_recv.empty() && pLink->m_state == CLink::UP)) {
				pLink->m_bNotifyRead = false;
				pSink->OnLinkReadable();
				if (pLink->m_bDead || pLink->m_pSink != pSink)
					return;
			}
			if (pLink->m_bNotifyWrite) {
				pLink->m_bNotifyWrite = false;
				pSink->OnLinkWritable();
				if (pLink->m_bDead || pLink->m_pSink != pSink)
					return;
			}
			if (pLink->m_bNotifyClosed && pLink->m_recv.empty()) {
				pLink->m_bNotifyClosed = false;
				pSink->OnLinkClosed();
			}
		}

		CLink* NewLink(bool bServer, uint32 uIP, uint16 uPort, const uchar *pPeerHash)
		{
			CLink *pLink = new CLink;
			pLink->m_bServer = bServer;
			pLink->m_uIP = uIP;
			pLink->m_uPort = uPort;
			if (pPeerHash != NULL && !isnulmd4(pPeerHash)) {
				md4cpy(pLink->m_peerHash, pPeerHash);
				pLink->m_bPeerKnown = true;
			}
			pLink->m_uStart = NowNs();
			pLink->m_uNextResend = pLink->m_uStart + HANDSHAKE_RESEND;
			s_links.insert(pLink);
			return pLink;
		}

		void OnQuicDatagram(const BYTE *pData, UINT uLen, uint32 uIP, uint16 uPort)
		{
			const uint64 uNow = NowNs();
			ngtcp2_version_cid vc = {};
			if (ngtcp2_pkt_decode_version_cid(&vc, pData, uLen, CID_LEN) != 0)
				return;
			CLink *pLink = NULL;
			const auto it = s_byCid.find(std::string((const char*)vc.dcid, vc.dcidlen));
			if (it != s_byCid.end())
				pLink = it->second;
			if (pLink == NULL) {
				// a first packet: only from someone we wait for
				ngtcp2_pkt_hd hd = {};
				if (ngtcp2_accept(&hd, pData, uLen) != 0 || s_links.size() >= MAX_LINKS || !InitTls())
					return;
				SExpect expect = {};
				bool bFound = false;
				for (auto e = s_expected.begin(); e != s_expected.end(); ++e)
					if (e->uIP == uIP && e->uUntil > uNow) {
						expect = *e;
						s_expected.erase(e);
						bFound = true;
						break;
					}
				if (!bFound)
					return;
				pLink = NewLink(true, uIP, uPort, expect.bKnown ? expect.hash : KnownHash(uIP, uPort));
				pLink->m_pDone = expect.pDone;
				pLink->m_pUser = expect.pUser;
				pLink->m_bHeardPeer = true;
				pLink->m_scid.datalen = CID_LEN;
				Random(pLink->m_scid.data, CID_LEN);
				ngtcp2_settings settings;
				ngtcp2_transport_params params;
				Settings(settings, params, uNow);
				params.original_dcid = hd.dcid;
				params.original_dcid_present = 1;
				sockaddr_in remote, local;
				ngtcp2_path path = {};
				MakePath(pLink, remote, local, path);
				const ngtcp2_callbacks cb = MakeCallbacks(true);
				if (!SetupTls(pLink) || ngtcp2_conn_server_new(&pLink->m_pConn, &hd.scid, &pLink->m_scid, &path, hd.version, &cb, &settings, &params, NULL, pLink) != 0) {
					Close(pLink, _T("cannot set up the connection"), false);
					return;
				}
				ngtcp2_conn_set_tls_native_handle(pLink->m_pConn, pLink->m_pOssl);
				AddCid(pLink, pLink->m_scid);
				AddCid(pLink, hd.dcid);
			}
			if (pLink->m_state == CLink::CLOSED || pLink->m_uIP != uIP)
				return;
			if (!pLink->m_bHeardPeer && pLink->m_bPlainTurn)
				pLink->m_bKeepPlain = true;	// the answer came while we were trying plain: it stays
			pLink->m_bHeardPeer = true;
			pLink->m_uPort = uPort;	// the NAT of the peer may have moved it
			sockaddr_in remote, local;
			ngtcp2_path path = {};
			MakePath(pLink, remote, local, path);
			ngtcp2_pkt_info pi = {};
			const int rv = ngtcp2_conn_read_pkt(pLink->m_pConn, &path, &pi, pData, uLen, uNow);
			if (rv != 0)
				HandleError(pLink, rv, _T("read"));
			else
				WriteOut(pLink, uNow);
			Notify(pLink);
		}

		LRESULT CALLBACK TimerWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
		{
			if (uMsg == WM_TIMER) {
				Process();
				return 0;
			}
			return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
		}

		void StartTimer()
		{
			if (s_hTimerWnd != NULL)
				return;
			static const TCHAR szClass[] = _T("PegasoQuicTimer");
			WNDCLASS wc = {};
			wc.lpfnWndProc = TimerWndProc;
			wc.hInstance = AfxGetInstanceHandle();
			wc.lpszClassName = szClass;
			::RegisterClass(&wc);
			s_hTimerWnd = ::CreateWindowEx(0, szClass, _T(""), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, AfxGetInstanceHandle(), NULL);
			if (s_hTimerWnd != NULL)
				::SetTimer(s_hTimerWnd, 1, 10, NULL);
		}

		void StopTimerIfIdle()
		{
			if (s_hTimerWnd != NULL && s_links.empty()) {
				::KillTimer(s_hTimerWnd, 1);
				::DestroyWindow(s_hTimerWnd);
				s_hTimerWnd = NULL;
			}
		}
	}

	bool IsAvailable()
	{
		// the setting is read from the file now and then, not for every datagram
		static bool s_bOn;
		static DWORD s_dwRead;
		const DWORD dwNow = ::GetTickCount();
		if (s_dwRead == 0 || dwNow - s_dwRead >= 5000) {
			s_dwRead = dwNow | 1;
			s_bOn = theApp.GetProfileInt(_T("eMule"), _T("NatTraversal"), 1) != 0;
		}
		// not behind a proxy, which UDP would go around. Bound to an adapter (VPN protection) it is fine:
		// everything here goes through the client UDP socket, which is bound too.
		return s_bOn && thePrefs.GetUDPPort() != 0 && !thePrefs.GetProxySettings().bUseProxy && theApp.clientudp != NULL && InitTls();
	}

	void SendFrame(uint8 uType, const BYTE *pData, UINT uLen, uint32 uIP, uint16 uPort, const uchar *pPeerHash)
	{
		if (theApp.clientudp == NULL || uIP == 0 || uPort == 0)
			return;
		Packet *pPacket = new Packet(OP_UDPRESERVEDPROT2);
		pPacket->opcode = uType;
		pPacket->size = uLen;
		pPacket->pBuffer = new char[uLen + 1];
		memcpy(pPacket->pBuffer, pData, uLen);
		theStats.AddUpDataOverheadOther(uLen + 2);
		const bool bObfuscate = pPeerHash != NULL && !isnulmd4(pPeerHash) && thePrefs.IsCryptLayerEnabled();
		theApp.clientudp->SendPacket(pPacket, uIP, uPort, bObfuscate, pPeerHash, false, 0);
	}

	void SendCaps(uint32 uIP, uint16 uPort, const uchar *pPeerHash, const uchar *pFileHash)
	{
		BYTE caps[CAPS_SIZE] = {};
		memcpy(caps, "EAQC", 4);
		caps[4] = 1;
		caps[5] = (BYTE)((thePrefs.IsCryptLayerEnabled() ? 1 : 0) | (thePrefs.IsCryptLayerPreferred() ? 2 : 0) | (thePrefs.IsCryptLayerRequired() ? 4 : 0) | 0x40);
		md4cpy(caps + 6, thePrefs.GetUserHash());
		if (pPeerHash != NULL)
			md4cpy(caps + 22, pPeerHash);
		if (pFileHash != NULL)
			md4cpy(caps + 38, pFileHash);
		SendFrame(FRAME_CAPS, caps, sizeof caps, uIP, uPort, pPeerHash != NULL ? pPeerHash : KnownHash(uIP, uPort));
	}

	void OnFrame(const BYTE *pData, UINT uLen, uint32 uIP, uint16 uPort)
	{
		if (uLen < 2 || pData[0] != OP_UDPRESERVEDPROT2 || !IsAvailable())
			return;
		const uint8 uType = pData[1];
		pData += 2;
		uLen -= 2;
		switch (uType) {
		case FRAME_QUIC:
			OnQuicDatagram(pData, uLen, uIP, uPort);
			break;
		case FRAME_KEY:
			if (uLen >= 16)
				LearnHash(uIP, uPort, pData, false);	// never instead of a hash a rendezvous gave
			break;
		case FRAME_CAPS:
		case FRAME_CAPS_ACK:
			if (uLen >= CAPS_SIZE && memcmp(pData, "EAQC", 4) == 0 && pData[4] == 1
				&& (isnulmd4(pData + 22) || md4equ(pData + 22, thePrefs.GetUserHash())))
			{
				LearnHash(uIP, uPort, pData + 6, false);
				if (uType == FRAME_CAPS) {
					DWORD &rdwLast = s_capsAnswered[uIP];
					const DWORD dwNow = ::GetTickCount();
					if (rdwLast == 0 || dwNow - rdwLast >= 1000) {	// one answer per address per second
						rdwLast = dwNow | 1;
						BYTE caps[CAPS_SIZE];
						memcpy(caps, pData, CAPS_SIZE);
						caps[5] = (BYTE)((thePrefs.IsCryptLayerEnabled() ? 1 : 0) | (thePrefs.IsCryptLayerPreferred() ? 2 : 0) | (thePrefs.IsCryptLayerRequired() ? 4 : 0) | 0x40);
						md4cpy(caps + 6, thePrefs.GetUserHash());
						md4cpy(caps + 22, pData + 6);	// the file hash is echoed
						SendFrame(FRAME_CAPS_ACK, caps, sizeof caps, uIP, uPort, pData + 6);
					}
				}
			}
			break;
		}
	}

	CLink* Dial(uint32 uIP, uint16 uPort, const uchar *pPeerHash, LINKDONE pDone, void *pUser)
	{
		if (!IsAvailable() || s_links.size() >= MAX_LINKS)
			return NULL;
		const uint64 uNow = NowNs();
		CLink *pLink = NewLink(false, uIP, uPort, pPeerHash != NULL ? pPeerHash : KnownHash(uIP, uPort));
		pLink->m_pDone = pDone;
		pLink->m_pUser = pUser;
		ngtcp2_cid dcid = {};
		dcid.datalen = CID_LEN;
		Random(dcid.data, CID_LEN);
		pLink->m_scid.datalen = CID_LEN;
		Random(pLink->m_scid.data, CID_LEN);
		ngtcp2_settings settings;
		ngtcp2_transport_params params;
		Settings(settings, params, uNow);
		sockaddr_in remote, local;
		ngtcp2_path path = {};
		MakePath(pLink, remote, local, path);
		const ngtcp2_callbacks cb = MakeCallbacks(false);
		if (!SetupTls(pLink) || ngtcp2_conn_client_new(&pLink->m_pConn, &dcid, &pLink->m_scid, &path, NGTCP2_PROTO_VER_V1, &cb, &settings, &params, NULL, pLink) != 0) {
			s_links.erase(pLink);
			delete pLink;
			return NULL;
		}
		ngtcp2_conn_set_tls_native_handle(pLink->m_pConn, pLink->m_pOssl);
		AddCid(pLink, pLink->m_scid);
		StartTimer();
		WriteOut(pLink, uNow);
		return pLink;
	}

	void Expect(uint32 uIP, const uchar *pPeerHash, LINKDONE pDone, void *pUser)
	{
		if (!IsAvailable())
			return;
		const uint64 uNow = NowNs();
		for (auto it = s_expected.begin(); it != s_expected.end();)
			it = (it->uUntil <= uNow || it->uIP == uIP) ? s_expected.erase(it) : it + 1;
		SExpect expect = {};
		expect.uIP = uIP;
		if (pPeerHash != NULL && !isnulmd4(pPeerHash)) {
			md4cpy(expect.hash, pPeerHash);
			expect.bKnown = true;
		}
		expect.pDone = pDone;
		expect.pUser = pUser;
		expect.uUntil = uNow + EXPECT_TTL;
		s_expected.push_back(expect);
		StartTimer();
	}

	namespace { void DeleteDeadSinks(); bool s_bInProcess; }

	void Process()
	{
		if (s_bInProcess)
			return;	// a dialog opened from inside a notification pumps the timer
		s_bInProcess = true;
		DeleteDeadSinks();
		struct SLeave { ~SLeave() { s_bInProcess = false; } } leave;
		if (s_links.empty()) {
			if (s_expected.empty() || s_expected.back().uUntil <= NowNs())
				StopTimerIfIdle();
			return;
		}
		const uint64 uNow = NowNs();
		if (s_keys.size() > 4096 || s_capsAnswered.size() > 4096) {	// what was learned about endpoints does not pile up
			s_keys.clear();
			s_capsAnswered.clear();
		}
		const std::vector<CLink*> links(s_links.begin(), s_links.end());
		for (CLink *pLink : links) {
			if (!s_links.count(pLink))
				continue;	// gone meanwhile
			if (pLink->m_bDead) {
				s_links.erase(pLink);
				Forget(pLink);
				delete pLink;
				continue;
			}
			if (pLink->m_state != CLink::CLOSED && pLink->m_pConn != NULL) {
				if (ngtcp2_conn_get_expiry2(pLink->m_pConn) <= uNow) {
					const int rv = ngtcp2_conn_handle_expiry(pLink->m_pConn, uNow);
					if (rv != 0)
						HandleError(pLink, rv, _T("timer"));
				}
				// the first flight again while the NATs open: it may have hit a mapping which was not there yet
				if (pLink->m_state != CLink::CLOSED && !pLink->m_bHeardPeer && pLink->m_iResendLeft > 0 && uNow >= pLink->m_uNextResend && !pLink->m_lastLong.empty()) {
					--pLink->m_iResendLeft;
					pLink->m_uNextResend = uNow + HANDSHAKE_RESEND;
					const std::string again(pLink->m_lastLong);
					SendDatagram(pLink, (const uint8_t*)again.data(), again.size(), uNow);
				}
				if (pLink->m_state == CLink::HANDSHAKE || pLink->m_state == CLink::PROVING) {
					if (uNow - pLink->m_uStart > HANDSHAKE_TIMEOUT)
						Close(pLink, _T("no answer"), false);
				}
				if (pLink->m_state != CLink::CLOSED) {
					if (pLink->m_state == CLink::PROVING) {
						OpenLocalStream(pLink);
						CheckUp(pLink);
					}
					WriteOut(pLink, uNow);
				}
			}
			Notify(pLink);
		}
	}

	void Shutdown()
	{
		for (CLink *pLink : s_links) {
			pLink->m_pSink = NULL;
			pLink->m_pDone = NULL;
			Close(pLink, _T("closing"), true);
			delete pLink;
		}
		s_links.clear();
		s_byCid.clear();
		s_expected.clear();
		StopTimerIfIdle();
	}

	int LinkSend(CLink *pLink, const void *pData, int iLen)
	{
		if (pLink == NULL || pLink->m_state != CLink::UP || iLen <= 0)
			return -1;
		std::lock_guard<std::mutex> guard(pLink->m_sendLock);
		const uint64 uPending = pLink->m_uEnd - pLink->m_uAcked;
		if (uPending >= MAX_UNACKED) {
			pLink->m_bWriterWaits = true;
			return -1;
		}
		const int n = (int)min((uint64)iLen, MAX_UNACKED - uPending);
		QueueBytes(pLink, (const BYTE*)pData, (size_t)n);
		return n;
	}

	int LinkReceive(CLink *pLink, void *pData, int iLen)
	{
		if (pLink == NULL || iLen <= 0)
			return 0;
		if (pLink->m_recv.empty())
			return (pLink->m_state == CLink::UP && !pLink->m_bPeerFinished) ? -1 : 0;
		const size_t n = min(pLink->m_recv.size(), (size_t)iLen);
		memcpy(pData, pLink->m_recv.data(), n);
		pLink->m_recv.erase(0, n);
		if (pLink->m_pConn != NULL && pLink->m_state == CLink::UP && pLink->m_iRemoteStream >= 0) {
			// room for the peer to send more
			ngtcp2_conn_extend_max_stream_offset(pLink->m_pConn, pLink->m_iRemoteStream, n);
			ngtcp2_conn_extend_max_offset(pLink->m_pConn, n);
		}
		return (int)n;
	}

	void LinkSetSink(CLink *pLink, ILinkSink *pSink)
	{
		if (pLink != NULL)
			pLink->m_pSink = pSink;
	}

	void LinkClose(CLink *pLink)
	{
		if (pLink == NULL || pLink->m_bDead)
			return;
		pLink->m_pSink = NULL;
		pLink->m_pDone = NULL;
		Close(pLink, _T("closed here"), true);
		pLink->m_bDead = true;	// deleted by Process()
	}

	uint32 LinkIP(const CLink *pLink)			{ return pLink->m_uIP; }
	uint16 LinkPort(const CLink *pLink)			{ return pLink->m_uPort; }
	const uchar* LinkPeerHash(const CLink *pLink)	{ return pLink->m_peerHash; }

	///////////////////////////////////////////////////////////////////////////////////////
	// A link under an eMule socket

	namespace
	{
		class CSocketSink : public ILinkSink
		{
		public:
			CAsyncSocketEx *m_pSocket;
			CLink *m_pLink;

			virtual void OnLinkReadable()
			{
				// eMule reads once per event; ask again as long as it takes something
				for (int i = 0; i < 16; ++i) {
					CAsyncSocketEx *pSocket = m_pSocket;
					CLink *pLink = m_pLink;
					if (pSocket == NULL || pLink == NULL)
						return;
					const size_t uBefore = pLink->m_recv.size();
					if (uBefore == 0 && pLink->m_state == CLink::UP && !pLink->m_bPeerFinished)
						return;
					pSocket->OnReceive(0);
					if (m_pSocket != pSocket || m_pLink != pLink || uBefore == 0 || pLink->m_recv.size() >= uBefore)
						return;
				}
			}
			virtual void OnLinkWritable()
			{
				if (m_pSocket != NULL)
					m_pSocket->OnSend(0);
			}
			virtual void OnLinkClosed()
			{
				if (m_pSocket != NULL)
					m_pSocket->OnClose(0);
			}
		};
		std::map<CAsyncSocketEx*, CSocketSink*> s_sinks;
		std::vector<CSocketSink*> s_deadSinks;	// deleted by Process(): one may be running
	}

	bool AttachToSocket(CAsyncSocketEx *pSocket, CLink *pLink)
	{
		if (pSocket == NULL || pLink == NULL || pLink->m_state != CLink::UP)
			return false;
		CSocketSink *pSink = new CSocketSink;
		pSink->m_pSocket = pSocket;
		pSink->m_pLink = pLink;
		s_sinks[pSocket] = pSink;
		pLink->m_pSink = pSink;
		pSocket->m_pPegasoLink = pLink;
		return true;
	}

	namespace
	{
		void DeleteDeadSinks()
		{
			for (CSocketSink *pSink : s_deadSinks)
				delete pSink;
			s_deadSinks.clear();
		}
	}

	// Called by CAsyncSocketEx when a socket with a link is closed.
	void DetachFromSocket(CAsyncSocketEx *pSocket)
	{
		const auto it = s_sinks.find(pSocket);
		if (it != s_sinks.end()) {
			it->second->m_pSocket = NULL;
			it->second->m_pLink = NULL;
			s_deadSinks.push_back(it->second);
			s_sinks.erase(it);
		}
		CLink *pLink = (CLink*)pSocket->m_pPegasoLink;
		pSocket->m_pPegasoLink = NULL;
		LinkClose(pLink);
	}
}
