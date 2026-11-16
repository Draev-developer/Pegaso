# Pegaso 0.5.2

Versión de mejora de Pegaso, el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista.

## Descarga

**`Pegaso-0.5.2-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de versiones anteriores y conserva la configuración y las descargas.

## Novedades

- **La carpeta de origen desaparece.** Al mover un vídeo a otra carpeta, la carpeta donde estaba se quita si no queda en ella ningún otro vídeo. Lo que sobre va a la papelera de reciclaje; lo que un torrent sigue ofreciendo se guarda aparte para que siga sembrando.
- **Cómo conseguir las claves.** La ventana «Claves…» explica los pasos de cada servicio y abre su web. Guía completa: [docs/renombrar-videos.md](https://github.com/Draev-developer/Pegaso/blob/main/docs/renombrar-videos.md).

Lo demás es lo de la [0.5](https://github.com/Draev-developer/Pegaso/releases/tag/v0.5) y la [0.5.1](https://github.com/Draev-developer/Pegaso/releases/tag/v0.5.1). La lista completa está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

## A tener en cuenta

- Una carpeta donde queda otro vídeo no se toca, y las carpetas de descargas en sí no se quitan nunca.
- Quitar la carpeta se puede desactivar en la propia ventana de renombrar.

## Licencia y créditos

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio.

Los nombres de películas y series vienen de TheMovieDB, OMDb, TheTVDB, TVmaze y AniDB, según elija cada usuario y con sus propias claves; Pegaso no está avalado ni certificado por ninguna de ellas. Las conexiones QUIC usan [ngtcp2](https://github.com/ngtcp2/ngtcp2) (licencia MIT) y OpenSSL. Los formatos de IPv6 y de las conexiones entre ID bajas siguen las especificaciones publicadas por el proyecto eMule Qt. Datos de países: [IP Geolocation by DB-IP](https://db-ip.com) (CC BY 4.0). Iconos de banderas: Mark James, famfamfam.com (dominio público).

Usa Pegaso solo con contenidos que tengas derecho a descargar y compartir.
