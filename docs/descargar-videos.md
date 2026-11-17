# Descargar un vídeo de una web

Pegaso puede bajar un vídeo publicado en una web eligiendo su calidad, su idioma y el nombre del archivo. · [English](en/download-videos.md)

## Cómo se usa

1. Pega la dirección en el campo de enlaces de siempre, o abre **Herramientas → Descargar un vídeo de una web…** y escríbela ahí. Vale la dirección de la página o la de la lista del vídeo (`.m3u8`, `.mpd`).
2. Pegaso mira la dirección y muestra las **calidades** y los **idiomas** en que está publicado el vídeo. Viene elegida la mejor calidad y, si existe, el idioma de Pegaso.
3. Escribe el **nombre del archivo** (se propone el título de la página) y pulsa **Descargar**.

Puedes seguir usando Pegaso mientras baja. El archivo queda en la carpeta de descargas, o en la carpeta de vídeos si tienes una puesta en «Carpetas automáticas».

## Qué vídeos baja

Los que la web publica como una lista abierta: un archivo de texto (HLS, `.m3u8`, o DASH, `.mpd`) que cualquiera puede leer y que nombra los trozos del vídeo en cada calidad e idioma. Es el formato que usan muchas televisiones públicas, cursos, conferencias y servidores propios.

## Qué vídeos no baja

- **Los cifrados.** Pegaso lo dice y no pide ni la clave ni los trozos.
- **Las emisiones en directo**, que no tienen un final hasta el que descargar.
- **Los de webs que no publican la lista.** Muchas plataformas no ponen la dirección del vídeo en la página: su reproductor la pide a un servicio privado, con un inicio de sesión, credenciales de acceso o una protección contra copia. Ahí Pegaso no encuentra nada y lo dice. No hay en Pegaso nada hecho a medida para ninguna web, ni para saltarse esos controles.

Baja solo vídeos que tengas derecho a guardar.

## Vídeo y sonido: ffmpeg

Cuando un vídeo se ofrece en varios idiomas, la imagen y el sonido viajan en archivos distintos. Para unirlos en uno hace falta **ffmpeg**, un programa libre que Pegaso no incluye pero usa si está en tu equipo:

- junto a `Pegaso.exe`, o
- en el PATH de Windows, o
- donde diga `FfmpegPath` en la sección `[eMule]` de `preferences.ini`.

Con ffmpeg, Pegaso une imagen y sonido en un `.mp4` sin recodificar nada. Sin él, deja dos archivos con el mismo nombre —el vídeo y el sonido, con el idioma en el nombre— y te lo avisa antes de empezar. Muchos reproductores (VLC entre ellos) abren el vídeo con el sonido de un archivo aparte.

Si el vídeo lleva el sonido dentro (un solo idioma), no hace falta ffmpeg.

## Límites

- Los trozos se bajan uno tras otro: un vídeo de una hora puede tardar varios minutos aunque tu conexión sea rápida.
- No se reanuda: si cierras Pegaso a medias, hay que empezar de nuevo.
- No aparece en la lista de descargas; el avance se ve en la propia ventana, de la que solo hay una.
- No cuenta para el límite de velocidad de las preferencias.
- Los subtítulos no se descargan.
