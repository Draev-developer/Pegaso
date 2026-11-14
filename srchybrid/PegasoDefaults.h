// Pegaso: what a fresh installation needs to get on the networks without any setup.
//
// - ed2k servers: a built-in list, used whenever the server list is empty. They are the
//   servers that appear in both public reference lists (emule-security.org and
//   emuling.gitlab.io) at the time of the release.
// - addresses of server.met files for "update the server list from a URL".
// - Kad: with no known contacts (no nodes.dat) a contact list is fetched in the
//   background, so that "Connect" brings up Kad as well as a server.
#pragma once

// Adds the built-in servers; returns how many were added. Main thread, window created.
int		PegasoAddDefaultServers();

// Addresses of server.met files, for an empty addresses.dat.
void	PegasoDefaultServerMetUrls(CStringList &list);

// Kad is running without contacts: fetch some. Does nothing if a fetch is under way,
// was done a moment ago, or VPN protection is holding the network.
void	PegasoBootstrapKad();

// Main thread, called regularly: hands a fetched contact list over to Kad.
void	PegasoDefaultsProcess();
