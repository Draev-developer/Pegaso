// Pegaso: the way out of the web requests.
//
// Web downloads, web searches, the databases of the video names and the updates of the lists
// are made by Windows (WinHTTP, WinINet), which can neither be tied to a network adapter nor
// speak to a SOCKS proxy. Whenever Pegaso is tied to an adapter or a proxy is set in the
// preferences, Windows is told to use a proxy on this computer - the bridge - and the bridge
// makes the connection the way Pegaso wants it:
//  - from the address of the adapter Pegaso is tied to (IPv4 only), looking the names up on
//    the DNS servers of that adapter and of no other (if it has any), or
//  - through the SOCKS4a / SOCKS5 proxy of the preferences, which also looks the names up, or
//  - through the HTTP proxy of the preferences, with its user and password.
// While the VPN protection or the scheduler holds everything, the bridge answers 503.
// TLS goes through untouched: the bridge only carries the bytes.
//
// The bridge listens on 127.0.0.1, on a port chosen by Windows, and only while it is needed.
#pragma once

namespace PegasoBridge
{
	// Main thread: takes note of the proxy of the preferences and of the VPN protection.
	void	Sync();

	// Any thread. 0: no bridge is needed, Windows goes its own way. > 0: the port of the
	// bridge. < 0: the requests cannot be made (a SOCKS4 proxy, which cannot look names up;
	// or the bridge could not be started) and are not to go any other way.
	int		Port();
}
