# Privacidad en Pegaso

Qué sabe de ti quien comparte contigo, qué no sale nunca de tu equipo y qué puedes hacer para que tu dirección no se vea. Corresponde a la revisión de privacidad hecha para la versión 0.6.1.

## Lo esencial

- **Pegaso no tiene cuentas, ni telemetría, ni avisos automáticos.** No hay servidor del proyecto al que Pegaso llame: no se registra nada, no se envían estadísticas ni informes de errores, y no comprueba por su cuenta si hay versión nueva (el menú abre la página de versiones en tu navegador cuando tú lo pides).
- **Compartir entre pares no es anónimo.** En eD2K, Kad y BitTorrent, quien te envía o te pide un archivo ve tu dirección IP. Es así en todos los programas de este tipo. Lo que la oculta es una VPN (o un proxy), y Pegaso está hecho para que nada salga por fuera de ella.
- **Tu nombre no viaja.** El nombre de usuario por defecto es «Pegaso»; Pegaso no lee ni envía tu nombre de Windows, el del equipo ni tus carpetas.

## Qué ven los demás

| Quién | Qué ve |
|---|---|
| Otros usuarios de eD2K y Kad | Tu IP y puertos, el nombre de usuario que hayas puesto, que usas Pegaso y su versión, y un identificador de usuario (un número al azar creado en tu equipo, que sirve para el sistema de créditos). Los archivos que les sirves o les pides. |
| El servidor eD2K al que te conectas | Lo anterior y la lista de archivos que compartes. |
| Otros usuarios de BitTorrent, rastreadores y la red DHT | Tu IP y puerto, que usas Pegaso, y los torrents en los que participas. |
| Las webs de las que descargas o en las que buscas | Tu IP y que la petición la hace Pegaso. Sin cookies y sin página de procedencia. |
| Los servicios de renombrado (TVmaze, TheMovieDB, OMDb, TheTVDB) | Tu IP, el título que se busca y tu clave del servicio. Solo cuando usas «Renombrar vídeos». |

Otro usuario no puede pedirte la lista de todo lo que compartes: esa función viene desactivada.

## Ocultar tu dirección: VPN

**Preferencias → Pegaso → Protección VPN** ata Pegaso al adaptador de tu VPN:

- eD2K, Kad y BitTorrent solo usan ese adaptador.
- Con el **corte de emergencia** (activado por defecto), si la VPN se cae se detiene todo: transferencias, descargas web, búsquedas en webs, renombrado y actualización de listas. Se reanuda solo cuando vuelve.
- Mientras Pegaso está atado a un adaptador **no usa IPv6**, porque saldría por fuera de la VPN.

Si usas una VPN **sin** atar Pegaso a ella, Pegaso comprueba igualmente por dónde sale cada protocolo: si IPv4 sale por un adaptador e IPv6 por otro (lo que ocurre con las VPN que solo llevan IPv4), deja de usar IPv6 y lo avisa en el registro. Aun así, lo seguro es activar la protección: sin ella, si la VPN se cae, las transferencias siguen por tu conexión normal.

Límites que conviene conocer:

- Las descargas web siguen la ruta del sistema. Con una VPN de túnel completo salen por ella; con «túnel dividido» pueden no hacerlo.
- La VPN sustituye a tu operador como testigo: elige una en la que confíes.

## Ocultar tu dirección: proxy

Con un proxy en **Preferencias → Proxy**, Pegaso lo usa para todo lo que puede pasar por él y **no envía lo que no puede**:

| | SOCKS5 | SOCKS4 | HTTP |
|---|---|---|---|
| eD2K (conexiones TCP) | por el proxy | por el proxy | por el proxy |
| BitTorrent: pares y rastreadores | por el proxy | por el proxy | por el proxy |
| Descargas web, búsquedas en webs, renombrado | no se hacen | no se hacen | por el proxy |
| Kad y el UDP de eD2K | no se usan | no se usan | no se usan |
| DHT de BitTorrent | por el proxy si este admite UDP | no se usa | no se usa |
| IPv6, descubrimiento en la red local, apertura de puertos | no se usan | no se usan | no se usan |

- Los nombres de los rastreadores y de las webs los resuelve el proxy, no tu equipo.
- Con proxy, BitTorrent no dice qué programa es ni acepta conexiones entrantes.
- Kad funciona solo por UDP, que estos proxies no llevan: con proxy no arranca. Si prefieres el comportamiento de eMule (UDP por fuera del proxy, con tu dirección a la vista), pon `ProxyStrict=0` en la sección `[eMule]` de `preferences.ini`.
- Las peticiones web a un proxy HTTP con usuario y contraseña no están admitidas.
- Las direcciones del propio equipo (127.0.0.1) no pasan por el proxy: no salen a la red.

## IPv6

Pegaso usa IPv6 en eD2K, Kad y BitTorrent cuando tu conexión lo tiene. Una dirección IPv6 pública identifica tu conexión igual que la IPv4. Pegaso no anuncia direcciones IPv6 construidas a partir del número de serie de la tarjeta de red (las que Windows crea solo si se le desactiva su ajuste de privacidad). Para no usar IPv6 en eD2K y Kad: `IPv6Enabled=0` en `preferences.ini`.

## Lo que queda en tu equipo

Todo está en `%LOCALAPPDATA%\Pegaso`, dentro de tu perfil de Windows:

- `config\preferences.ini`: la configuración. Guarda **en claro** las claves de los servicios de renombrado, el token de la API local y, si lo usas, la contraseña del proxy. No compartas ese archivo.
- `config\known.met`, `cancelled.met`, `torrents\`: qué has descargado, cancelado y compartido. Se puede desactivar el recuerdo en Preferencias → Archivos.
- `config\AC_SearchStrings.dat`: las últimas búsquedas, para autocompletar. Se desactiva en Preferencias → Pantalla.
- Registro de actividad en disco: desactivado por defecto.

Al desinstalar, esa carpeta se conserva; bórrala a mano si quieres que no quede nada.

## API local y extensión del navegador

- La API viene **apagada**. Encendida, escucha solo en `127.0.0.1`, exige un token al azar y rechaza las peticiones que una página web intente hacer desde el navegador.
- La extensión solo habla con Pegaso en tu equipo. No tiene permiso para leer las páginas que visitas, ni cookies, ni historial; no envía nada a ningún otro sitio. Las descargas del navegador solo las mira si activas que Pegaso se quede con ellas.

## Otros detalles

- **Antivirus.** Las descargas terminadas se pasan al antivirus instalado en tu equipo. Lo que este haga con ellas (por ejemplo, enviar muestras a su nube) depende de su configuración, no de Pegaso.
- **Banderas de países.** Salen de un archivo incluido en la instalación; no se consulta nada en la red.
- **Primera conexión.** Si no hay lista de servidores ni contactos de Kad, Pegaso descarga una lista de contactos de `upd.emule-security.org`, que ve tu IP en ese momento.
- **Torrents que creas.** Llevan los nombres de los archivos, «Pegaso» como programa y la fecha. No llevan rutas de tu disco ni tu nombre.
- **Colecciones de eMule que creas.** Llevan tu nombre de usuario de Pegaso como autor.
- **Contacto.** Si escribes al proyecto por correo, tu dirección la ve el destinatario, como en cualquier correo.
- **IRC, interfaz web y avisos por correo** de eMule siguen estando, apagados. Son funciones antiguas: si las enciendes, la interfaz web escucha en la red y el IRC muestra tu dirección al servidor.

## Comprobar el instalador

El instalador no está firmado digitalmente, así que Windows puede avisar al abrirlo. Cada versión publica su huella SHA-256 en la página de versiones; para comprobarla:

```
certutil -hashfile Pegaso-0.6.1-setup.exe SHA256
```
