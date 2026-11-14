# Compilar Pegaso

Pegaso se compila para Windows de 64 bits con Visual Studio 2026 (conjunto de herramientas v145). El programa enlaza todas sus bibliotecas de forma estática, así que el resultado es un único `Pegaso.exe` más las DLL de idioma.

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

BitTorrent usa **libtorrent 2.1** con Boost y OpenSSL, que se obtienen con [vcpkg](https://github.com/microsoft/vcpkg). El proyecto lo busca dos carpetas por encima de la raíz del repositorio (`..\..\vcpkg`); se puede indicar otra ruta con la propiedad `PegasoVcpkg` de MSBuild.

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
python -I tools\webtest\nettest.py           # Conectar, contra las redes reales
```

Casi todas usan solo servidores simulados en el propio equipo. `nettest.py` y las que necesitan Kad se conectan a las redes reales.

## Notas sobre el código

- Los fuentes heredados de eMule usan finales de línea CRLF y codificación Latin-1; conviene respetarlos para que los diffs sean legibles.
- `TorrentEngine.cpp` se compila aparte (C++17, sin cabecera precompilada) y es el único fichero que incluye libtorrent y Boost.
- Las etiquetas del árbol de Preferencias se asignan por posición (`uids` en `PreferencesDlg.cpp`): al añadir o quitar páginas hay que actualizar esa lista.
