// Pegaso: country flags next to sources, clients and servers.
//
// The country of an address comes from geo\ip-country.dat next to the executable (built by
// tools/geo/make_geo.py from DB-IP's "IP to Country Lite" - CC BY 4.0, see geo\README.txt),
// its flag from geo\flags.bmp. Without those files nothing is drawn and nothing else changes.
// Nothing is ever looked up on the network.
#pragma once

// Draws the flag of an address (as eMule keeps the address of a client or server) at the
// left of rRect, centred vertically, and moves the left edge past it. The room is taken even
// if the country is not known, so that names stay aligned.
void	PegasoDrawFlag(CDC *pDC, RECT &rRect, uint32 uIP, int iGapBefore = 0);
// "Spain (ES)" - the name as Windows calls the country in the language of the user; empty if not known.
CString	PegasoCountryOf(uint32 uIP);
