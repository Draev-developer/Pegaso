# Cambios

Pegaso parte de eMule 0.72a (edición de la comunidad). El historial de eMule está en `changelog_full.txt`.

## 0.1 — primera versión

### Redes eMule
- Servidores eD2K de serie y direcciones para actualizar la lista de servidores.
- **Conectar** entra siempre en un servidor y en Kad; si Kad no conoce contactos, descarga una lista inicial.
- Sin chat IRC ni mensajes entre usuarios.

### BitTorrent
- Enlaces magnet y archivos `.torrent` en la misma lista de descargas (motor libtorrent 2.1).
- Selector de archivos del torrent; los accesos directos `.url` van desmarcados.
- Mapa de piezas en la barra de progreso.
- Siembra visible (velocidad, total compartido) con límite de ratio.
- Semillas web: las del propio torrent y las que añada el usuario.
- Los torrents terminados se ofrecen también en eD2K y Kad.

### Descargas web
- HTTP y HTTPS con varias conexiones por archivo, pausa, reanudación tras reiniciar y límite de velocidad compartido con el resto de descargas.
- Los enlaces llegan por el portapapeles, por el diálogo de pegar enlaces o por la línea de órdenes.

### Enjambre híbrido
- Una descarga de eMule puede recibir además una fuente web y un torrent del mismo archivo.
- El torrent baja solo lo que falta y sus datos pasan la comprobación de hashes de eMule; un torrent que no es el mismo archivo se descarta.

### Seguridad
- Marcador de archivos: correcto, mala calidad, falso o no cumple la descripción, con aviso cuando varios usuarios señalan un archivo.
- Protección VPN: uso exclusivo de un adaptador de red y corte del tráfico si se desconecta.
- Análisis de cada descarga terminada con el antivirus instalado (AMSI).
- Marca de «descargado de internet» en los archivos terminados.

### Interfaz
- Nombre, iconos e identidad propios.
- Página de opciones «Web y BitTorrent».
- Buscador de torrents con proveedores Torznab configurados por el usuario.
- Idiomas incluidos: español, inglés, francés, italiano, portugués, alemán y ruso.

### Técnica
- Solo Windows 10 y 11 de 64 bits; compilación con Visual Studio 2026.
- Instalador (Inno Setup) sin permisos de administrador.
- Corregido un fallo heredado por el que un enlace recibido desde otra instancia podía perderse.

### Limitaciones conocidas
- Las fuentes web de una descarga de eMule solo admiten `http://`.
- El límite de ratio de siembra se ajusta en `preferences.ini` (`TorrentSeedRatio`), no en Preferencias.
- Durante un enjambre híbrido el torrent guarda su propia copia de lo que baja hasta que la descarga termina.
- Los mensajes nuevos del registro están solo en inglés.
