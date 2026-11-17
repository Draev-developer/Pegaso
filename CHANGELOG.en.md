# Changes

Pegaso starts from eMule 0.72a (community edition). The history of eMule is in `changelog_full.txt`. · [Español](CHANGELOG.md)

## 0.7.1

- **Adding a torrent to an eMule download without typing paths.** "Add a torrent of this same file…" now opens a window with a **Browse** button (it starts in your download folder) and a list of what Pegaso has found by itself: the torrents of the download list and the `.torrent` files of your download folders which have a file of exactly the same size. If there is only one, it comes chosen already: OK is enough. The magnet link or the path can still be typed.
- **A torrent of the list as a source.** A torrent which is in the list already — finished or halfway — can give its data to an eMule download of the same file without ceasing to be what it was: it keeps its row, its folder and whatever it downloads. The data goes through the hash check of eMule all the same.
- **The built-in server list is up to date.** Pegaso now ships with the 19 servers of the list of `upd.emule-security.org` of 17 November 2026 (9 before). They are added when the server list is empty; whoever has servers already keeps them and can update them in Servers → "Update server.met from URL".

## 0.7

### Daily use
- **Play while downloading.** Right-click a torrent → "Play while downloading": Pegaso fetches its pieces in order, the beginning and the end of the largest video or audio first, and opens it with the player of Windows as soon as the beginning has arrived. If there is not enough yet, it opens by itself when there is. eMule downloads still have "Preview".
- **Watched folder.** Tools → "Automatic folders…": `.torrent` files, and `.txt` files with one link per line, left in the folder you choose are added by themselves. What was added is moved to its `added` subfolder.
- **Finished downloads to their folder, by type.** In the same window, a folder for videos, audio, images, documents, archives and programs. It works for eMule, web and BitTorrent; a torrent goes whole to the folder of its largest file. What is moved stays shared on eD2K and seeded. Downloads with a category of their own go to the folder of their category, as always.
- **Import from eMule and qBittorrent.** Tools → "Import from eMule or qBittorrent…". From eMule, Pegaso goes on with the unfinished downloads where they are (its Temp folder becomes one more temporary folder of Pegaso; nothing is copied). From qBittorrent, it adds the torrents of its `BT_backup` folder with what qBittorrent knew of them: where the files are, which are wanted and what is there already.

- **What has been renamed leaves Downloads.** A video which "Rename videos" has given a name or a place no longer stays in the download list, where it looked as if it had not been moved. If it came from eMule or the web it is still under Shared. If it belonged to a torrent, the torrent leaves the list once all its videos have their place and goes on being seeded until it reaches the ratio limit of the preferences; then Pegaso lets go of it (the files stay). With the ratio limit at 0 (seed for ever) the torrent stays in the list, so that nothing is seeded out of sight without an end.

### Privacy
- **An indicator always in sight.** The side bar says in one line how hidden your address is: green if everything leaves through the VPN or the proxy, amber if the address of your connection is in sight, red if everything is stopped. A click opens "Check my connection".
- **Website names through the VPN.** With the VPN protection on, Pegaso asks the names of the websites (DNS) of the servers of the VPN adapter and of no other, instead of leaving it to Windows. If that adapter has no DNS servers of its own, Windows still looks them up, and "Check my connection" says so.

### Fixed
- **Downloading many results at once left Pegaso not responding.** When hundreds of files were added from a search, the whole download queue was sorted again for each file, by a method whose cost went up with the square of the list (inherited from eMule), and the list of part files was written again each time. Now it is sorted and saved once, at the end: 1500 downloads go from 53 seconds of standstill to 8.
- With several suspicious results among those selected, Pegaso asks one question for all of them (all, only the others, or cancel) instead of opening a window per file.

### API
- `POST /api/v1/downloads/{hash}/play`: plays a torrent while it downloads.

### Documentation
- `BUILDING.en.md` and `CHANGELOG.en.md`: what was missing in English.

## 0.6.2

It completes the privacy review of 0.6.1. Everything is explained in [docs/en/privacy.md](docs/en/privacy.md).

### Privacy and security
- **Encrypted secrets.** The token of the local API, the keys of the renaming services and of your search sites and the proxy password are no longer in clear in `preferences.ini`: Windows encrypts them for your user account. What was in clear is encrypted at startup.
- **Check my connection** (Tools, and Preferences → Web and BitTorrent): which way each protocol leaves at that moment — proxy, VPN adapter or system route —, whether the kill switch is armed and whether everything is stopped. In the API too: `GET /api/v1/connection`.
- **Web requests tied to the VPN.** With the VPN protection on, web downloads, web searches, renaming and the updates of the lists leave through the VPN adapter, not by the route of the system: Pegaso makes the connection itself.
- **Web requests through SOCKS proxies.** With a SOCKS5 or SOCKS4a proxy, web requests go through it and it is the proxy which looks the names up (they were not made before). The user and password of the proxy are used too, including an HTTP proxy with a password.
- **Clear local traces** (Tools): search history, finished and cancelled downloads, the memory of files you no longer share and the activity log. An option does it every time Pegaso closes.
- The SHA-256 fingerprint of the installer is published with every release.

### Fixed
- **Options lost on exit.** A Pegaso option changed during a session — the adapter of the VPN protection, BitTorrent, the local API, your search sites, the keys — could go back to its former value when the program closed or the preferences were accepted, if an eMule preference changed in the same session as well. eMule saved the configuration file as it had read it at startup; now it reads it again before saving. The fault came from earlier versions.

### Daily use
- **A check for new versions, if you want it.** Off by default. Turned on in Preferences → General, Pegaso asks GitHub every so many days and says so in the log. It downloads and installs nothing.
- **"Pause everything" in the scheduler.** Preferences → Scheduler has a new action: during the hours you set there is no eD2K, Kad, BitTorrent or web, and when they are over Pegaso connects again. The speed limits of the scheduler already applied to BitTorrent and web downloads.
- **Torrents without their folder.** An option to save the files of a torrent straight into the downloads folder. If one of them would land on a file which is already there, the torrent keeps its folder.
- **Documentation in English**: [README.en.md](README.en.md) and `docs/en`.

## 0.6.1

Privacy review. What Pegaso lets others see of you, what it does not, and how to hide your address is in [docs/en/privacy.md](docs/en/privacy.md).

### The proxy of the preferences is for everything
- Until now the proxy of Preferences → Proxy was used by eD2K alone: BitTorrent and web downloads went out directly, with your address in sight. Now BitTorrent (peers, trackers and, with SOCKS5, the DHT) goes through the proxy, and the names are looked up by the proxy.
- What cannot go through the proxy is no longer sent. Kad and the UDP of eD2K, which in eMule went round the proxy, are not used while there is a proxy (`ProxyStrict=0` in `preferences.ini` gives back the behaviour of eMule). Web downloads, web searches and renaming go through the proxy if it is an HTTP one; with a SOCKS proxy, which Windows cannot use for the web, they are not made and Pegaso says so.
- With a proxy, BitTorrent does not say which program it is, accepts no incoming connections and asks nobody on the local network.

### IPv6 and VPN
- With a VPN which carries IPv4 only and without the VPN protection of Pegaso turned on, IPv6 left by the normal connection. Pegaso now looks at which adapter each protocol leaves through and, if they differ, stops using IPv6 on eD2K, Kad and BitTorrent and says so.
- Tied to an adapter or with a proxy, BitTorrent no longer connects to any peer over IPv6, whoever names it.
- IPv6 addresses made from the number of the network card are not announced.

### Others
- The update of the server list and of the IP filter respects the kill switch of the VPN protection and uses the proxy.
- The list of titles of AniDB is fetched over HTTPS.

## 0.6

### Browser extension
- Pegaso has an extension for Chrome, Edge and Firefox (`browser-extension` folder). It adds "Download with Pegaso" to the right-click menu on a link, on a video or audio and on a selected text (it sends all the ed2k, magnet and web links it contains). Its button shows the downloads of Pegaso with their progress and takes a pasted link.
- Optional, off by default: let Pegaso take the downloads the browser starts. If Pegaso does not take one, the browser goes on with it.
- The extension only talks to Pegaso on the computer itself, through the local API and with its key. It sends nothing to anyone else.
- Local API: requests from browser extensions are admitted (those from web pages are still rejected), and the web address of a .torrent file is added as a torrent.

### Making torrents
- Tools → "Create torrent…" makes the torrent of a file or folder of yours. It reads them once to compute their hashes (in the background, with a progress bar), writes the .torrent file next to them, gives its magnet link and starts seeding it without checking the data again.
- Trackers are optional: without them the torrent is found through the DHT network. It can be marked as private (only through its trackers).
- The data is neither moved nor copied: it is seeded from where it is.

### Series of downloads
- A web address with a range in square brackets stands for the whole series: `https://example.org/part[01-20].zip` fetches `part01.zip` to `part20.zip`. The first number sets how the others are written: with leading zeros, they all keep its width. Up to 1000 addresses per series. It works in the field above, in "Add link", in the extension and in the API.

### Others
- Contact: the window also carries the accounts of Pegaso on X (@draev_developer) and Instagram (draev.pegaso), with a button which opens each.

## 0.5.3

- Contact: a new entry in the side bar, with the address to write suggestions to (draev.pegaso@gmail.com), a button which opens the mail program with the address and the version already filled in, and another which copies the address.
- In low windows, the sections of the side bar stand closer so that the connection card still fits.

## 0.5.2

- Renaming videos: when a video is moved to another folder, the folder it was in is removed if no other video is left in it (samples do not count). The leftovers go to the Recycle Bin with the folder. What a torrent of the list still offers (an .nfo, some subtitles) is not deleted: it is kept apart, with the state of the engine, and the torrent stays complete and seeding. The download folders themselves are never touched. It can be turned off in the window itself.
- Folders marked as read-only, which Windows does not let be removed just like that, are removed too.
- Keys: the "Keys…" window explains how to get the one of each service and opens its website. There is a guide in `docs/en/rename-videos.md`.

## 0.5.1

- Renaming videos: moving many files, or large files to another disk, no longer leaves Pegaso stuck. The files are moved one after another in the background, with a progress bar and the name of the one being moved, and the window no longer holds the rest of Pegaso: it can be used meanwhile.
- A file of a torrent is moved like the others, to another disk too: the torrent lets go of it, the file is moved and the engine takes note of its new place and goes on seeding it.
- Splash screen: "Pegaso (C) 2026 Draev - eMule (C) 2002-2026 Merkur". The notice of eMule is kept, as its licence asks.

## 0.5

### Renaming videos
- Tools → "Rename videos…" gives finished videos the name of the film or series they are, as collection organisers do: `Name (year).ext` for films and `Name - 1x05.ext` for series. What each file is (film or episode, of what, season and episode) is read from its name; the right name, and the year of the films, is given by the database chosen: TVmaze, TheMovieDB, TheTVDB or AniDB for series, TheMovieDB or OMDb for films. The names are asked for in the language of Pegaso where the database allows it.
- Before renaming, the list shows the present name and the new one; you tick what you want changed and any name is corrected by hand (F2).
- Moving: films and series can each be taken to a folder, which is made if missing.
- A renamed or moved file is still the same one for the networks. On eD2K and Kad it keeps its hash and stays shared without computing it again, also if the new folder is not a shared one. On BitTorrent the engine itself moves it and goes on seeding it from the new place, also after closing and opening Pegaso.
- Keys: TVmaze and AniDB ask for none. TheMovieDB, OMDb and TheTVDB give one for free to whoever signs up; Pegaso ships with none and each user types their own ("Keys…" button).
- AniDB has no search for programs: it publishes the list of all its titles, which Pegaso fetches once a day at most and looks up at home. Where AniDB puts a check for browsers in front, Pegaso does not get round it: it says so, and uses the list the user downloads with the browser and leaves in the configuration folder.

### Others
- User name: whoever starts with Pegaso is called "Pegaso" on the networks, instead of the address of the eMule website. Whoever already had a name keeps it.
- Local API: the list of shared files always came out empty; now it brings them, with their path.
- Dragging files from Explorer: the files Pegaso opens with a double click (.torrent and eMule collections) are also accepted when dropped on the window, one or several at a time. Before, the window only accepted links and Internet shortcuts.

## 0.4

### Interface
- A new main window. A side bar replaces the toolbar: the sections (Downloads, Search, Shared, Uploads, Servers, Kad, Statistics) with their counters and, at the foot, the state of the networks with the connect button.
- Above each section, a single field: what is typed is searched for, and if it is a link (ed2k, magnet or web) it is offered for download. Next to it, "Add link", Tools and Preferences.
- Downloads can be filtered by state: all, downloading, paused and completed, each with its number.
- Thin progress bars with the percentage next to them, taller rows and the selected row in the colour of the program. The map of parts is still in the details of the file.
- Downloads and Uploads each have their section; the column of small buttons next to the downloads is hidden by default (it comes back in Preferences → Display).

### Search
- Searching the sites of the user: the list of providers of the web search becomes a notebook of sites. Besides a Torznab API, a site is saved with the address of its search page and `%s` where the words go; Pegaso asks for that page and lists the download links it finds in it (ed2k, magnet, .torrent files and files). The search runs on all the sites or on the one chosen. Pegaso still ships with no site.
- Sites without a search engine: an address saved without `%s` which does not turn out to be a Torznab source is taken as a catalogue. Pegaso looks for the words in what the page lists, opens the pages which match (eight at most per search; it says how many are left) and lists their download links with the name of each page. The same is done with a results page which only links to other pages. If nothing matches, the search says so and explains which address to save, instead of a bare "0 results".
- Pages to open by hand: when a page which matches does not show the download (it is behind another site or behind a step only a person can take), cannot be read or could not be opened, it comes up in the results as "Web page" and the button turns into "Open the site": it opens in the browser of the user, who goes on from there. Pegaso does not try to get past those steps.
- An address of the kind `…/get.php?file=name.torrent` is recognised as a .torrent file.

### Security
- Kaspersky VPN for Pegaso: where Kaspersky VPN is installed, Tools offers to tie Pegaso to its adapter with the kill switch on, explains what to set in Kaspersky so that the VPN carries Pegaso alone (split tunnelling, "Apps that always connect via VPN") and opens it. Kaspersky VPN does not let another program connect it or change its settings: connecting and adding Pegaso to its list is done by the user there.

### Identity
- Pegaso shows "Created by Draev" under its name and in the credits; the installer names it as the publisher.

### Known limitations
- The search on sites only sees what the page brings in its HTML: it runs no JavaScript, does not log in and does not pass checks against programs (captchas and the like). It takes up to 500 links from a page, without size or seeds except for ed2k links.
- The inside of the sections (search, servers, Kad, statistics, preferences) keeps the controls it always had.

## 0.3.1

### eMule networks
- Transfers between two low IDs through Kad, without depending on the server, in two ways:
  - Through the buddy of the source: Pegaso sends it a request which any buddy, also that of an unmodified eMule, forwards to the source; the source opens its router towards us and the connection is made over QUIC. When Pegaso acts as a buddy, it also tells whoever asks where the client it serves is.
  - Direct, with the sources which have their UDP port open and announce it in Kad (`nat` tag): they are asked over UDP to wait for a QUIC call.
- Pegaso announces in its Kad source records that it accepts QUIC calls when it cannot receive connections.
- With a low ID, Kad sources which cannot receive connections either are no longer dropped.

### Known limitations
- The two Kad ways only work with sources which speak this dialect (Pegaso, eMule Qt, eMuleAI); tested between two Pegaso with a simulated Kad node and buddy, not with other programs or through real routers.
- The introduction through Lugdunum servers is still missing.

## 0.3

### eMule networks
- IPv6 for eD2K and Kad sources, with the extension used by eMule Qt and other clients: Pegaso listens on IPv6, announces its address in the hello, in the server login and in Kad when it knows it is reachable, and calls directly over IPv6 the low-ID sources which announce it. It understands the IPv6 sources the servers hand out and the request for a call over IPv6. Keys in `preferences.ini`: `IPv6Enabled`, `IPv6Address`, `IPv6AssumeReachable`.
- Transfers between two low IDs: the server introduces the two clients (ed2k-server protocol 0.9.84 or later), both open their routers with UDP datagrams and the eD2K connection travels inside a QUIC link, with the dialect of eMuleAI, aMule and eMule Qt. Key `NatTraversal` in `preferences.ini`.
- IPv6 is left out while Pegaso is tied to an adapter (VPN) or uses a proxy; QUIC links, only with a proxy (with a VPN they leave through the tied adapter, like the rest of the UDP traffic).

### Interface
- A local API for other programs on the computer: REST and MCP (for AI assistants). Off by default; it is turned on in Tools → "Local API (REST and MCP)…". It listens on 127.0.0.1 only, asks for a secret key and rejects the requests which come from web pages. It lets downloads be seen and handled, eD2K and Kad be searched, and the program be connected or disconnected. Described in `docs/en/api.md`.

### Technical
- The installer takes another output folder (`ISCC /DOutDir=…`).
- A new dependency: ngtcp2 (QUIC), through vcpkg.

### Known limitations
- The introduction between two low IDs only works with servers which speak the ed2k-server protocol; the Kad way and that of the Lugdunum servers are missing.
- QUIC datagrams are obfuscated only when the public IP is known when the link is made; otherwise they go unobfuscated.
- Of the IPv6 extension, the extended source exchange, IPv6 sources in ed2k links and UDP over IPv6 are missing.
- The IP filter does not cover IPv6 addresses.

## 0.2

### Security
- Fake detector: every search result and every download gets a score from 0 to 100 without waiting for anybody to rate it. It looks at the name (a program named like a video, double extensions), the different names one file goes around under, the announced length against the size, the first bytes of the download against its extension, the content of torrents and the verdicts of the users. It shows in the "Trust" column, with the reasons in the tooltip; before downloading a suspicious result, Pegaso asks. It only warns, it never blocks.

### eMule networks
- The sources of each download are remembered on exit (up to 20 per file, for 24 hours) and brought back at startup, without waiting for the server or Kad to find them again.
- Port mapping on the router: on by default and, besides UPnP, with NAT-PMP and PCP. A port only counts as open if the router grants exactly the one asked for and directs it to this computer; it is renewed before it expires and given back on exit. If the router has no public address (CGNAT or a double router) or the port is already forwarded to another computer, Pegaso says so in the log instead of leaving a low ID unexplained.

### Interface
- Country flags next to the sources of the downloads, the clients (uploads, queue, known clients) and the servers; the country also shows in the tooltip of each source. The lookup is done on the computer itself with the "IP to Country Lite" database of DB-IP (CC BY 4.0); nothing is sent anywhere.

## 0.1 — first version

### eMule networks
- eD2K servers out of the box, and addresses to update the server list from.
- **Connect** always joins a server and Kad; if Kad knows no contacts, it downloads an initial list.
- No IRC chat and no messages between users.

### BitTorrent
- Magnet links and `.torrent` files in the same download list (libtorrent 2.1 engine).
- A chooser for the files of the torrent; `.url` shortcuts come unticked.
- A map of pieces in the progress bar.
- Visible seeding (speed, total shared) with a ratio limit.
- Web seeds: those of the torrent itself and those the user adds.
- Finished torrents are offered on eD2K and Kad as well.

### Web downloads
- HTTP and HTTPS with several connections per file, pause, resume after a restart and a speed limit shared with the rest of the downloads.
- Copying a download link (the web address of a file, magnet or ed2k) opens the "Add download links" window; what is copied while it is open is added to the list.
- Tools → "Add download links…" to type or paste them by hand.

### Hybrid swarm
- An eMule download can also take a web source and a torrent of the same file.
- The torrent only fetches what is missing and its data passes the hash check of eMule; a torrent which is not the same file is dropped.
- The web sources and the torrent added to a download are kept across a restart.

### Security
- File verdicts: correct, poor quality, fake or not what the description says, with a warning when several users flag a file.
- The verdicts work for torrents too: the verdict is published in Kad and seen by the other users of Pegaso who add that torrent.
- VPN protection: exclusive use of one network adapter, and the traffic is cut if it disconnects.
- Every finished download is checked with the installed antivirus (AMSI).
