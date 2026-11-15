# Pegaso 0.4

Pegaso es el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista. Esta versión estrena ventana principal.

## Descarga

**`Pegaso-0.4-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de versiones anteriores y conserva la configuración y las descargas.

## Novedades

- **Ventana principal nueva.** Una barra lateral sustituye a la barra de herramientas: Descargas, Buscar, Compartidos, Subidas, Servidores, Kad y Estadísticas, con sus contadores. Al pie, el estado de las redes y el botón de conectar.
- **Un único campo para buscar y para descargar.** Lo que se escribe se busca; si es un enlace ed2k, magnet o web, se ofrece para descargar.
- **Filtros de descargas por estado**: todas, descargando, en pausa y completadas, cada una con su número.
- **Buscar en tus webs.** La búsqueda web tiene ahora una libreta de webs: guarda la dirección de la página de búsqueda de cada una, con `%s` donde van las palabras, y Pegaso lista los enlaces de descarga que devuelve. Se puede buscar en todas o en una sola. Pegaso no trae ninguna.
- **Webs sin buscador y descargas que no están a la vista.** Una web que solo es un catálogo se guarda con la dirección de la página que lo lista todo: Pegaso busca tus palabras en la lista, abre las páginas que coinciden y enseña sus descargas. Cuando la descarga no está en la página, sale la propia página y el botón la abre en tu navegador para que sigas tú.
- **Kaspersky VPN para Pegaso.** Si tienes Kaspersky VPN, Herramientas ofrece atar Pegaso a su adaptador (nada sale si la VPN está apagada), te dice qué ajustar en Kaspersky para que la VPN lleve solo Pegaso y lo abre.
- **Listas más claras**: barra de progreso fina con el porcentaje, filas más altas y la fila seleccionada en el color del programa.
- **Transferencias entre dos ID bajas a través de Kad** (de la 0.3.1): por el compañero de la fuente o directamente por UDP, sin depender del servidor.

La lista completa está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

## A tener en cuenta

- El interior de las secciones (búsqueda, servidores, Kad, estadísticas, preferencias) conserva los controles de siempre.
- La búsqueda en webs solo ve lo que la página trae en su HTML: no ejecuta JavaScript, no inicia sesión y no pasa captchas.
- El mapa de partes de cada descarga ya no se ve en la lista; sigue en los detalles del archivo.
- La columna de botones pequeños junto a las descargas queda oculta de serie; se recupera en Preferencias → Mostrar. Sus acciones siguen en el menú contextual.
- Las conexiones entre ID bajas a través de Kad solo sirven con fuentes que hablen el mismo dialecto (Pegaso, eMule Qt, eMuleAI) y se han probado entre dos Pegaso, no todavía con otros programas.

## Licencia y créditos

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio.

Las conexiones QUIC usan [ngtcp2](https://github.com/ngtcp2/ngtcp2) (licencia MIT) y OpenSSL. Los formatos de IPv6 y de las conexiones entre ID bajas siguen las especificaciones publicadas por el proyecto eMule Qt. Datos de países: [IP Geolocation by DB-IP](https://db-ip.com) (CC BY 4.0). Iconos de banderas: Mark James, famfamfam.com (dominio público).

Usa Pegaso solo con contenidos que tengas derecho a descargar y compartir.
