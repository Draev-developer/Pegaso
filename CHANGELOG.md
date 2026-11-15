# Cambios

Pegaso parte de eMule 0.72a (edición de la comunidad). El historial de eMule está en `changelog_full.txt`.

## 0.3.1

### Redes eMule
- Transferencias entre dos ID bajas a través de Kad, sin depender del servidor, por dos vías:
  - A través del compañero (*buddy*) de la fuente: Pegaso le envía una petición que cualquier compañero, también el de un eMule sin modificar, reenvía a la fuente; la fuente abre su router hacia nosotros y la conexión se hace sobre QUIC. Cuando Pegaso hace de compañero, además le dice al que pregunta dónde está el cliente al que sirve.
  - Directa, con las fuentes que tienen el puerto UDP abierto y lo anuncian en Kad (etiqueta `nat`): se les pide por UDP que esperen una llamada QUIC.
- Pegaso anuncia en sus registros de fuente de Kad que acepta llamadas QUIC cuando no puede recibir conexiones.
- Con ID baja, las fuentes de Kad que tampoco pueden recibir conexiones ya no se descartan.

### Limitaciones conocidas
- Las dos vías de Kad solo sirven con fuentes que hablen este dialecto (Pegaso, eMule Qt, eMuleAI); probadas entre dos Pegaso con un nodo Kad y un compañero simulados, no con otros programas ni a través de routers reales.
- Sigue faltando la presentación a través de servidores Lugdunum.

## 0.3

### Redes eMule
- IPv6 para fuentes de eD2K y Kad, con la extensión que usan eMule Qt y otros clientes: Pegaso escucha en IPv6, anuncia su dirección en el saludo, en el inicio de sesión del servidor y en Kad cuando sabe que es alcanzable, y llama directamente por IPv6 a las fuentes con ID baja que la anuncian. Entiende las fuentes IPv6 que entregan los servidores y la petición de llamada por IPv6. Claves en `preferences.ini`: `IPv6Enabled`, `IPv6Address`, `IPv6AssumeReachable`.
- Transferencias entre dos ID bajas: el servidor presenta a los dos clientes (protocolo de ed2k-server 0.9.84 o posterior), ambos abren sus routers con datagramas UDP y la conexión eD2K viaja dentro de un enlace QUIC, con el dialecto de eMuleAI, aMule y eMule Qt. Clave `NatTraversal` en `preferences.ini`.
- IPv6 queda fuera mientras Pegaso está atado a un adaptador (VPN) o usa un proxy; los enlaces QUIC, solo con proxy (con VPN salen por el adaptador atado, como el resto del tráfico UDP).

### Interfaz
- API local para otros programas del equipo: REST y MCP (para asistentes de IA). Desactivada de serie; se activa en Herramientas → «API local (REST y MCP)…». Solo escucha en 127.0.0.1, pide una clave secreta y rechaza las peticiones que vienen de páginas web. Permite ver y manejar descargas, buscar en eD2K y Kad y conectar o desconectar. Descrita en `docs/api.md`.

### Técnica
- El instalador admite otra carpeta de salida (`ISCC /DOutDir=…`).
- Dependencia nueva: ngtcp2 (QUIC), a través de vcpkg.

### Limitaciones conocidas
- La presentación entre dos ID bajas solo funciona con servidores que hablan el protocolo de ed2k-server; faltan la vía de Kad y la de los servidores Lugdunum.
- Los datagramas QUIC se ofuscan solo cuando se conoce la IP pública propia al crear el enlace; si no, van sin ofuscar.
- De la extensión IPv6 faltan el intercambio de fuentes ampliado, las fuentes IPv6 en enlaces ed2k y UDP sobre IPv6.
- El filtro de IP no cubre direcciones IPv6.

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
