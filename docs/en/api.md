# Local API of Pegaso (REST and MCP)

Pegaso can serve other programs on the same computer: scripts, and AI assistants through the MCP protocol. With the API you can see and handle the downloads, search eD2K and Kad, and connect or disconnect the program. · [Español](../api.md)

## Turning it on

**Tools → Local API (REST and MCP)…** The first time, Pegaso makes a secret key, shows it and copies it to the clipboard. It is off by default.

- REST address: `http://127.0.0.1:4780/api/v1`
- MCP address: `http://127.0.0.1:4780/mcp`
- The port is changed with `ApiPort` in `preferences.ini`. The key is kept there as `ApiToken`, encrypted for your Windows account; to see it again, open the same menu entry.

## Security

- It listens on `127.0.0.1` only: it cannot be reached from the network.
- Every request carries the key: `Authorization: Bearer <key>`. Without it, the answer is 401.
- Requests whose `Host` is not this computer and those sent by a web page (an `Origin` header which is not local) are rejected (403), so that a website cannot reach the API through the browser.
- A browser extension (`chrome-extension://…`, `moz-extension://…`) is not a web page and is admitted, with the key like anyone else: that is how the [Pegaso extension](browser-extension.md) works.
- Whoever has the key can add and remove downloads. Treat it as a password.

## REST

All answers are JSON. Errors carry `{"error": "..."}` and the matching HTTP code.

| Method and address | What it does |
|---|---|
| `GET /api/v1/status` | Version, state of eD2K and Kad, speeds, number of downloads and of shared files, and `hiddenSeeds`: torrents seeded out of the download list because their videos have been renamed or moved |
| `GET /api/v1/connection` | `{"report": "…"}`: the text of "Check my connection" — which way each protocol leaves right now (proxy, VPN adapter or system route), in the language of the program |
| `GET /api/v1/downloads` | List of downloads |
| `POST /api/v1/downloads` | Adds downloads. Body: `{"links": ["ed2k://…", "magnet:…", "https://…"]}`. The web address of a `.torrent` file is added as a torrent; with `"kind": "torrent"` every web address is taken for a `.torrent` file, even if it does not look like one. A web address with a range, `…/part[01-20].zip`, stands for the whole series |
| `GET /api/v1/downloads/{hash}` | One download |
| `POST /api/v1/downloads/{hash}/pause` | Pauses |
| `POST /api/v1/downloads/{hash}/resume` | Resumes |
| `POST /api/v1/downloads/{hash}/play` | Plays a torrent while it downloads: fetches it in order and opens its largest video or audio when the beginning has arrived. Returns `{"opened": true}` if it has been opened already, or `false` with a `message` saying how much is missing |
| `DELETE /api/v1/downloads/{hash}` | Removes the download. What was received of an unfinished download is deleted |
| `GET /api/v1/shared` | Shared files, with their ed2k link (up to 1000) |
| `POST /api/v1/searches` | Starts a search. Body: `{"query": "…", "network": "server"｜"global"｜"kad", "type": "video"｜"audio"｜…}`. Returns `{"id": n}` |
| `GET /api/v1/searches/{id}` | Results received so far (up to 500) |
| `POST /api/v1/searches/{id}/download` | Downloads a result. Body: `{"hash": "…"}` |
| `POST /api/v1/connect`, `POST /api/v1/disconnect` | Connects or disconnects eD2K and Kad |

Every download and every search result carries `trust`, the verdict of the fake detector: `score` from 0 (nothing against it) to 100, `label` and `reasons`.

Example:

```
curl -H "Authorization: Bearer KEY" http://127.0.0.1:4780/api/v1/downloads
curl -H "Authorization: Bearer KEY" -H "Content-Type: application/json" \
     -d "{\"links\": [\"https://example.org/file.zip\"]}" http://127.0.0.1:4780/api/v1/downloads
```

## MCP

The `/mcp` address speaks the Model Context Protocol over HTTP (JSON-RPC 2.0, versions 2025-06-18, 2025-03-26 and 2024-11-05). It offers these tools:

`pegaso_status`, `pegaso_list_downloads`, `pegaso_add_download`, `pegaso_pause_download`, `pegaso_resume_download`, `pegaso_remove_download`, `pegaso_list_shared`, `pegaso_search`, `pegaso_search_results`, `pegaso_download_result`, `pegaso_connect`, `pegaso_disconnect`.

To add it to an MCP client which supports HTTP servers with headers, give it the address and the header with the key. In many clients it is written like this:

```
{ "mcpServers": { "pegaso": { "type": "http", "url": "http://127.0.0.1:4780/mcp", "headers": { "Authorization": "Bearer KEY" } } } }
```

The server answers each request with a single JSON message; it uses neither SSE streams nor sessions.

## Limits

- One request at a time; long answers are cut at 500 results or 1000 files.
- There is no address yet to change preferences, choose the files of a torrent or rate files.
- A server search replaces the previous server search, as in the window of Pegaso.
