// Pegaso: a local API - REST and MCP - for other programs on this computer.
//
// Off by default. When the user turns it on (Tools menu), a small HTTP server listens on
// 127.0.0.1 only and lets scripts, and AI assistants through the Model Context Protocol, see
// and control Pegaso: status, downloads (list, add, pause, resume, remove), shared files,
// searches and their results, connect and disconnect.
//
//   - It never listens on the network: the socket is bound to the loopback address.
//   - Every request needs the secret token ("Authorization: Bearer <token>"); the token is
//     made of random bytes the first time the API is turned on.
//   - Requests whose Host is not this computer, or which a web page sends (an Origin that is
//     not local), are refused, so a web site cannot reach the API through the browser.
//
// preferences.ini, [eMule]: ApiEnabled (0/1), ApiPort (4780), ApiToken.
// The endpoints are described in docs/api.md.
#pragma once

class CPegasoApi
{
public:
	void	Start();		// main thread, at startup: starts listening if the API is turned on
	void	Stop();			// main thread, at shutdown
	bool	IsRunning() const;
	void	ShowDialog(CWnd *pParent);	// Tools menu: tells the state, turns it on or off

	static bool	IsEnabledInPrefs();
	static UINT	PortInPrefs();
	static CString TokenInPrefs();	// made the first time it is asked for
};

extern CPegasoApi thePegasoApi;
