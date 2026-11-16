# Pegaso 0.6

Pegaso es el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista. Esta versión llega al navegador y aprende a publicar.

## Descarga

- **`Pegaso-0.6-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de versiones anteriores y conserva la configuración y las descargas. Incluye la extensión.
- **`Pegaso-0.6-extension-chromium.zip`** y **`Pegaso-0.6-extension-firefox.zip`** — la extensión sola, para Chrome y Edge y para Firefox.

## Novedades

- **Extensión de navegador.** «Descargar con Pegaso» en el menú del botón derecho de Chrome, Edge y Firefox: sobre un enlace, sobre un vídeo o audio, o sobre un texto seleccionado con varios enlaces. Su botón muestra las descargas de Pegaso y admite un enlace pegado. Si quieres, Pegaso se queda también con las descargas que empieza el navegador.
- **Crear torrents.** Herramientas → «Crear torrent…»: eliges un archivo o una carpeta tuyos y Pegaso hace el .torrent, te da el enlace magnet y empieza a sembrarlo desde donde está. Los rastreadores son opcionales.
- **Descargas en serie.** Una dirección como `https://ejemplo.org/parte[01-20].zip` baja las veinte partes de una vez.
- **Privada.** La extensión solo habla con Pegaso en tu propio equipo, con una clave que tú le das.
- **Pegaso en las redes.** El apartado «Contacto» lleva las cuentas de Pegaso en [X](https://x.com/draev_developer) e [Instagram](https://www.instagram.com/draev.pegaso/).

Cómo instalarla y conectarla: [docs/extension-navegador.md](https://github.com/Draev-developer/Pegaso/blob/main/docs/extension-navegador.md). La lista completa de cambios está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

## A tener en cuenta

- La extensión todavía no está en las tiendas de Chrome, Edge ni Firefox. En Chrome y Edge se carga desde una carpeta y queda instalada; en Firefox, mientras no esté firmada, solo se puede cargar de forma temporal.
- Se ha probado cargada en Edge. En Chrome usa el mismo código; en Firefox no se ha probado todavía.
- Las descargas del navegador que exigen tener la sesión iniciada en la web pueden fallar en Pegaso.
- Un torrent sin rastreadores solo lo encuentran otros por la red DHT, y para que te lleguen conexiones conviene tener el puerto de BitTorrent abierto en el router.
- Crea torrents solo de contenidos que tengas derecho a compartir.

## Licencia y créditos

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio.

Los nombres de películas y series vienen de TheMovieDB, OMDb, TheTVDB, TVmaze y AniDB, según elija cada usuario y con sus propias claves; Pegaso no está avalado ni certificado por ninguna de ellas. Las conexiones QUIC usan [ngtcp2](https://github.com/ngtcp2/ngtcp2) (licencia MIT) y OpenSSL. Los formatos de IPv6 y de las conexiones entre ID bajas siguen las especificaciones publicadas por el proyecto eMule Qt. Datos de países: [IP Geolocation by DB-IP](https://db-ip.com) (CC BY 4.0). Iconos de banderas: Mark James, famfamfam.com (dominio público).

Usa Pegaso solo con contenidos que tengas derecho a descargar y compartir.
