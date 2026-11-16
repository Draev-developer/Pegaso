# API local de Pegaso (REST y MCP)

Pegaso puede atender a otros programas del mismo equipo: scripts, y asistentes de IA mediante el protocolo MCP. Con la API se ven y se manejan las descargas, se busca en eD2K y Kad, y se conecta o desconecta el programa. · [English](en/api.md)

## Activarla

**Herramientas → API local (REST y MCP)…** La primera vez Pegaso crea una clave secreta, la muestra y la copia al portapapeles. Viene desactivada.

- Dirección REST: `http://127.0.0.1:4780/api/v1`
- Dirección MCP: `http://127.0.0.1:4780/mcp`
- El puerto se cambia con `ApiPort` en `preferences.ini`. La clave se guarda ahí como `ApiToken`, cifrada para tu cuenta de Windows; para verla de nuevo, abre la misma opción del menú.

## Seguridad

- Solo escucha en `127.0.0.1`: no es accesible desde la red.
- Cada petición lleva la clave: `Authorization: Bearer <clave>`. Sin ella, respuesta 401.
- Se rechazan (403) las peticiones cuyo `Host` no sea este equipo y las que envía una página web (cabecera `Origin` que no es local), para que un sitio web no pueda llegar a la API a través del navegador.
- Una extensión del navegador (`chrome-extension://…`, `moz-extension://…`) no es una página web y sí se admite, con la clave como cualquiera: así funciona la [extensión de Pegaso](extension-navegador.md).
- Quien tiene la clave puede añadir y quitar descargas. Trátala como una contraseña.

## REST

Todas las respuestas son JSON. Los errores llevan `{"error": "..."}` y el código HTTP correspondiente.

| Método y dirección | Qué hace |
|---|---|
| `GET /api/v1/status` | Versión, estado de eD2K y Kad, velocidades, número de descargas y de archivos compartidos |
| `GET /api/v1/connection` | `{"report": "…"}`: el texto de «Comprobar mi conexión» — por dónde sale cada protocolo en ese momento (proxy, adaptador de la VPN o ruta del sistema), en el idioma del programa |
| `GET /api/v1/downloads` | Lista de descargas |
| `POST /api/v1/downloads` | Añade descargas. Cuerpo: `{"links": ["ed2k://…", "magnet:…", "https://…"]}`. La dirección web de un archivo `.torrent` se añade como torrent; con `"kind": "torrent"` todas las direcciones web se toman por archivos `.torrent`, aunque no lo parezcan. Una dirección web con un rango, `…/parte[01-20].zip`, vale por toda la serie |
| `GET /api/v1/downloads/{hash}` | Una descarga |
| `POST /api/v1/downloads/{hash}/pause` | Pausa |
| `POST /api/v1/downloads/{hash}/resume` | Reanuda |
| `DELETE /api/v1/downloads/{hash}` | Quita la descarga. Lo recibido de una descarga sin terminar se borra |
| `GET /api/v1/shared` | Archivos compartidos, con su enlace ed2k (hasta 1000) |
| `POST /api/v1/searches` | Empieza una búsqueda. Cuerpo: `{"query": "…", "network": "server"｜"global"｜"kad", "type": "video"｜"audio"｜…}`. Devuelve `{"id": n}` |
| `GET /api/v1/searches/{id}` | Resultados recibidos hasta el momento (hasta 500) |
| `POST /api/v1/searches/{id}/download` | Descarga un resultado. Cuerpo: `{"hash": "…"}` |
| `POST /api/v1/connect`, `POST /api/v1/disconnect` | Conecta o desconecta eD2K y Kad |

Cada descarga y cada resultado de búsqueda lleva `trust`, la valoración del detector de falsos: `score` de 0 (nada en contra) a 100, `label` y `reasons`.

Ejemplo:

```
curl -H "Authorization: Bearer CLAVE" http://127.0.0.1:4780/api/v1/downloads
curl -H "Authorization: Bearer CLAVE" -H "Content-Type: application/json" \
     -d "{\"links\": [\"https://ejemplo.org/archivo.zip\"]}" http://127.0.0.1:4780/api/v1/downloads
```

## MCP

La dirección `/mcp` habla el Model Context Protocol por HTTP (JSON-RPC 2.0, versiones 2025-06-18, 2025-03-26 y 2024-11-05). Ofrece estas herramientas:

`pegaso_status`, `pegaso_list_downloads`, `pegaso_add_download`, `pegaso_pause_download`, `pegaso_resume_download`, `pegaso_remove_download`, `pegaso_list_shared`, `pegaso_search`, `pegaso_search_results`, `pegaso_download_result`, `pegaso_connect`, `pegaso_disconnect`.

Para añadirlo a un cliente MCP que admita servidores HTTP con cabeceras, dale la dirección y la cabecera con la clave. En muchos clientes se escribe así:

```
{ "mcpServers": { "pegaso": { "type": "http", "url": "http://127.0.0.1:4780/mcp", "headers": { "Authorization": "Bearer CLAVE" } } } }
```

El servidor responde a cada petición con un único mensaje JSON; no usa flujos SSE ni sesiones.

## Límites

- Una petición cada vez; las respuestas largas se recortan a 500 resultados o 1000 archivos.
- No hay todavía dirección para cambiar preferencias, elegir los archivos de un torrent ni valorar archivos.
- Una búsqueda en servidor sustituye a la anterior búsqueda en servidor, igual que en la ventana de Pegaso.
