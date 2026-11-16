# Privacy in Pegaso

What those you share with know about you, what never leaves your computer, and what you can do so that your address is not seen. It describes version 0.7. · [Español](../privacidad.md)

## The essentials

- **Pegaso has no accounts and no telemetry.** There is no server of the project for Pegaso to call: nothing is registered and no statistics or error reports are sent. It does not check for new versions either, unless you turn that on.
- **Sharing between peers is not anonymous.** On eD2K, Kad and BitTorrent, whoever sends you a file or asks you for one sees your IP address. That is so in every program of this kind. What hides it is a VPN (or a proxy), and Pegaso is built so that nothing goes round it.
- **Your name does not travel.** The default user name is "Pegaso"; Pegaso neither reads nor sends your Windows name, the name of the computer or your folders.

## Check my connection

**Tools → Check my connection…** (also in Preferences → Web and BitTorrent) says, in one line per protocol, which way each thing leaves at that moment: through the proxy, only through the VPN adapter, or by the normal route of the system, with the name of the adapter and its address. It also says whether the kill switch is armed and whether everything is stopped. It refreshes by itself: if the VPN drops while the window is open, you see it.

The summary is always in sight in the side bar, in one line with a coloured dot: **green** if everything leaves through the VPN or the proxy, **amber** if the address of your connection is in sight, **red** if everything is stopped. A click on that line opens the window.

What it shows is how Pegaso is set up and which adapter it leaves through; it asks no website to find out your public address.

## What others see

| Who | What they see |
|---|---|
| Other eD2K and Kad users | Your IP and ports, the user name you have set, that you use Pegaso and its version, and a user identifier (a random number made on your computer, used by the credit system). The files you serve them or ask them for. |
| The eD2K server you connect to | The above and the list of files you share. |
| Other BitTorrent users, trackers and the DHT network | Your IP and port, that you use Pegaso, and the torrents you take part in. |
| The websites you download from or search on | Your IP and that the request is made by Pegaso. No cookies and no referring page. |
| The renaming services (TVmaze, TheMovieDB, OMDb, TheTVDB) | Your IP, the title looked up and your key for the service. Only when you use "Rename videos". |

Another user cannot ask you for the list of everything you share: that function is off by default.

## Hiding your address: VPN

**Preferences → Web and BitTorrent → VPN protection** ties Pegaso to the adapter of your VPN:

- eD2K, Kad and BitTorrent use that adapter only.
- So do web downloads, web searches, video renaming and the updates of the lists: those requests are made by Windows, which cannot be tied to an adapter, so Pegaso sends them through a bridge of its own inside the computer, which connects from the address of that adapter.
- With the **kill switch** (on by default), everything stops if the VPN drops. It resumes by itself when the VPN is back.
- While Pegaso is tied to an adapter it **does not use IPv6**, because it would go round the VPN.

If you use a VPN **without** tying Pegaso to it, Pegaso still checks which way each protocol leaves: if IPv4 leaves through one adapter and IPv6 through another (which is what happens with VPNs that carry IPv4 only), it stops using IPv6 and says so in the log. Even so, the safe thing is to turn the protection on: without it, if the VPN drops, transfers go on through your normal connection.

Limits worth knowing:

- The names of the websites (DNS) are asked by Pegaso of the DNS servers of the VPN adapter and of no other. If that adapter has no DNS servers of its own, Windows looks them up with its own settings, and "Check my connection" says so.
- That holds for the web requests. BitTorrent looks the names of its trackers up with the settings of Windows: with a "split tunnelling" VPN those queries may go to your provider, who would see which tracker you go to, though not what you download.
- The VPN takes the place of your provider as the witness: choose one you trust.

## Hiding your address: proxy

With a proxy in **Preferences → Proxy**, Pegaso uses it for everything that can go through it and **does not send what cannot**:

| | SOCKS5 | SOCKS4a | SOCKS4 | HTTP |
|---|---|---|---|---|
| eD2K (TCP connections) | through the proxy | through the proxy | through the proxy | through the proxy |
| BitTorrent: peers and trackers | through the proxy | through the proxy | through the proxy | through the proxy |
| Web downloads, web searches, renaming, lists | through the proxy | through the proxy | not made | through the proxy |
| Kad and the UDP of eD2K | not used | not used | not used | not used |
| BitTorrent DHT | through the proxy if it carries UDP | not used | not used | not used |
| IPv6, local network discovery, port mapping | not used | not used | not used | not used |

- The names of the websites are looked up by the proxy, not by your computer. Plain SOCKS4 cannot look names up: that is why no web request is made with it.
- On BitTorrent, the names of the trackers are looked up by the proxy with SOCKS5 and HTTP. With SOCKS4 and SOCKS4a your computer looks them up (your DNS sees which tracker you go to). Use SOCKS5 if you can choose.
- The user and password of the proxy are used for the web requests too.
- With a proxy, BitTorrent does not say which program it is and accepts no incoming connections.
- Kad works over UDP only, which these proxies do not carry: with a proxy it does not start. If you prefer the behaviour of eMule (UDP round the proxy, with your address in sight), set `ProxyStrict=0` in the `[eMule]` section of `preferences.ini`; "Check my connection" will remind you of it.
- Addresses of the computer itself (127.0.0.1) do not go through the proxy: they never reach the network.

## IPv6

Pegaso uses IPv6 on eD2K, Kad and BitTorrent when your connection has it. A public IPv6 address identifies your connection just as the IPv4 one does. Pegaso does not announce IPv6 addresses built from the serial number of the network card (the ones Windows only makes when its privacy setting is turned off). To not use IPv6 on eD2K and Kad: `IPv6Enabled=0` in `preferences.ini`.

## What stays on your computer

Everything is in `%LOCALAPPDATA%\Pegaso`, inside your Windows profile:

- `config\preferences.ini`: the configuration. The keys of the renaming services and of your search sites, the token of the local API and the proxy password are stored **encrypted by Windows for your user account** (they show as `dpapi:…`). Another user of the computer, or someone who copies the file to another machine, cannot read them; a program running in your session can. If you take the configuration to another computer you will have to type them again.
- `config\known.met`, `cancelled.met`, `torrents\`: what you have downloaded, cancelled and shared.
- `config\AC_SearchStrings.dat`: the latest searches, for autocompletion.
- Activity log on disk: off by default.

### Clearing the traces

**Tools → Clear local traces…** deletes in one go the search history, the finished downloads of the list, the memory of cancelled downloads and of files you no longer share, and the activity log. It touches neither the downloads in progress nor the shared files.

To have it done every time you close Pegaso: **Preferences → Web and BitTorrent → Clear local traces when Pegaso closes**.

The downloaded files stay in their folder, of course, and Windows may keep its own traces of them (recent files, for instance).

Uninstalling keeps the configuration folder; delete it by hand if you want nothing left.

## Local API and browser extension

- The API is **off** by default. Turned on, it listens on `127.0.0.1` only, demands a random token and rejects the requests a web page may try to make from the browser.
- The extension only talks to Pegaso on your computer. It has no permission to read the pages you visit, nor cookies, nor history; it sends nothing anywhere else. It only looks at the downloads of the browser if you turn on that Pegaso takes them over.

## Other details

- **Check for a new version.** Off by default. If you turn it on in Preferences → General, Pegaso asks GitHub (`api.github.com`) which is the latest published version, at most once every so many days as you set, and tells you in the log. GitHub then sees the address you leave from (that of your VPN or proxy if you use them). Nothing is downloaded or installed.
- **Antivirus.** Finished downloads are handed to the antivirus installed on your computer. What it does with them (sending samples to its cloud, for instance) depends on its settings, not on Pegaso.
- **Country flags.** They come from a file included in the installation; nothing is looked up on the network.
- **First connection.** If there is no server list and no Kad contacts, Pegaso downloads a list of contacts from `upd.emule-security.org`, which sees your IP at that moment.
- **Torrents you make.** They carry the names of the files, "Pegaso" as the program and the date. They carry no paths of your disk and not your name.
- **eMule collections you make.** They carry your Pegaso user name as the author.
- **Contact.** If you write to the project by e-mail, the recipient sees your address, as with any e-mail.
- **IRC, web interface and e-mail notifications** of eMule are still there, turned off. They are old functions: if you turn them on, the web interface listens on the network and IRC shows your address to the server.
- **The bridge of the web requests** listens on `127.0.0.1` only, on a port chosen by Windows, and only exists while Pegaso is tied to an adapter or uses a proxy.

## Checking the installer

The installer is not digitally signed, so Windows may warn when you open it. Each release publishes the SHA-256 fingerprint of its installer on the releases page; to check it:

```
certutil -hashfile Pegaso-0.7-setup.exe SHA256
```
