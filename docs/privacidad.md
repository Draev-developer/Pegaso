# Privacidad en Pegaso

Qué sabe de ti quien comparte contigo, qué no sale nunca de tu equipo y qué puedes hacer para que tu dirección no se vea. Corresponde a la versión 0.7. · [English](en/privacy.md)

## Lo esencial

- **Pegaso no tiene cuentas ni telemetría.** No hay servidor del proyecto al que Pegaso llame: no se registra nada y no se envían estadísticas ni informes de errores. Tampoco comprueba si hay versión nueva, salvo que tú lo actives.
- **Compartir entre pares no es anónimo.** En eD2K, Kad y BitTorrent, quien te envía o te pide un archivo ve tu dirección IP. Es así en todos los programas de este tipo. Lo que la oculta es una VPN (o un proxy), y Pegaso está hecho para que nada salga por fuera de ella.
- **Tu nombre no viaja.** El nombre de usuario por defecto es «Pegaso»; Pegaso no lee ni envía tu nombre de Windows, el del equipo ni tus carpetas.

## Comprobar mi conexión

**Herramientas → Comprobar mi conexión…** (también en Preferencias → Web y BitTorrent) dice en una línea por protocolo por dónde sale cada cosa en ese momento: por el proxy, solo por el adaptador de la VPN, o por la ruta normal del sistema, con el nombre del adaptador y su dirección. También dice si el corte de emergencia está armado y si todo está detenido. Se actualiza sola: si la VPN se cae con la ventana abierta, lo ves.

El resumen está siempre a la vista en la barra lateral, en una línea con un punto de color: **verde** si todo sale por la VPN o el proxy, **ámbar** si la dirección de tu conexión está a la vista, **rojo** si está todo detenido. Un clic en esa línea abre la ventana.

Lo que muestra es cómo está configurado Pegaso y por qué adaptador sale; no consulta ninguna web para averiguar tu dirección pública.

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

**Preferencias → Web y BitTorrent → Protección VPN** ata Pegaso al adaptador de tu VPN:

- eD2K, Kad y BitTorrent solo usan ese adaptador.
- Las descargas web, las búsquedas en webs, el renombrado y la actualización de listas también: esas peticiones las hace Windows, que no se puede atar a un adaptador, así que Pegaso las hace pasar por un puente propio dentro del equipo que conecta desde la dirección de ese adaptador.
- Con el **corte de emergencia** (activado por defecto), si la VPN se cae se detiene todo. Se reanuda solo cuando vuelve.
- Mientras Pegaso está atado a un adaptador **no usa IPv6**, porque saldría por fuera de la VPN.

Si usas una VPN **sin** atar Pegaso a ella, Pegaso comprueba igualmente por dónde sale cada protocolo: si IPv4 sale por un adaptador e IPv6 por otro (lo que ocurre con las VPN que solo llevan IPv4), deja de usar IPv6 y lo avisa en el registro. Aun así, lo seguro es activar la protección: sin ella, si la VPN se cae, las transferencias siguen por tu conexión normal.

Límites que conviene conocer:

- Los nombres de las webs (DNS) los pregunta Pegaso a los servidores DNS del adaptador de la VPN y a ningún otro. Si ese adaptador no tiene servidores DNS propios, los resuelve Windows con su configuración, y «Comprobar mi conexión» lo dice.
- Eso vale para las peticiones web. BitTorrent resuelve los nombres de sus rastreadores con la configuración de Windows: con una VPN de «túnel dividido», esas consultas pueden ir a tu operador, que vería a qué rastreador vas, aunque no lo que descargas.
- La VPN sustituye a tu operador como testigo: elige una en la que confíes.

## Ocultar tu dirección: proxy

Con un proxy en **Preferencias → Proxy**, Pegaso lo usa para todo lo que puede pasar por él y **no envía lo que no puede**:

| | SOCKS5 | SOCKS4a | SOCKS4 | HTTP |
|---|---|---|---|---|
| eD2K (conexiones TCP) | por el proxy | por el proxy | por el proxy | por el proxy |
| BitTorrent: pares y rastreadores | por el proxy | por el proxy | por el proxy | por el proxy |
| Descargas web, búsquedas en webs, renombrado, listas | por el proxy | por el proxy | no se hacen | por el proxy |
| Kad y el UDP de eD2K | no se usan | no se usan | no se usan | no se usan |
| DHT de BitTorrent | por el proxy si este admite UDP | no se usa | no se usa | no se usa |
| IPv6, descubrimiento en la red local, apertura de puertos | no se usan | no se usan | no se usan | no se usan |

- Los nombres de las webs los resuelve el proxy, no tu equipo. SOCKS4 a secas no sabe resolver nombres: por eso con él no hay peticiones web.
- En BitTorrent, los nombres de los rastreadores los resuelve el proxy con SOCKS5 y HTTP. Con SOCKS4 y SOCKS4a los resuelve tu equipo (tu DNS ve a qué rastreador vas). Usa SOCKS5 si puedes elegir.
- El usuario y la contraseña del proxy valen también para las peticiones web.
- Con proxy, BitTorrent no dice qué programa es ni acepta conexiones entrantes.
- Kad funciona solo por UDP, que estos proxies no llevan: con proxy no arranca. Si prefieres el comportamiento de eMule (UDP por fuera del proxy, con tu dirección a la vista), pon `ProxyStrict=0` en la sección `[eMule]` de `preferences.ini`; «Comprobar mi conexión» te lo recordará.
- Las direcciones del propio equipo (127.0.0.1) no pasan por el proxy: no salen a la red.

## IPv6

Pegaso usa IPv6 en eD2K, Kad y BitTorrent cuando tu conexión lo tiene. Una dirección IPv6 pública identifica tu conexión igual que la IPv4. Pegaso no anuncia direcciones IPv6 construidas a partir del número de serie de la tarjeta de red (las que Windows crea solo si se le desactiva su ajuste de privacidad). Para no usar IPv6 en eD2K y Kad: `IPv6Enabled=0` en `preferences.ini`.

## Lo que queda en tu equipo

Todo está en `%LOCALAPPDATA%\Pegaso`, dentro de tu perfil de Windows:

- `config\preferences.ini`: la configuración. Las claves de los servicios de renombrado y de tus webs de búsqueda, el token de la API local y la contraseña del proxy se guardan **cifrados por Windows para tu cuenta de usuario** (aparecen como `dpapi:…`). Otro usuario del equipo, o quien copie el archivo a otro ordenador, no puede leerlos; un programa que se ejecute con tu sesión sí. Si llevas la configuración a otro equipo tendrás que volver a escribirlos.
- `config\known.met`, `cancelled.met`, `torrents\`: qué has descargado, cancelado y compartido.
- `config\AC_SearchStrings.dat`: las últimas búsquedas, para autocompletar.
- Registro de actividad en disco: desactivado por defecto.

### Borrar el rastro

**Herramientas → Borrar el rastro local…** borra de una vez el historial de búsquedas, las descargas terminadas de la lista, el recuerdo de las descargas canceladas y de los archivos que ya no compartes, y el registro de actividad. No toca las descargas en curso ni los archivos compartidos.

Para que se haga solo cada vez que cierras Pegaso: **Preferencias → Web y BitTorrent → Borrar el rastro local al cerrar Pegaso**.

Los archivos descargados siguen en su carpeta, claro, y Windows puede conservar su propio rastro de ellos (archivos recientes, por ejemplo).

Al desinstalar, la carpeta de configuración se conserva; bórrala a mano si quieres que no quede nada.

## API local y extensión del navegador

- La API viene **apagada**. Encendida, escucha solo en `127.0.0.1`, exige un token al azar y rechaza las peticiones que una página web intente hacer desde el navegador.
- La extensión solo habla con Pegaso en tu equipo. No tiene permiso para leer las páginas que visitas, ni cookies, ni historial; no envía nada a ningún otro sitio. Las descargas del navegador solo las mira si activas que Pegaso se quede con ellas.

## Otros detalles

- **Aviso de versión nueva.** Viene apagado. Si lo activas en Preferencias → General, Pegaso pregunta a GitHub (`api.github.com`) cuál es la última versión publicada, como mucho una vez cada tantos días como indiques, y te lo dice en el registro. GitHub ve entonces la dirección por la que sales (la de tu VPN o tu proxy si los usas). No se descarga ni se instala nada.
- **Antivirus.** Las descargas terminadas se pasan al antivirus instalado en tu equipo. Lo que este haga con ellas (por ejemplo, enviar muestras a su nube) depende de su configuración, no de Pegaso.
- **Banderas de países.** Salen de un archivo incluido en la instalación; no se consulta nada en la red.
- **Primera conexión.** Si no hay lista de servidores ni contactos de Kad, Pegaso descarga una lista de contactos de `upd.emule-security.org`, que ve tu IP en ese momento.
- **Torrents que creas.** Llevan los nombres de los archivos, «Pegaso» como programa y la fecha. No llevan rutas de tu disco ni tu nombre.
- **Colecciones de eMule que creas.** Llevan tu nombre de usuario de Pegaso como autor.
- **Contacto.** Si escribes al proyecto por correo, tu dirección la ve el destinatario, como en cualquier correo.
- **IRC, interfaz web y avisos por correo** de eMule siguen estando, apagados. Son funciones antiguas: si las enciendes, la interfaz web escucha en la red y el IRC muestra tu dirección al servidor.
- **El puente de las peticiones web** escucha solo en `127.0.0.1`, en un puerto que elige Windows, y solo existe mientras Pegaso está atado a un adaptador o usa un proxy.

## Comprobar el instalador

El instalador no está firmado digitalmente, así que Windows puede avisar al abrirlo. Cada versión publica la huella SHA-256 de su instalador en la página de versiones; para comprobarla:

```
certutil -hashfile Pegaso-0.7-setup.exe SHA256
```
