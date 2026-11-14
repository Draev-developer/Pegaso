# Pegaso 0.1

Primera versión pública de Pegaso: un cliente P2P libre para Windows que descarga de las redes eMule (eD2K y Kad), de BitTorrent y de la web desde una sola lista.

## Descarga

**`Pegaso-0.1-setup.exe`** — instalador para Windows 10 y 11 de 64 bits. No necesita permisos de administrador.

## Lo más destacado

- **Tres orígenes, una lista.** Enlaces `ed2k://`, enlaces magnet, archivos `.torrent` y direcciones web.
- **Enjambre híbrido.** Una misma descarga puede recibir datos de fuentes eMule, de un servidor web y de un torrent a la vez, todo verificado con los hashes de eMule.
- **Conectar y listo.** Trae servidores eD2K de serie y arranca Kad sin configuración.
- **Marcador de archivos.** Los usuarios pueden señalar un archivo como correcto, de mala calidad, falso o que no cumple su descripción; los señalados por varios aparecen como sospechosos.
- **Protección VPN** con corte automático del tráfico.
- **Análisis antivirus** de cada descarga terminada.
- **Siete idiomas:** español, inglés, francés, italiano, portugués, alemán y ruso.

La lista completa está en [CHANGELOG.md](https://github.com/Draev-developer/Pegaso/blob/main/CHANGELOG.md).

## Limitaciones conocidas

- Las fuentes web añadidas a una descarga de eMule solo admiten `http://`.
- El límite de ratio de siembra se cambia en `preferences.ini` (`TorrentSeedRatio`).
- Es una primera versión: conviene probarla antes de confiarle descargas importantes.

## Licencia

GNU GPL v2. Pegaso deriva de eMule 0.72a (edición de la comunidad); el código fuente completo está en este repositorio.

Usa Pegaso solo con contenidos que tengas derecho a descargar y compartir.
