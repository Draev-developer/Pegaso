# Pegaso 0.5

Pegaso es el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista. Esta versión pone nombre a los vídeos.

## Descarga

**`Pegaso-0.5-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de versiones anteriores y conserva la configuración y las descargas.

## Novedades

- **Renombrar vídeos.** Herramientas → «Renombrar vídeos…» propone para cada vídeo terminado el nombre de la película o la serie que es: `Nombre (año)` para las películas y `Nombre - 1x05` para las series. El nombre lo da la base de datos que elijas: TVmaze, TheMovieDB, TheTVDB o AniDB para las series; TheMovieDB u OMDb para las películas. Ves el antes y el después, marcas lo que quieres cambiar y puedes corregir cualquier nombre a mano.
- **Mover a otra carpeta.** Las películas y las series pueden ir cada una a su carpeta.
- **Sin dejar de compartir.** Un archivo renombrado o movido conserva su hash de eD2K y sigue compartido, y un archivo de un torrent se sigue sembrando desde su sitio nuevo.
- **Arrastrar archivos a la ventana.** Los archivos .torrent y las colecciones de eMule se pueden soltar sobre Pegaso desde el Explorador, uno o varios a la vez.
- **Nombre de usuario «Pegaso»** para quien empieza; quien ya tenía uno lo conserva.

La lista completa está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

## Licencia y créditos

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio.

Los nombres de películas y series vienen de TheMovieDB, OMDb, TheTVDB, TVmaze y AniDB, según elija cada usuario y con sus propias claves; Pegaso no está avalado ni certificado por ninguna de ellas. Las conexiones QUIC usan [ngtcp2](https://github.com/ngtcp2/ngtcp2) (licencia MIT) y OpenSSL. Los formatos de IPv6 y de las conexiones entre ID bajas siguen las especificaciones publicadas por el proyecto eMule Qt. Datos de países: [IP Geolocation by DB-IP](https://db-ip.com) (CC BY 4.0). Iconos de banderas: Mark James, famfamfam.com (dominio público).

Usa Pegaso solo con contenidos que tengas derecho a descargar y compartir.
