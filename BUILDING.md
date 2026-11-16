# Compilar Pegaso

Pegaso se compila para Windows de 64 bits con Visual Studio 2026 (conjunto de herramientas v145). El programa enlaza todas sus bibliotecas de forma estática, así que el resultado es un único `Pegaso.exe` más las DLL de idioma. · [English](BUILDING.en.md)

## Requisitos

- **Visual Studio 2026** (la edición Community sirve) con la carga de trabajo *Desarrollo para el escritorio con C++* y el componente **MFC para x64**.
- **CMake** y **Git** en el `PATH`.
- **PowerShell 7** (`pwsh`).
- Opcional: **Python 3** con Pillow para regenerar iconos y textos, e **Inno Setup 6** para el instalador.

## 1. Bibliotecas

El código de la aplicación está en `srchybrid`. Las bibliotecas van en carpetas hermanas, en la raíz del repositorio (no están incluidas en él):

| Carpeta | Biblioteca | Versión usada |
|---|---|---|
| `cryptopp` | Crypto++ | 8.9 |
| `id3lib` | id3lib | 3.9.1 |
| `mbedtls` | Mbed TLS | 4.2.0 |
| `miniupnpc` | miniupnpc | 2.3.3 |
| `ResizableLib` | ResizableLib | la de eMule |
| `zlib` | zlib | 1.3.2 |

Descarga el código de cada una en su carpeta y compílalas todas con:

```powershell
pwsh tools\build-deps.ps1
```

BitTorrent usa **libtorrent 2.1** con Boost y OpenSSL, y las conexiones entre ID bajas usan **ngtcp2** (QUIC); todo ello se obtiene con [vcpkg](https://github.com/microsoft/vcpkg). El proyecto lo busca dos carpetas por encima de la raíz del repositorio (`..\..\vcpkg`); se puede indicar otra ruta con la propiedad `PegasoVcpkg` de MSBuild.

```powershell
git clone https://github.com/microsoft/vcpkg ..\..\vcpkg
..\..\vcpkg\bootstrap-vcpkg.bat
pwsh tools\build-deps.ps1 -Only libtorrent
```

## 2. La aplicación

Compila el proyecto directamente, no la solución:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    srchybrid\emule.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```

El resultado queda en `srchybrid\x64\Release`: `Pegaso.exe` y la carpeta `lang` con los idiomas.

Para probar sin tocar tu perfil de usuario, copia ambos a una carpeta que contenga `config\preferences.ini` (puede estar vacío): Pegaso funciona entonces en modo portable y guarda todo dentro de esa carpeta.

## 3. El instalador

```powershell
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" tools\installer\pegaso.iss
```

Genera `tools\installer\output\Pegaso-<versión>-setup.exe`.

## Herramientas del repositorio

| Ruta | Para qué |
|---|---|
| `tools/build-deps.ps1` | Compila las bibliotecas. |
| `tools/strings/make_strings.py` | Genera `PegasoStrings.h/.cpp` (textos propios de Pegaso en siete idiomas). |
| `tools/strings/rebrand_rc.py` | Pone el nombre Pegaso en los textos heredados de eMule. |
| `tools/icons/make_icons.py` | Dibuja el emblema y regenera los iconos. |
| `tools/installer/` | Guion de Inno Setup. |
| `tools/webtest/` | Pruebas automáticas que manejan la ventana real de Pegaso (ver abajo). |

## Pruebas

Las pruebas de `tools/webtest` arrancan Pegaso en una carpeta de pruebas, lo manejan como lo haría un usuario y comprueban el resultado. Están escritas para Windows y Python 3, y las rutas de la carpeta de pruebas se ajustan al principio de `webtest.py` y `btuitest.py`.

```powershell
python -I tools\webtest\webtest.py basic     # descargas web
python -I tools\webtest\hybridtest.py        # enjambre híbrido, tres instancias
python -I tools\webtest\ratetest.py          # marcador de archivos, tres instancias
python -I tools\webtest\btseedtest.py        # torrents entre dos instancias
python -I tools\webtest\webseedtest.py       # semillas web de torrents
python -I tools\webtest\sidebartest.py       # barra lateral, campo de búsqueda y filtros (pantalla al 100 %)
python -I tools\webtest\sitesearchtest.py    # búsqueda en las webs del usuario (web simulada)
python -I browser-extension\build.py           # empaqueta la extensión del navegador (antes de crear el instalador)
python -I tools\webtest\exttest.py           # la extensión cargada en Edge y lo que la API hace por ella (Node 22 o posterior)
python -I tools\webtest\createtest.py        # crear torrents de archivos propios y descargas web en serie
python -I tools\webtest\renametest.py        # renombrar vídeos con las bases de datos (simuladas) y moverlos sin dejar de compartir
python -I tools\webtest\proxytest.py         # el proxy de las preferencias lo lleva todo (proxies SOCKS5 y HTTP simulados)
python -I tools\webtest\privtest.py          # claves cifradas, aviso de versión, rastro, web atada al adaptador, pausa del programador
python    tools\webtest\privuitest.py        # las ventanas de las opciones de privacidad, con capturas (necesita Pillow)
python -I tools\webtest\autotest.py          # carpeta vigilada, carpetas por tipo, reproducir, importar, nombres por el adaptador
python -I tools\webtest\manytest.py 1500     # descargar de una vez muchos resultados de una búsqueda
python -I tools\webtest\droptest.py          # archivos arrastrados desde el Explorador (mueve el ratón unos segundos)
python -I tools\webtest\kvpntest.py          # oferta de Kaspersky VPN (solo si está instalado; no lo toca)
python -I tools\webtest\nattest.py           # dos ID bajas presentadas por un servidor simulado
python -I tools\webtest\kadnattest.py direct # dos ID bajas a través de Kad, vía directa por UDP
python -I tools\webtest\kadnattest.py buddy  # ídem, a través del compañero de la fuente (unos 9 minutos)
python -I tools\webtest\nettest.py           # Conectar, contra las redes reales
```

Casi todas usan solo servidores simulados en el propio equipo. `nettest.py` y las que necesitan Kad se conectan a las redes reales.

Con una VPN conectada, Windows puede rechazar las conexiones a una dirección de sus propios adaptadores: las pruebas que llaman al servidor web local por la dirección de la tarjeta de red pueden fallar solo por eso.

## Notas sobre el código

- Los fuentes heredados de eMule usan finales de línea CRLF y codificación Latin-1; conviene respetarlos para que los diffs sean legibles.
- `TorrentEngine.cpp` se compila aparte (C++17, sin cabecera precompilada) y es el único fichero que incluye libtorrent y Boost.
- Las etiquetas del árbol de Preferencias se asignan por posición (`uids` en `PreferencesDlg.cpp`): al añadir o quitar páginas hay que actualizar esa lista.
- `preferences.ini` tiene dos escritores. eMule lee el archivo entero en memoria al arrancar (`CPreferences::prefsIni`) y lo reescribe entero al guardar; las opciones de Pegaso se escriben directamente en el archivo con `theApp.WriteProfile…`. `SavePreferences` relee el archivo antes de escribir (`CIni::Reload`), para no devolver esas opciones al valor que tenían al arrancar.
