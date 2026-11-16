# Pegaso 0.6.1

Pegaso es el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista. Esta versión es el resultado de una revisión de privacidad: cierra los caminos por los que tu dirección podía verse cuando creías tenerla oculta.

## Descarga

- **`Pegaso-0.6.1-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de versiones anteriores y conserva la configuración y las descargas. Incluye la extensión.
- **`Pegaso-0.6.1-extension-chromium.zip`** y **`Pegaso-0.6.1-extension-firefox.zip`** — la extensión sola (no cambia respecto a la 0.6).

## Qué cambia

- **El proxy vale para todo.** El proxy de Preferencias → Proxy solo lo usaba eD2K; BitTorrent y las descargas web salían directas. Ahora BitTorrent va entero por el proxy, y lo que no puede pasar por él (Kad, el UDP de eD2K, la web con un proxy SOCKS) no se envía.
- **IPv6 no se escapa de la VPN.** Si tu VPN solo lleva IPv4 y no has activado la protección VPN de Pegaso, Pegaso lo detecta y deja de usar IPv6.
- **Sin direcciones que delaten la tarjeta de red.** No se anuncian direcciones IPv6 hechas con su número.
- **Corte de emergencia más completo.** También detiene la actualización de la lista de servidores y del filtro de IP.
- **Documento de privacidad.** Qué ve cada cual, qué queda en tu equipo y los límites de cada protección: [docs/privacidad.md](https://github.com/Draev-developer/Pegaso/blob/main/docs/privacidad.md).

La lista completa de cambios está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

## A tener en cuenta

- Si usas proxy, Kad deja de funcionar: es todo UDP y estos proxies no lo llevan. Con una VPN y la protección VPN de Pegaso funciona todo.
- Con un proxy SOCKS no hay descargas web, búsquedas en webs ni renombrado; con un proxy HTTP sí, salvo que pida usuario y contraseña.
- La detección de IPv6 por fuera de la VPN se ha comprobado leyendo el código y en un equipo sin IPv6; no se ha podido probar con una VPN real de ese tipo.
- Compartir entre pares no es anónimo: sin VPN ni proxy, quien comparte contigo ve tu dirección IP.

## Licencia y créditos

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio.
