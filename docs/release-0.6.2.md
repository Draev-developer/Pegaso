# Pegaso 0.6.2

Pegaso es el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista. Esta versión remata la revisión de privacidad: claves cifradas, la web atada a la VPN y a través de proxies SOCKS, y una ventana que dice por dónde sale cada cosa. *English below.*

## Descarga

- **`Pegaso-0.6.2-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de versiones anteriores y conserva la configuración y las descargas. Incluye la extensión.
- **`Pegaso-0.6.2-extension-chromium.zip`** y **`Pegaso-0.6.2-extension-firefox.zip`** — la extensión sola (no cambia respecto a la 0.6).

## Qué cambia

- **Claves cifradas.** El token de la API, las claves de renombrado y de tus webs de búsqueda y la contraseña del proxy ya no están en claro en `preferences.ini`: Windows los cifra para tu cuenta.
- **Comprobar mi conexión.** En Herramientas: por dónde sale cada protocolo ahora mismo (proxy, adaptador de la VPN o ruta normal) y si el corte de emergencia está armado.
- **La web, atada a la VPN.** Con la protección VPN, las descargas web, búsquedas y renombrado salen también por el adaptador de la VPN.
- **La web, por proxy SOCKS.** Con SOCKS5 o SOCKS4a las peticiones web pasan por el proxy, que también resuelve los nombres. Antes no se hacían.
- **Borrar el rastro local.** En Herramientas, o solo al cerrar: búsquedas, descargas terminadas y canceladas, archivos que ya no compartes y registro.
- **Aviso de versión nueva, opcional.** Apagado de serie; se activa en Preferencias → General.
- **Pausa total en el programador.** Una acción nueva detiene eD2K, Kad, BitTorrent y web en el horario que elijas.
- **Torrents sin su carpeta.** Opción en Preferencias → Web y BitTorrent.
- **Documentación en inglés.**
- **Corregido: opciones que se perdían al cerrar.** Una opción de Pegaso cambiada durante la sesión (por ejemplo, el adaptador de la protección VPN) podía volver a su valor anterior al cerrar. Si usas la protección VPN, comprueba tras actualizar que sigue puesta en Preferencias → Web y BitTorrent.

La lista completa está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md) y la explicación de cada protección en [docs/privacidad.md](https://github.com/Draev-developer/Pegaso/blob/main/docs/privacidad.md).

## A tener en cuenta

- Las claves cifradas solo las lee tu cuenta de Windows en ese equipo: si copias la configuración a otro, tendrás que volver a escribirlas (y la extensión del navegador necesitará la clave nueva de la API).
- Con la protección VPN, los nombres de las webs (DNS) los sigue resolviendo Windows; con túnel dividido pueden ir a tu operador.
- Con proxy, Kad sigue sin funcionar: es todo UDP.
- Sin probar con una VPN o un proxy reales de terceros: las pruebas se han hecho con proxies SOCKS5 y HTTP simulados y con los adaptadores de un equipo.
- Compartir entre pares no es anónimo: sin VPN ni proxy, quien comparte contigo ve tu dirección IP.

---

## English

Pegaso is a free P2P client for Windows which downloads from the eMule networks (eD2K and Kad), BitTorrent and the web in a single list. This release completes the privacy review.

- **Encrypted secrets.** The API token, the keys of the video databases and of your search sites and the proxy password are no longer stored in clear in `preferences.ini`: Windows encrypts them for your user account.
- **Check my connection** (Tools): which way each protocol leaves right now (proxy, VPN adapter or the normal route) and whether the kill switch is armed.
- **Web requests tied to the VPN.** With the VPN protection on, web downloads, searches and video renaming leave through the VPN adapter too.
- **Web requests through SOCKS proxies.** With SOCKS5 or SOCKS4a they go through the proxy, which also looks the names up.
- **Clear local traces** (Tools, or at every exit): searches, finished and cancelled downloads, files no longer shared, logs.
- **Optional check for new versions**, off by default (Preferences → General).
- **"Pause everything"** as a scheduler action; **torrents saved without their folder** as an option.
- **Fixed: options lost on exit.** A Pegaso option changed during a session (the adapter of the VPN protection, for instance) could go back to its former value when the program closed. If you use the VPN protection, check after updating that it is still set in Preferences → Web and BitTorrent.
- **Documentation in English**: [README.en.md](https://github.com/Draev-developer/Pegaso/blob/main/README.en.md), [privacy](https://github.com/Draev-developer/Pegaso/blob/main/docs/en/privacy.md).

Sharing between peers is not anonymous: without a VPN or a proxy, those you share with see your IP address.

## Licencia y créditos / License

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio. Pegaso derives from eMule 0.72a (community edition); the full source code is in this repository.
