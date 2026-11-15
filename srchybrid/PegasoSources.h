// Pegaso: the sources of the downloads survive a restart.
//
// eMule forgets every source when it closes and has to find them again through the server
// and Kad, which takes minutes. Up to 20 sources per download - those which answered, the
// ones giving data first - are written to config\sources.dat when Pegaso closes (and every
// few minutes), and handed back to their downloads at the next start. An entry is used for
// 24 hours; it goes through the IP filter again and through every check eMule makes on a
// source it is told about. Only sources which can be called directly (high ID) are kept.
#pragma once

void	PegasoSourcesProcess();	// main thread, called regularly: loads once, saves now and then
void	PegasoSourcesSave();	// at shutdown, while the downloads still have their sources
