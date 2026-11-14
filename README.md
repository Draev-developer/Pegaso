# Pegaso

**Un solo programa para descargar de eMule, BitTorrent y la web.**

Pegaso es un cliente P2P libre para Windows, derivado de [eMule](https://www.emule-project.net) 0.72a (edición de la comunidad). Conserva todo lo que hace eMule en las redes eD2K y Kad y añade BitTorrent, descargas web y varias medidas de seguridad, todo en la misma lista de descargas.

![Pegaso descargando un torrent](docs/img/pegaso-descargas.png)

## Qué hace

| | |
|---|---|
| **Redes eMule** | Servidores eD2K y Kad, compatibles con cualquier cliente eMule. Trae servidores de serie y arranca Kad sin configuración: basta pulsar **Conectar**. |
| **BitTorrent** | Enlaces magnet y archivos `.torrent` (motor libtorrent). Selección de archivos, mapa de piezas, siembra con límite de ratio y semillas web. |
| **Descargas web** | HTTP y HTTPS con varias conexiones por archivo, pausa y reanudación tras cerrar el programa. |
| **Enjambre híbrido** | Una misma descarga puede recibir datos de fuentes eMule, de un servidor web y de un torrent a la vez. Todo lo que llega se comprueba con los hashes de eMule. |
| **Marcador de archivos** | Quien tiene un archivo puede avisar a los demás: correcto, mala calidad, falso o no cumple la descripción (por ejemplo, el nombre dice español y está en inglés). Los archivos señalados por varios usuarios aparecen como sospechosos. |
| **Protección VPN** | Pegaso puede atarse a un adaptador de red y cortar todo el tráfico si la VPN se cae. |
| **Seguridad** | Cada descarga terminada se pasa al antivirus instalado (AMSI) y queda marcada como procedente de internet, igual que hacen los navegadores. |
| **Buscador de torrents** | Consulta los proveedores Torznab que configure el usuario (por ejemplo, una instalación propia de Jackett o Prowlarr). Pegaso no trae ninguno. |
| **Idiomas** | Español, inglés, francés, italiano, portugués, alemán y ruso. |

## Instalación

Descarga el instalador de la [última versión](https://github.com/Draev-developer/Pegaso/releases/latest) y ejecútalo. No necesita permisos de administrador.

- Requisitos: Windows 10 u 11 de 64 bits.
- La configuración se guarda en `%LOCALAPPDATA%\Pegaso` y las descargas en la carpeta `Pegaso` dentro de Descargas. Desinstalar no borra ninguna de las dos.

## Primeros pasos

1. Pulsa **Conectar**. Pegaso entra en un servidor eD2K y en la red Kad.
2. Pega un enlace `ed2k://`, un enlace `magnet:`, un archivo `.torrent` o una dirección web para empezar una descarga.
3. En **Preferencias → Web y BitTorrent** están el puerto de BitTorrent, las conexiones por descarga web y la protección VPN.

Para recibir conexiones entrantes (ID alta en eMule), abre en el router los puertos que aparecen en **Preferencias → Conexión**.

### Enjambre híbrido

Con el botón derecho sobre una descarga de eMule:

- **Añadir fuente web…**: dirección `http://` donde está ese mismo archivo.
- **Añadir un torrent de este mismo archivo…**: enlace magnet o archivo `.torrent` que contenga un archivo de exactamente el mismo tamaño.

El torrent solo baja lo que a la descarga le falta. Si sus datos no pasan la comprobación de eMule, no es el mismo archivo y Pegaso lo descarta.

### Marcador de archivos

Botón derecho sobre una descarga o un archivo compartido → **Valorar este archivo…**. La valoración viaja por el sistema de comentarios de eMule, así que también la ven los usuarios de eMule.

![Diálogo para valorar un archivo](docs/img/pegaso-valorar.png)

## Uso responsable

Pegaso es una herramienta de transferencia de archivos. Úsala solo con contenidos que tengas derecho a descargar y compartir. El programa no incluye ni enlaza índices de contenidos.

## Compilar

Las instrucciones están en [BUILDING.md](BUILDING.md). Hace falta Visual Studio 2026 con el componente MFC.

## Licencia y créditos

Pegaso se distribuye bajo la **Licencia Pública General de GNU, versión 2** ([license.txt](license.txt)).

- Deriva de **eMule** © 2002-2026 Merkur y el equipo de eMule, a partir de la edición de la comunidad mantenida por [irwir](https://github.com/irwir/eMule).
- BitTorrent mediante [libtorrent](https://libtorrent.org) (Arvid Norberg), con Boost y OpenSSL.
- También usa Crypto++, mbed TLS, miniupnpc, zlib, id3lib y ResizableLib.

Los cambios respecto a eMule 0.72a están en [CHANGELOG.md](CHANGELOG.md) y en el historial de este repositorio.

---

## English

Pegaso is a free P2P client for Windows derived from eMule 0.72a (community edition). It keeps everything eMule does on the eD2K and Kad networks and adds BitTorrent (magnet links and `.torrent` files, via libtorrent), multi-connection HTTP/HTTPS downloads, hybrid downloads (one file fed by eMule sources, a web server and a torrent at once, verified with the eD2K hashes), file verdicts (correct / poor quality / fake / does not match its description), VPN binding with a kill switch, an antivirus check of finished downloads, and a torrent search through user-configured Torznab providers.

Download the installer from the [latest release](https://github.com/Draev-developer/Pegaso/releases/latest). Windows 10/11, 64-bit. Licensed under the GNU GPL v2; see [BUILDING.md](BUILDING.md) to build it from source.
