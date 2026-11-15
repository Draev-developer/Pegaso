# Cambios

Pegaso parte de eMule 0.72a (edición de la comunidad). El historial de eMule está en `changelog_full.txt`.

## 0.2

### Seguridad
- Detector de falsos: cada resultado de búsqueda y cada descarga recibe una puntuación de 0 a 100 sin esperar a que nadie lo valore. Mira el nombre (un programa con nombre de vídeo, extensiones dobles), los nombres distintos con que circula un mismo archivo, la duración anunciada frente al tamaño, los primeros bytes de la descarga frente a su extensión, el contenido de los torrents y las valoraciones de los usuarios. Se ve en la columna «Confianza», con los motivos en el globo de ayuda; antes de descargar un resultado sospechoso, Pegaso pregunta. Solo avisa, nunca bloquea.

### Redes eMule
- Las fuentes de cada descarga se recuerdan al cerrar (hasta 20 por archivo, durante 24 horas) y se recuperan al arrancar, sin esperar a que el servidor o Kad las vuelvan a encontrar.
- Apertura de puertos en el router: activada de serie y, además de UPnP, con NAT-PMP y PCP. Un puerto solo cuenta como abierto si el router concede exactamente el pedido y lo dirige a este equipo; se renueva antes de caducar y se devuelve al salir. Si el router no tiene dirección pública (CGNAT o doble router) o el puerto ya está redirigido a otro equipo, Pegaso lo dice en el registro en lugar de dejar una ID baja sin explicación.

### Interfaz
- Banderas de país junto a las fuentes de las descargas, los clientes (subidas, cola, clientes conocidos) y los servidores; el país aparece también en el globo de cada fuente. La consulta se hace en el propio equipo con la base «IP to Country Lite» de DB-IP (CC BY 4.0); no se envía nada a ningún sitio.

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
- Al copiar un enlace de descarga (dirección web de un archivo, magnet o ed2k) se abre la ventana «Añadir enlaces de descarga»; lo que se copie mientras está abierta se suma a la lista.
- Herramientas → «Añadir enlaces de descarga…» para escribirlos o pegarlos a mano.

### Enjambre híbrido
- Una descarga de eMule puede recibir además una fuente web y un torrent del mismo archivo.
- El torrent baja solo lo que falta y sus datos pasan la comprobación de hashes de eMule; un torrent que no es el mismo archivo se descarta.
- Las fuentes web y el torrent añadidos a una descarga se conservan al reiniciar.

### Seguridad
- Marcador de archivos: correcto, mala calidad, falso o no cumple la descripción, con aviso cuando varios usuarios señalan un archivo.
- El marcador vale también para torrents: la valoración se publica en Kad y la ven los demás usuarios de Pegaso que añadan ese torrent.
- Protección VPN: uso exclusivo de un adaptador de red y corte del tráfico si se desconecta.
- Análisis de cada descarga terminada con el antivirus instalado (AMSI).
- Marca de «descargado de internet» en los archivos terminados.

### Interfaz
- Nombre, iconos e identidad propios.
- Columna «Origen» en la lista de descargas: E (eMule), T (torrent), W (web) o su combinación.
- Página de opciones «Web y BitTorrent».
- Buscador de torrents con proveedores Torznab configurados por el usuario.
- Idiomas incluidos: español, inglés, francés, italiano, portugués, alemán y ruso.
- El instalador y el programa arrancan en el idioma de Windows si está entre los incluidos.

### Técnica
- Solo Windows 10 y 11 de 64 bits; compilación con Visual Studio 2026.
- Instalador (Inno Setup) sin permisos de administrador.
- Corregido un fallo heredado por el que un enlace recibido desde otra instancia podía perderse.

### Limitaciones conocidas
- Las fuentes web de una descarga de eMule solo admiten `http://`.
- El límite de ratio de siembra se ajusta en `preferences.ini` (`TorrentSeedRatio`), no en Preferencias.
- Durante un enjambre híbrido el torrent guarda su propia copia de lo que baja hasta que la descarga termina.
- Los mensajes nuevos del registro están solo en inglés.
