# The Pegaso extension for the browser

It adds "Download with Pegaso" to Chrome, Edge and Firefox. What you send shows up at once in the download list of Pegaso. · [Español](../extension-navegador.md)

## What it does

- **Right-click a link** → "Download with Pegaso". It works for ed2k links, magnet links, .torrent files and files on the web.
- **Right-click a selected text** → sends in one go all the links it contains.
- **Right-click a video or an audio** → sends its file.
- **The button of the extension** shows the downloads of Pegaso with their progress and takes a pasted link.
- **Optional:** let Pegaso take the downloads the browser starts. It is off by default.

The extension only talks to Pegaso on your own computer. It sends nothing to anyone else and keeps no record of the pages you visit.

## 1. Turn on the API of Pegaso

The extension talks to Pegaso through its local API, which is off by default.

1. In Pegaso: **Tools → Local API (REST and MCP)…**
2. Pegaso turns it on, shows a key and copies it to the clipboard. Keep it: it has to be pasted into the extension.

Pegaso 0.6 or later is needed, and it has to be open when you use the extension.

## 2. Install the extension

The extension comes with Pegaso, in the `browser-extension` folder of the place you installed it to (normally `C:\Program Files\Pegaso\browser-extension`). It is also in every published release, at <https://github.com/Draev-developer/Pegaso/releases>.

### Chrome and Edge

1. Open `chrome://extensions` (in Edge, `edge://extensions`).
2. Turn on **Developer mode**.
3. Press **Load unpacked** and choose the `browser-extension\chrome-edge` folder.

It stays installed for good. Chrome may remind you at startup that you have an extension in developer mode; that is normal as long as it is not in its store.

### Firefox

Firefox only installs for good the extensions signed by Mozilla. As long as the one of Pegaso is not, it can be loaded temporarily:

1. Open `about:debugging#/runtime/this-firefox`.
2. Press **Load Temporary Add-on…** and choose `pegaso-firefox.zip`.

It is removed when Firefox closes and has to be loaded again.

## 3. Connect it

1. Open the options of the extension (in Chrome and Edge, press its button and then "Options").
2. Paste the key of Pegaso and press **Save and test**. It must say "Connected to Pegaso" with its version.
3. The port is `4780` unless you have changed it.

## If something fails

| Message | What is going on |
|---|---|
| "Pegaso does not answer" | Pegaso is closed, or its local API is not turned on. |
| "Pegaso does not accept the key" | The key was copied wrong. Copy it again from Tools → Local API. |
| "This Pegaso does not accept browser extensions yet" | Your Pegaso is older than 0.6: update it. |
| "There is no ed2k, magnet or web link in that" | The selection holds no link Pegaso can take. |

## The downloads of the browser

With the box "Let Pegaso take the downloads of the browser" ticked, every download the browser starts is handed to Pegaso and the one of the browser is cancelled.

- Downloads which demand being logged in to the website may fail in Pegaso, which does not have the sessions of your browser.
- If Pegaso does not take a download (because it is closed, for instance), the browser goes on with it as if nothing had happened.

## For developers

The code is in `browser-extension/src` and is the same for every browser; only the manifest changes. `python browser-extension/build.py` builds both variants in `browser-extension/dist`. The test is `tools/webtest/exttest.py`.
