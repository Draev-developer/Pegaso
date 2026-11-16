# Pegaso 0.2

Segunda versión de Pegaso, el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista.

## Descarga

**`Pegaso-0.2-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de la 0.1 y conserva la configuración y las descargas.

## Novedades

- **Detector de falsos.** La columna «Confianza» puntúa cada resultado de búsqueda y cada descarga sin esperar a que nadie lo valore: avisa de un programa con nombre de vídeo, de un archivo cuyo contenido no corresponde a su extensión, de un mismo archivo que circula con nombres que no tienen que ver, de una duración que no cabe en el tamaño o de un torrent con un programa camuflado. El motivo aparece en el globo de ayuda y Pegaso pregunta antes de descargar un resultado sospechoso. Solo avisa: la decisión es del usuario.
- **Las fuentes se recuerdan.** Al cerrar se guardan hasta 20 fuentes por descarga y al arrancar se recuperan, sin esperar a que el servidor o Kad las vuelvan a encontrar.
- **Apertura de puertos más fiable.** Activada de serie y, además de UPnP, con NAT-PMP y PCP. Un puerto solo cuenta como abierto si el router concede exactamente el pedido. Si el router no tiene dirección pública (CGNAT o doble router) o el puerto ya está redirigido a otro equipo, Pegaso lo explica en el registro.
- **Banderas de país** junto a servidores, fuentes y clientes. La consulta se hace en el propio equipo; no se envía nada a ningún sitio.

La lista completa está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

## Licencia y créditos

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio.

Datos de países: [IP Geolocation by DB-IP](https://db-ip.com) (base «IP to Country Lite», CC BY 4.0). Iconos de banderas: Mark James, famfamfam.com (dominio público).

Usa Pegaso solo con contenidos que tengas derecho a descargar y compartir.
