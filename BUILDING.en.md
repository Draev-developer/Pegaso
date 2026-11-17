# Building Pegaso

Pegaso is built for 64-bit Windows with Visual Studio 2026 (toolset v145). The program links all its libraries statically, so the result is a single `Pegaso.exe` plus the language DLLs. · [Español](BUILDING.md)

## Requirements

- **Visual Studio 2026** (the Community edition will do) with the *Desktop development with C++* workload and the **MFC for x64** component.
- **CMake** and **Git** on the `PATH`.
- **PowerShell 7** (`pwsh`).
- Optional: **Python 3** with Pillow to regenerate icons and texts, and **Inno Setup 6** for the installer.

## 1. Libraries

The code of the application is in `srchybrid`. The libraries go in sibling folders, at the root of the repository (they are not included in it):

| Folder | Library | Version used |
|---|---|---|
| `cryptopp` | Crypto++ | 8.9 |
| `id3lib` | id3lib | 3.9.1 |
| `mbedtls` | Mbed TLS | 4.2.0 |
| `miniupnpc` | miniupnpc | 2.3.3 |
| `ResizableLib` | ResizableLib | the one of eMule |
| `zlib` | zlib | 1.3.2 |

Download the source of each into its folder and build them all with:

```powershell
pwsh tools\build-deps.ps1
```

BitTorrent uses **libtorrent 2.1** with Boost and OpenSSL, and the connections between low IDs use **ngtcp2** (QUIC); all of that comes from [vcpkg](https://github.com/microsoft/vcpkg). The project looks for it two folders above the root of the repository (`..\..\vcpkg`); another path can be given with the `PegasoVcpkg` property of MSBuild.

```powershell
git clone https://github.com/microsoft/vcpkg ..\..\vcpkg
..\..\vcpkg\bootstrap-vcpkg.bat
pwsh tools\build-deps.ps1 -Only libtorrent
```

## 2. The application

Build the project directly, not the solution:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    srchybrid\emule.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```

The result is left in `srchybrid\x64\Release`: `Pegaso.exe` and the `lang` folder with the languages.

To try it without touching your user profile, copy both to a folder which contains `config\preferences.ini` (it may be empty): Pegaso then runs in portable mode and keeps everything inside that folder.

## 3. The installer

```powershell
python browser-extension\build.py
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" tools\installer\pegaso.iss
```

It makes `tools\installer\output\Pegaso-<version>-setup.exe`.

## Tools of the repository

| Path | What for |
|---|---|
| `tools/build-deps.ps1` | Builds the libraries. |
| `tools/strings/make_strings.py` | Generates `PegasoStrings.h/.cpp` (the texts of Pegaso itself, in seven languages). |
| `tools/strings/rebrand_rc.py` | Puts the name Pegaso into the texts inherited from eMule. |
| `tools/icons/make_icons.py` | Draws the emblem and regenerates the icons. |
| `tools/installer/` | Inno Setup script, and the script which publishes a release. |
| `tools/webtest/` | Automatic tests which drive the real window of Pegaso (see below). |

## Tests

The tests of `tools/webtest` start Pegaso in a test folder, drive it as a user would and check the result. They are written for Windows and Python 3, and the paths of the test folder are set at the top of `webtest.py` and `btuitest.py`.

```powershell
python -I tools\webtest\webtest.py basic     # web downloads
python -I tools\webtest\hybridtest.py        # hybrid swarm, three instances
python -I tools\webtest\ratetest.py          # file verdicts, three instances
python -I tools\webtest\btseedtest.py        # torrents between two instances
python -I tools\webtest\webseedtest.py       # web seeds of torrents
python -I tools\webtest\sidebartest.py       # side bar, search field and filters (display at 100 %)
python -I tools\webtest\sitesearchtest.py    # search on the sites of the user (a simulated site)
python -I browser-extension\build.py           # packs the browser extension (before making the installer)
python -I tools\webtest\exttest.py           # the extension loaded in Edge and what the API does for it (Node 22 or later)
python -I tools\webtest\createtest.py        # making torrents of your own files, and series of web downloads
python -I tools\webtest\renametest.py        # renaming videos with the (simulated) databases and moving them while still shared
python -I tools\webtest\proxytest.py         # the proxy of the preferences carries everything (simulated SOCKS5 and HTTP proxies)
python -I tools\webtest\privtest.py          # encrypted keys, version check, traces, web tied to the adapter, scheduler pause
python    tools\webtest\privuitest.py        # the windows of the privacy options, with screenshots (needs Pillow)
python -I tools\webtest\autotest.py          # watched folder, folders by type, playing, import, names through the adapter
python -I tools\webtest\manytest.py 1500     # downloading many search results at once
python -I tools\webtest\hybridpicktest.py    # picking, not typing, the torrent given to an eMule download
python -I tools\webtest\droptest.py          # files dragged from Explorer (moves the mouse for a few seconds)
python -I tools\webtest\kvpntest.py          # the offer of Kaspersky VPN (only if it is installed; does not touch it)
python -I tools\webtest\nattest.py           # two low IDs introduced by a simulated server
python -I tools\webtest\kadnattest.py direct # two low IDs through Kad, direct way over UDP
python -I tools\webtest\kadnattest.py buddy  # the same, through the buddy of the source (about 9 minutes)
python -I tools\webtest\nettest.py           # Connect, against the real networks
```

Almost all of them use only simulated servers on the computer itself. `nettest.py` and those which need Kad connect to the real networks.

With a VPN connected, Windows may refuse connections to an address of one of its own adapters: tests which call the local web server by the address of the network card can fail for that reason alone.

## Notes on the code

- The sources inherited from eMule use CRLF line endings and Latin-1 encoding; it is best to respect them so that diffs stay readable.
- `TorrentEngine.cpp` is compiled apart (C++17, no precompiled header) and is the only file which includes libtorrent and Boost.
- The labels of the Preferences tree are assigned by position (`uids` in `PreferencesDlg.cpp`): when pages are added or removed that list has to be updated.
- `preferences.ini` has two writers. eMule reads the whole file into memory at startup (`CPreferences::prefsIni`) and writes the whole of it back when it saves; the options of Pegaso are written straight to the file with `theApp.WriteProfile…`. `SavePreferences` reads the file again before writing (`CIni::Reload`), so that it does not put those options back to what they were at startup.
