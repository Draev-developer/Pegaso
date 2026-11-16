# Renombrar vídeos

Herramientas → «Renombrar vídeos…» da a los vídeos terminados el nombre de la película o la serie que son y, si quieres, los lleva a otra carpeta. No dejan de compartirse en eD2K ni de sembrarse en BitTorrent. · [English](en/rename-videos.md)

- Películas: `Nombre (año).ext`
- Series: `Nombre - 1x05.ext` (temporada x capítulo)

## Cómo se usa

1. Elige una base de datos para las series y otra para las películas.
2. Pulsa «Buscar nombres». Se marcan solos los vídeos que se han encontrado.
3. Revisa la columna «Nombre nuevo». Un nombre se corrige a mano con F2 o con un clic lento sobre él.
4. Si quieres moverlos, escribe o elige una carpeta para las películas y otra para las series. Vacío: se quedan donde están.
5. Pulsa «Renombrar». Puedes seguir usando Pegaso mientras se mueven.

Qué es cada archivo se lee de su nombre: `S01E05`, `1x05`, `Cap.105`, «Temporada 1 Capítulo 5» o, en una película, el año. Con nombres que no dicen nada no hay nada que buscar.

## Qué bases de datos piden clave

| Base de datos | Sirve para | Clave |
|---|---|---|
| TVmaze | Series | No pide |
| AniDB | Anime | No pide (ver más abajo) |
| TheMovieDB | Series y películas | Gratuita |
| OMDb | Películas | Gratuita (1.000 consultas al día) |
| TheTVDB | Series | Puede pedir suscripción de pago |

Pegaso no trae ninguna clave: cada persona usa las suyas. Se escriben en el botón «Claves…», que también abre la web de cada servicio, y se guardan cifradas para tu cuenta de Windows. Para empezar sin registrarte en nada, usa TVmaze para las series; para las películas hace falta una de las dos claves gratuitas.

## Cómo conseguir cada clave

### TheMovieDB

1. Crea una cuenta gratuita en <https://www.themoviedb.org/signup> y confirma el correo.
2. Con la sesión abierta, entra en <https://www.themoviedb.org/settings/api> (Ajustes → API).
3. Solicita una clave de tipo «Developer» (uso personal). Pide aceptar las condiciones y rellenar un formulario corto: en el uso, basta con decir que es para poner nombre a tus propios archivos.
4. Copia la «Clave de la API» (32 letras y números) o el «Token de acceso de lectura a la API» (mucho más largo). Pegaso acepta cualquiera de las dos.
5. Pégala en «Claves…», en la casilla de TheMovieDB.

### OMDb

1. Entra en <https://www.omdbapi.com/apikey.aspx>.
2. Elige el plan «FREE» (1.000 consultas al día), pon tu correo y un nombre, y describe el uso en una línea.
3. Te llega un correo con la clave (8 letras y números) y un enlace de activación. **Hay que abrir ese enlace**: sin activar, la clave no funciona.
4. Pégala en «Claves…», en la casilla de OMDb.

### TheTVDB

1. Crea una cuenta en <https://thetvdb.com>.
2. Entra en <https://thetvdb.com/api-information> y pide una clave de la versión 4 de su API.
3. TheTVDB puede pedir una suscripción de pago para darla. Si no quieres pagar, TVmaze y TheMovieDB hacen el mismo trabajo para las series.
4. Pégala en «Claves…», en la casilla de TheTVDB.

### AniDB

AniDB no tiene búsqueda para programas: publica la lista de todos sus títulos, y Pegaso la baja una vez al día como mucho y la consulta en tu equipo. En algunas conexiones AniDB pone delante una comprobación para navegadores, que Pegaso no salta. Si la ventana te avisa de ello:

1. Abre <https://anidb.net/api/anime-titles.dat.gz> en tu navegador y guarda el archivo, sin descomprimirlo.
2. Déjalo en la carpeta de configuración de Pegaso (la ventana te dice cuál es; normalmente `%LOCALAPPDATA%\Pegaso\config`), con el nombre `anidb-titles.dat.gz`.
3. Vuelve a pulsar «Buscar nombres».

## Si una búsqueda falla

- **«falta una clave válida»**: la casilla está vacía, la clave está mal copiada o, en OMDb, no se ha abierto el enlace de activación.
- **«no encontrado»**: la base de datos no conoce ese título tal como está escrito en el nombre del archivo. Prueba con otra base de datos o corrige el nombre a mano.

## La carpeta de origen

Con la casilla «Al mover un vídeo, quitar la carpeta donde estaba…» marcada, al sacar un vídeo de una carpeta que está dentro de tus carpetas de descargas:

- si en la carpeta queda otro vídeo, no se toca;
- lo que un torrent de la lista sigue ofreciendo (un `.nfo`, unos subtítulos) se guarda aparte, con el estado del motor, para que el torrent siga completo;
- lo demás va a la papelera de reciclaje junto con la carpeta.

Las carpetas de descargas en sí no se quitan nunca.
