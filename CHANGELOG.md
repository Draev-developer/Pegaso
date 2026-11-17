# Cambios

Pegaso parte de eMule 0.72a (edición de la comunidad). El historial de eMule está en `changelog_full.txt`.

## 0.8

- **Descargar un vídeo de una web, eligiendo calidad, idioma y nombre.** Muchas webs publican sus vídeos como una lista (HLS, `.m3u8`; DASH, `.mpd`) con varias calidades y pistas de sonido, cada una en cientos de trozos. Al dar a Pegaso la dirección de esa lista, o de una página que la lleve a la vista, se abre una ventana para elegir la calidad, el idioma y el nombre del archivo; Pegaso baja los trozos y los une. Vale el campo de enlaces de siempre y también Herramientas → «Descargar un vídeo de una web…». Guía: [docs/descargar-videos.md](docs/descargar-videos.md).
- Lo que no baja, y lo dice: vídeos cifrados, emisiones en directo, y los de las webs que no publican la lista sino que la entregan a su propio reproductor mediante un servicio privado, un inicio de sesión o una protección contra copia. No hay nada hecho a medida para ninguna web.
- **Vídeo y sonido en un solo archivo, si hay ffmpeg.** Cuando se elige idioma, vídeo y sonido viajan aparte. Si ffmpeg está en el equipo (junto a `Pegaso.exe`, en el PATH o en `FfmpegPath` de `preferences.ini`), Pegaso los une sin recodificar; si no, deja el sonido en un archivo aparte junto al vídeo y lo avisa antes de empezar. Pegaso no trae ffmpeg.
- Un enlace que resulta ser una página web ya no se descarga como archivo ni da error: se mira si lleva un vídeo.

## 0.7.1

- **Añadir un torrent a una descarga de eMule, sin escribir rutas.** «Añadir un torrent de este mismo archivo…» abre ahora una ventana con un botón **Examinar** (empieza en tu carpeta de descargas) y una lista de lo que Pegaso ha encontrado por su cuenta: los torrents de la lista de descargas y los archivos `.torrent` de tus carpetas de descargas que tienen un archivo de exactamente el mismo tamaño. Si solo hay uno, ya viene elegido: basta Aceptar. El enlace magnet o la ruta se pueden seguir escribiendo.
- **Un torrent de la lista como fuente.** Un torrent que ya está en la lista —terminado o a medias— puede dar sus datos a una descarga de eMule del mismo archivo sin dejar de ser lo que era: conserva su fila, su carpeta y lo que descarga. Los datos pasan igualmente la comprobación de hashes de eMule.
- **Lista de servidores de serie actualizada.** Pegaso trae ahora los 19 servidores de la lista de `upd.emule-security.org` del 17 de noviembre de 2026 (antes 9). Se añaden cuando la lista de servidores está vacía; quien ya tiene servidores conserva los suyos y puede actualizarlos en Servidores → «Actualizar server.met desde URL».

## 0.7

· [English](CHANGELOG.en.md)

### Uso diario
- **Reproducir mientras descarga.** Botón derecho sobre un torrent → «Reproducir mientras descarga»: Pegaso baja sus piezas en orden, primero el comienzo y el final del vídeo o audio más grande, y lo abre con el reproductor de Windows en cuanto ha llegado el comienzo. Si aún no hay bastante, se abre solo cuando lo haya. En las descargas de eMule sigue estando «Vista previa».
- **Carpeta vigilada.** Herramientas → «Carpetas automáticas…»: los archivos `.torrent`, y los `.txt` con un enlace por línea, que dejes en la carpeta que elijas se añaden solos. Lo añadido pasa a su subcarpeta `added`.
- **Lo terminado, a su carpeta según el tipo.** En la misma ventana, una carpeta para vídeos, audio, imágenes, documentos, archivos comprimidos y programas. Vale para eMule, web y BitTorrent; un torrent va entero a la carpeta de su archivo más grande. Lo movido sigue compartido en eD2K y sembrado. Las descargas con categoría propia van a la carpeta de su categoría, como siempre.
- **Importar de eMule y de qBittorrent.** Herramientas → «Importar de eMule o qBittorrent…». De eMule, Pegaso continúa las descargas sin terminar donde están (su carpeta Temp pasa a ser una carpeta temporal más de Pegaso; no se copia nada). De qBittorrent, añade los torrents de su carpeta `BT_backup` con lo que qBittorrent sabía de ellos: dónde están los archivos, cuáles se quieren y qué hay ya bajado.

- **Lo renombrado sale de Descargas.** Un vídeo al que «Renombrar vídeos» ha dado nombre o sitio ya no queda en la lista de descargas, donde parecía que no se había movido. Si venía de eMule o de la web sigue en Compartidos. Si era de un torrent, el torrent sale de la lista cuando todos sus vídeos tienen ya su sitio y se sigue sembrando hasta alcanzar el límite de ratio de las preferencias; entonces Pegaso lo suelta (los archivos se quedan). Con el límite de ratio a 0 (sembrar siempre) el torrent se queda en la lista, para que nada se siembre sin fin fuera de la vista.

### Privacidad
- **Indicador siempre a la vista.** La barra lateral dice en una línea cómo de oculta está tu dirección: verde si todo sale por la VPN o el proxy, ámbar si la dirección de tu conexión está a la vista, rojo si está todo detenido. Un clic abre «Comprobar mi conexión».
- **Los nombres de las webs, por la VPN.** Con la protección VPN, Pegaso pregunta los nombres de las webs (DNS) a los servidores del adaptador de la VPN y a ningún otro, en vez de dejarlo en manos de Windows. Si ese adaptador no tiene servidores DNS propios sigue resolviendo Windows, y «Comprobar mi conexión» lo dice.

### Corregido
- **Descargar muchos resultados a la vez dejaba Pegaso sin responder.** Al añadir cientos de archivos desde una búsqueda, la cola de descargas se reordenaba entera por cada archivo, con un método cuyo coste crecía con el cuadrado de la lista (heredado de eMule), y el índice de descargas se reescribía cada vez. Ahora se ordena y se guarda una sola vez, al final: 1500 descargas pasan de 53 segundos de bloqueo a 8.
- Con varios resultados sospechosos entre los elegidos, Pegaso hace una sola pregunta para todos (todos, solo los demás, o cancelar) en vez de abrir una ventana por archivo.

### API
- `POST /api/v1/downloads/{hash}/play`: reproduce un torrent mientras descarga.

### Documentación
- `BUILDING.en.md` y `CHANGELOG.en.md`: en inglés lo que faltaba.

## 0.6.2

Remata la revisión de privacidad de la 0.6.1. Todo está explicado en [docs/privacidad.md](docs/privacidad.md) ([English](docs/en/privacy.md)).

### Privacidad y seguridad
- **Claves cifradas.** El token de la API local, las claves de los servicios de renombrado y de tus webs de búsqueda y la contraseña del proxy ya no están en claro en `preferences.ini`: Windows los cifra para tu cuenta de usuario. Lo que estaba en claro se cifra al arrancar.
- **Comprobar mi conexión** (Herramientas, y Preferencias → Web y BitTorrent): por dónde sale cada protocolo en ese momento —proxy, adaptador de la VPN o ruta del sistema—, si el corte de emergencia está armado y si todo está detenido. También en la API: `GET /api/v1/connection`.
- **La web, atada a la VPN.** Con la protección VPN, las descargas web, las búsquedas en webs, el renombrado y la actualización de listas salen por el adaptador de la VPN, no por la ruta del sistema: Pegaso hace él mismo la conexión.
- **La web, por proxy SOCKS.** Con un proxy SOCKS5 o SOCKS4a, las peticiones web pasan por él y es el proxy quien resuelve los nombres (antes no se hacían). También valen el usuario y la contraseña del proxy, incluido un proxy HTTP con contraseña.
- **Borrar el rastro local** (Herramientas): historial de búsquedas, descargas terminadas y canceladas, recuerdo de archivos que ya no compartes y registro de actividad. Opción para hacerlo cada vez que se cierra Pegaso.
- La huella SHA-256 del instalador se publica con cada versión.

### Corregido
- **Opciones que se perdían al cerrar.** Una opción de Pegaso cambiada durante la sesión —el adaptador de la protección VPN, BitTorrent, la API local, tus webs de búsqueda, las claves— podía volver a su valor anterior al cerrar el programa o al aceptar las preferencias, si en la misma sesión cambiaba además alguna preferencia de eMule. eMule guardaba el archivo de configuración tal como lo había leído al arrancar; ahora lo relee antes de guardar. El fallo venía de versiones anteriores.

### Uso diario
- **Aviso de versión nueva, si lo quieres.** Apagado de serie. Activado en Preferencias → General, Pegaso pregunta a GitHub cada tantos días y lo dice en el registro. No descarga ni instala nada.
- **Pausa total en el programador.** Preferencias → Programador tiene una acción nueva, «Pausar todo»: en el horario que indiques no hay eD2K, Kad, BitTorrent ni web, y al terminar se vuelve a conectar. Los límites de velocidad del programador ya valían para BitTorrent y las descargas web.
- **Torrents sin su carpeta.** Opción para guardar los archivos de un torrent directamente en la carpeta de descargas. Si alguno coincidiera con un archivo que ya está ahí, el torrent conserva su carpeta.
- **Documentación en inglés**: [README.en.md](README.en.md) y `docs/en`.

## 0.6.1

Revisión de privacidad. Lo que Pegaso deja ver de ti, lo que no, y cómo ocultar tu dirección está en [docs/privacidad.md](docs/privacidad.md).

### El proxy de las preferencias vale para todo
- Hasta ahora el proxy de Preferencias → Proxy solo lo usaba eD2K: BitTorrent y las descargas web salían directas, con tu dirección a la vista. Ahora BitTorrent (pares, rastreadores y, con SOCKS5, la DHT) va por el proxy, y los nombres los resuelve el proxy.
- Lo que no puede pasar por el proxy ya no se envía. Kad y el UDP de eD2K, que en eMule salían por fuera del proxy, no se usan mientras haya proxy (`ProxyStrict=0` en `preferences.ini` devuelve el comportamiento de eMule). Las descargas web, las búsquedas en webs y el renombrado van por el proxy si es HTTP; con un proxy SOCKS, que Windows no sabe usar para la web, no se hacen y Pegaso lo dice.
- Con proxy, BitTorrent no dice qué programa es, no acepta conexiones entrantes y no pregunta en la red local.

### IPv6 y VPN
- Con una VPN que solo lleva IPv4 y sin la protección VPN de Pegaso activada, IPv6 salía por la conexión normal. Pegaso mira ahora por qué adaptador sale cada protocolo y, si no coinciden, deja de usar IPv6 en eD2K, Kad y BitTorrent y lo avisa.
- Atado a un adaptador o con proxy, BitTorrent ya no conecta con ningún par por IPv6, lo nombre quien lo nombre.
- No se anuncian direcciones IPv6 hechas con el número de la tarjeta de red.

### Otros
- La actualización de la lista de servidores y del filtro de IP respeta el corte de emergencia de la protección VPN y usa el proxy.
- La lista de títulos de AniDB se pide por HTTPS.

## 0.6

### Extensión de navegador
- Pegaso tiene extensión para Chrome, Edge y Firefox (carpeta `browser-extension`). Añade «Descargar con Pegaso» al menú del botón derecho sobre un enlace, sobre un vídeo o audio y sobre un texto seleccionado (envía todos los enlaces ed2k, magnet y web que contenga). Su botón muestra las descargas de Pegaso con su avance y admite un enlace pegado.
- Opcional, apagado de serie: que Pegaso se quede con las descargas que empieza el navegador. Si Pegaso no acepta una, el navegador sigue con ella.
- La extensión solo habla con Pegaso en el propio equipo, a través de la API local y con su clave. No envía nada a nadie más.
- API local: se admiten las peticiones de extensiones de navegador (las de páginas web siguen rechazadas), y la dirección web de un archivo .torrent se añade como torrent.

### Crear torrents
- Herramientas → «Crear torrent…» hace el torrent de un archivo o de una carpeta tuyos. Los lee una vez para calcular sus hashes (en segundo plano, con barra de avance), escribe el archivo .torrent junto a ellos, da su enlace magnet y empieza a sembrarlo sin volver a comprobar los datos.
- Los rastreadores son opcionales: sin ellos el torrent se encuentra por la red DHT. Se puede marcar como privado (solo por sus rastreadores).
- Los datos no se mueven ni se copian: se siembran desde donde están.

### Descargas en serie
- Una dirección web con un rango entre corchetes vale por toda la serie: `https://ejemplo.org/parte[01-20].zip` baja de `parte01.zip` a `parte20.zip`. El primer número marca cómo se escriben los demás: con ceros delante, todos llevan su ancho. Hasta 1000 direcciones por serie. Sirve en el campo de arriba, en «Añadir enlace», en la extensión y en la API.

### Otros
- Contacto: la ventana lleva también las cuentas de Pegaso en X (@draev_developer) e Instagram (draev.pegaso), con un botón que abre cada una.

## 0.5.3

- Contacto: un apartado nuevo en la barra lateral, con la dirección a la que escribir las sugerencias (draev.pegaso@gmail.com), un botón que abre el programa de correo con la dirección y la versión ya puestas, y otro que copia la dirección.
- En ventanas bajas, las secciones de la barra lateral se juntan para que la tarjeta de conexión siga cabiendo.

## 0.5.2

- Renombrar vídeos: al mover un vídeo a otra carpeta, la carpeta donde estaba se quita si no queda en ella ningún otro vídeo (las muestras, «sample», no cuentan). Lo que sobre va a la papelera de reciclaje con la carpeta. Lo que un torrent de la lista sigue ofreciendo (un .nfo, unos subtítulos) no se borra: se guarda aparte, con el estado del motor, y el torrent sigue completo y sembrando. Las carpetas de descargas en sí no se tocan nunca. Se puede desactivar en la propia ventana.
- Las carpetas marcadas como de solo lectura, que Windows no deja quitar sin más, se quitan también.
- Claves: la ventana «Claves…» explica cómo conseguir la de cada servicio y abre su web. Hay una guía en `docs/renombrar-videos.md`.

## 0.5.1

- Renombrar vídeos: mover muchos archivos, o archivos grandes a otro disco, ya no deja Pegaso parado. Los archivos se mueven uno tras otro en segundo plano, con una barra de avance y el nombre del que se está moviendo, y la ventana ya no retiene al resto de Pegaso: se puede seguir usando mientras tanto.
- Un archivo de un torrent se mueve igual que los demás, también a otro disco: el torrent lo suelta, el archivo se mueve y el motor toma nota de su sitio nuevo y sigue sembrándolo.
- Pantalla de arranque: «Pegaso (C) 2026 Draev - eMule (C) 2002-2026 Merkur». El aviso de eMule se conserva, como pide su licencia.

## 0.5

### Renombrar vídeos
- Herramientas → «Renombrar vídeos…» da a los vídeos terminados el nombre de la película o la serie que son, como hacen los organizadores de colecciones: `Nombre (año).ext` para las películas y `Nombre - 1x05.ext` para las series. Qué es cada archivo (película o capítulo, de qué, temporada y capítulo) se lee de su nombre; el nombre correcto, y el año de las películas, lo da la base de datos que se elija: TVmaze, TheMovieDB, TheTVDB o AniDB para las series, TheMovieDB u OMDb para las películas. Los nombres se piden en el idioma de Pegaso donde la base de datos lo permite.
- Antes de renombrar se ve la lista con el nombre de ahora y el nuevo; se marca lo que se quiere cambiar y cualquier nombre se corrige a mano (F2).
- Mover: las películas y las series pueden llevarse cada una a una carpeta, que se crea si falta.
- Un archivo renombrado o movido sigue siendo el mismo para las redes. En eD2K y Kad conserva su hash y sigue compartido sin calcularlo de nuevo, también si la carpeta nueva no es una carpeta compartida. En BitTorrent lo mueve el propio motor, que sigue sembrándolo desde el sitio nuevo, también tras cerrar y abrir Pegaso.
- Claves: TVmaze y AniDB no piden. TheMovieDB, OMDb y TheTVDB dan una gratis a quien se registra; Pegaso no trae ninguna y cada usuario escribe las suyas (botón «Claves…»).
- AniDB no tiene búsqueda para programas: publica la lista de todos sus títulos, que Pegaso baja una vez al día como mucho y consulta en casa. Donde AniDB pone delante una comprobación para navegadores, Pegaso no la salta: lo dice, y sirve la lista que el usuario baje con su navegador y deje en la carpeta de configuración.

### Otros
- Nombre de usuario: quien empieza con Pegaso se llama «Pegaso» en las redes, en vez de la dirección de la web de eMule. Quien ya tenía un nombre lo conserva.
- API local: la lista de archivos compartidos salía siempre vacía; ahora los trae, con su ruta.
- Arrastrar archivos desde el Explorador: los archivos que Pegaso abre con doble clic (.torrent y colecciones de eMule) se aceptan también al soltarlos sobre la ventana, uno o varios a la vez. Antes la ventana solo aceptaba enlaces y accesos directos de Internet.

## 0.4

### Interfaz
- Ventana principal nueva. Una barra lateral sustituye a la barra de herramientas: las secciones (Descargas, Buscar, Compartidos, Subidas, Servidores, Kad, Estadísticas) con sus contadores y, al pie, el estado de las redes con el botón de conectar.
- Sobre cada sección, un único campo: lo que se escribe se busca, y si es un enlace (ed2k, magnet o web) se ofrece para descargar. A su lado, «Añadir enlace», Herramientas y Preferencias.
- Las descargas se pueden filtrar por estado: todas, descargando, en pausa y completadas, cada una con su número.
- Barras de progreso finas con el porcentaje al lado, filas más altas y la fila seleccionada en el color del programa. El mapa de partes sigue en los detalles del archivo.
- Descargas y Subidas tienen cada una su sección; la columna de botones pequeños junto a las descargas queda oculta de serie (se recupera en Preferencias → Mostrar).

### Búsqueda
- Buscar en las webs del usuario: la lista de proveedores de la búsqueda web pasa a ser una libreta de webs. Además de una API Torznab, una web se guarda con la dirección de su página de búsqueda y `%s` donde van las palabras; Pegaso pide esa página y lista los enlaces de descarga que encuentra en ella (ed2k, magnet, archivos .torrent y archivos). Se busca en todas las webs o en la que se elija. Pegaso sigue sin traer ninguna web.
- Webs sin buscador: una dirección guardada sin `%s` que no resulta ser una fuente Torznab se toma como catálogo. Pegaso busca las palabras en lo que la página lista, abre las páginas que coinciden (ocho como mucho por búsqueda; dice cuántas quedan) y lista sus enlaces de descarga con el nombre de cada página. Lo mismo se hace con una página de resultados que solo enlaza a otras páginas. Si nada coincide, la búsqueda lo dice y explica qué dirección guardar, en vez de «0 resultados» sin más.
- Páginas para abrir a mano: cuando una página que coincide no enseña la descarga (está tras otra web o tras un paso que solo puede dar una persona), no se puede leer o no llegó a abrirse, sale en los resultados como «Página web» y el botón pasa a «Abrir la web»: se abre en el navegador del usuario, que sigue allí. Pegaso no intenta saltarse esos pasos.
- Una dirección del tipo `…/bajar.php?file=nombre.torrent` se reconoce como archivo .torrent.

### Seguridad
- Kaspersky VPN para Pegaso: donde está instalado Kaspersky VPN, Herramientas ofrece atar Pegaso a su adaptador con el corte de seguridad puesto, explica qué ajustar en Kaspersky para que la VPN lleve solo Pegaso (túnel dividido, «Apps que siempre se conectan por VPN») y lo abre. Kaspersky VPN no permite que otro programa lo conecte ni cambie sus ajustes: conectar y añadir Pegaso a su lista lo hace el usuario allí.

### Identidad
- Pegaso muestra «Created by Draev» bajo su nombre y en los créditos; el instalador lo indica como editor.

### Limitaciones conocidas
- La búsqueda en webs solo ve lo que la página trae en su HTML: no ejecuta JavaScript, no inicia sesión y no pasa comprobaciones contra programas (captchas y similares). De una página toma hasta 500 enlaces, sin tamaño ni semillas salvo en los enlaces ed2k.
- El interior de las secciones (búsqueda, servidores, Kad, estadísticas, preferencias) conserva los controles de siempre.

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
