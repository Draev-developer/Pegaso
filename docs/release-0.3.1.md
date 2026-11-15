# Pegaso 0.3.1

Versión de mejora de Pegaso, el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista.

## Descarga

**`Pegaso-0.3.1-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de versiones anteriores y conserva la configuración y las descargas.

## Novedades

- **Transferencias entre dos ID bajas a través de Kad.** En la 0.3, dos clientes que no pueden recibir conexiones solo se encontraban si un servidor eD2K les presentaba. Ahora también se encuentran a través de Kad, por dos vías:
  - **El compañero de la fuente.** En Kad, cada cliente sin puertos abiertos tiene un compañero (*buddy*) que recibe peticiones por él. Pegaso le envía una petición que cualquier compañero reenvía, también el de un eMule sin modificar; la fuente abre su router hacia nosotros y la conexión se hace sobre QUIC.
  - **Directa.** A las fuentes que tienen el puerto UDP abierto y anuncian en Kad que aceptan llamadas QUIC se les pide directamente que esperen la llamada.
- Con ID baja, las fuentes de Kad que tampoco pueden recibir conexiones ya no se descartan.

La lista completa está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

## A tener en cuenta

- Las dos vías de Kad solo sirven con fuentes que hablen el mismo dialecto (Pegaso, eMule Qt, eMuleAI). Se han probado entre dos Pegaso con un nodo Kad y un compañero simulados; no todavía con otros programas ni a través de routers reales.
- Sigue faltando la presentación a través de servidores Lugdunum.
- Lo dicho en la 0.3 sobre IPv6, VPN y el filtro de IP sigue valiendo.

## Licencia y créditos

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio.

Las conexiones QUIC usan [ngtcp2](https://github.com/ngtcp2/ngtcp2) (licencia MIT) y OpenSSL. Los formatos de IPv6 y de las conexiones entre ID bajas siguen las especificaciones publicadas por el proyecto eMule Qt. Datos de países: [IP Geolocation by DB-IP](https://db-ip.com) (CC BY 4.0). Iconos de banderas: Mark James, famfamfam.com (dominio público).

Usa Pegaso solo con contenidos que tengas derecho a descargar y compartir.
