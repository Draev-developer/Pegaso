# Pegaso 0.3

Tercera versión de Pegaso, el cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista.

## Descarga

**`Pegaso-0.3-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador. Se instala encima de versiones anteriores y conserva la configuración y las descargas.

## Novedades

- **IPv6 en eD2K y Kad.** Quien está detrás de un NAT del operador (CGNAT) o de un router que no puede abrir tiene ID baja y solo se le alcanza con rodeos; con una dirección IPv6 pública se le puede llamar directamente. Pegaso escucha en IPv6 además de IPv4, anuncia su dirección cuando sabe que es alcanzable y llama por IPv6 a las fuentes con ID baja que la anuncian. Habla la extensión IPv6 que usan eMule Qt y otros clientes.
- **Transferencias entre dos ID bajas.** Hasta ahora, dos clientes que no pueden recibir conexiones no podían intercambiar nada. Ahora el servidor al que ambos están conectados les presenta, los dos abren un camino a través de sus routers y la conexión se establece sobre QUIC. Usa el mismo dialecto que eMuleAI, aMule y eMule Qt. En esta versión funciona con los servidores eD2K que saben hacer esa presentación (protocolo de ed2k-server 0.9.84 o posterior); la presentación a través de Kad llegará en una versión posterior.
- **API local (REST y MCP).** Otros programas del equipo —scripts, asistentes de IA— pueden ver y manejar Pegaso: descargas, búsquedas y conexión. Viene desactivada, solo escucha en `127.0.0.1` y pide una clave secreta. Se activa en Herramientas → «API local (REST y MCP)…». Descrita en [docs/api.md](https://github.com/Draev-developer/Pegaso/blob/main/docs/api.md).

La lista completa está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

## Licencia y créditos

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio.

Las conexiones QUIC usan [ngtcp2](https://github.com/ngtcp2/ngtcp2) (licencia MIT) y OpenSSL. Los formatos de IPv6 y de las conexiones entre ID bajas siguen las especificaciones publicadas por el proyecto eMule Qt. Datos de países: [IP Geolocation by DB-IP](https://db-ip.com) (CC BY 4.0). Iconos de banderas: Mark James, famfamfam.com (dominio público).

Usa Pegaso solo con contenidos que tengas derecho a descargar y compartir.
