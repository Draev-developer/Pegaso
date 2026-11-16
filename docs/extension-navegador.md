# Extensión de Pegaso para el navegador

Añade «Descargar con Pegaso» a Chrome, Edge y Firefox. Lo que envías aparece al momento en la lista de descargas de Pegaso. · [English](en/browser-extension.md)

## Qué hace

- **Clic derecho sobre un enlace** → «Descargar con Pegaso». Sirve para enlaces ed2k, magnet, archivos .torrent y archivos de la web.
- **Clic derecho sobre un texto seleccionado** → envía de una vez todos los enlaces que contenga.
- **Clic derecho sobre un vídeo o un audio** → envía su archivo.
- **El botón de la extensión** muestra las descargas de Pegaso con su avance y admite un enlace pegado.
- **Opcional:** que Pegaso se quede con las descargas que empieza el navegador. Viene apagado.

La extensión solo habla con Pegaso en tu propio equipo. No envía nada a nadie más ni guarda registro de las páginas que visitas.

## 1. Activar la API de Pegaso

La extensión habla con Pegaso por su API local, que viene desactivada.

1. En Pegaso: **Herramientas → API local (REST y MCP)…**
2. Pegaso la activa, muestra una clave y la copia al portapapeles. Guárdala: hay que pegarla en la extensión.

Hace falta Pegaso 0.6 o posterior, y que esté abierto cuando uses la extensión.

## 2. Instalar la extensión

La extensión viene con Pegaso, en la carpeta `browser-extension` de donde lo instalaste (normalmente `C:\Program Files\Pegaso\browser-extension`). También está en cada versión publicada, en <https://github.com/Draev-developer/Pegaso/releases>.

### Chrome y Edge

1. Abre `chrome://extensions` (en Edge, `edge://extensions`).
2. Activa el **Modo de desarrollador**.
3. Pulsa **Cargar descomprimida** y elige la carpeta `browser-extension\chrome-edge`.

Queda instalada de forma permanente. Chrome puede recordarte al arrancar que tienes una extensión en modo de desarrollador; es normal mientras no esté en su tienda.

### Firefox

Firefox solo instala de forma permanente extensiones firmadas por Mozilla. Mientras la de Pegaso no lo esté, se puede cargar de forma temporal:

1. Abre `about:debugging#/runtime/this-firefox`.
2. Pulsa **Cargar complemento temporal…** y elige `pegaso-firefox.zip`.

Se quita al cerrar Firefox y hay que cargarla de nuevo.

## 3. Conectarla

1. Abre las opciones de la extensión (se abren solas en Chrome y Edge al pulsar su botón y «Opciones»).
2. Pega la clave de Pegaso y pulsa **Guardar y probar**. Debe decir «Conectado a Pegaso» con su versión.
3. El puerto es `4780` salvo que lo hayas cambiado.

## Si algo falla

| Mensaje | Qué pasa |
|---|---|
| «Pegaso no responde» | Pegaso está cerrado, o su API local no está activada. |
| «Pegaso no acepta la clave» | La clave está mal copiada. Cópiala de nuevo en Herramientas → API local. |
| «Este Pegaso aún no acepta extensiones» | Tu Pegaso es anterior a la 0.6: actualízalo. |
| «Ahí no hay ningún enlace» | En lo seleccionado no hay enlaces ed2k, magnet ni web. |

## Las descargas del navegador

Con la casilla «Que Pegaso se quede con las descargas del navegador» marcada, cada descarga que empieza el navegador se le pasa a Pegaso y la del navegador se cancela.

- Las descargas que exigen tener la sesión iniciada en la web pueden fallar en Pegaso, que no tiene tus sesiones del navegador.
- Si Pegaso no acepta una descarga (por estar cerrado, por ejemplo), el navegador sigue con ella como si nada.

## Para desarrolladores

El código está en `browser-extension/src` y es el mismo para todos los navegadores; solo cambia el manifiesto. `python browser-extension/build.py` genera las dos variantes en `browser-extension/dist`. La prueba es `tools/webtest/exttest.py`.
