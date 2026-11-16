# Pegaso 0.7

Pegaso es el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista. Esta versión trae comodidades para el uso diario —ver un vídeo mientras baja, una carpeta vigilada, lo terminado a su carpeta, traer las descargas de otros programas— y dos mejoras de privacidad. *English below.*

## Descarga

- **`Pegaso-0.7-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de versiones anteriores y conserva la configuración y las descargas. Incluye la extensión.
- **`Pegaso-0.7-extension-chromium.zip`** y **`Pegaso-0.7-extension-firefox.zip`** — la extensión sola (no cambia respecto a la 0.6).

## Qué cambia

- **Reproducir mientras descarga.** Botón derecho sobre un torrent: Pegaso lo baja en orden y abre el vídeo o el audio en cuanto ha llegado el comienzo.
- **Carpeta vigilada.** Los `.torrent` y las listas de enlaces (`.txt`) que dejes en una carpeta se añaden solos.
- **Lo terminado, a su carpeta.** Una carpeta para cada tipo —vídeos, audio, imágenes, documentos, comprimidos, programas—, sin dejar de compartir ni de sembrar.
- **Importar de eMule y qBittorrent.** Pegaso continúa las descargas a medias de eMule donde están y añade los torrents de qBittorrent con lo que ya tenían bajado.
- **Indicador de privacidad en la barra lateral.** Verde, ámbar o rojo, siempre a la vista; un clic abre «Comprobar mi conexión».
- **Los nombres de las webs, por la VPN.** Con la protección VPN, las consultas DNS de las peticiones web van a los servidores del adaptador de la VPN.
- **Corregido: descargar cientos de resultados de una búsqueda a la vez dejaba Pegaso sin responder.** Ahora tarda segundos, y si hay varios sospechosos pregunta una sola vez.
- **Lo renombrado sale de Descargas.** Tras «Renombrar vídeos», el vídeo ya no queda en la lista de descargas; sigue compartido, y un torrent se sigue sembrando hasta su límite de ratio.
- **Documentación en inglés completa**: se suman la guía de compilación y la lista de cambios.

Las tres primeras están en **Herramientas → Carpetas automáticas…** y en el menú del botón derecho; la importación, en **Herramientas → Importar de eMule o qBittorrent…**. La lista completa está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

---

## English

Pegaso is a free P2P client for Windows which downloads from the eMule networks (eD2K and Kad), BitTorrent and the web in a single list.

- **Play while downloading.** Right-click a torrent: Pegaso fetches it in order and opens the video or audio as soon as its beginning has arrived.
- **Watched folder.** `.torrent` files and lists of links (`.txt`) left in a folder are added by themselves.
- **Finished downloads to their folder.** One folder per type — video, audio, images, documents, archives, programs — still shared and seeded.
- **Import from eMule and qBittorrent.** Pegaso goes on with the unfinished downloads of eMule where they are, and adds the torrents of qBittorrent with what they had.
- **Privacy indicator in the side bar.** Green, amber or red, always in sight; a click opens "Check my connection".
- **Website names through the VPN.** With the VPN protection on, the DNS queries of the web requests go to the servers of the VPN adapter.
- **Fixed: downloading hundreds of search results at once left Pegaso not responding.** It now takes seconds, with a single question if several are suspicious.
- **Renamed videos leave Downloads.** After "Rename videos" the video no longer stays in the download list; it is still shared, and a torrent goes on being seeded up to its ratio limit.
- **All the documentation in English** now: [BUILDING.en.md](https://github.com/Draev-developer/Pegaso/blob/main/BUILDING.en.md), [CHANGELOG.en.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.en.md).

## Licencia y créditos / License

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio. Pegaso derives from eMule 0.72a (community edition); the full source code is in this repository.
