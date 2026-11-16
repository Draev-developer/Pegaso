"""Builds the browser extension of Pegaso.

    python browser-extension/build.py [<output folder>]

Writes, in the output folder (default: browser-extension/dist):
  pegaso-chromium/   to load unpacked in Chrome or Edge, and pegaso-chromium.zip for their stores
  pegaso-firefox/    and pegaso-firefox.zip for Firefox
The code is the same; only the manifest differs. The version is the one of Pegaso
(tools/installer/pegaso.iss). The icons are in src/icons (made from the icon of Pegaso).
"""
import json
import os
import re
import shutil
import stat
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(HERE, 'src')

MESSAGES = {
    'name': ('Pegaso', 'Pegaso'),
    'description': ('Send links and downloads to Pegaso, the P2P client, on this computer.',
                    'Envía enlaces y descargas a Pegaso, el cliente P2P, en este equipo.'),
    'checking': ('Looking for Pegaso...', 'Buscando Pegaso...'),
    'options': ('Options', 'Opciones'),
    'pasteHint': ('Paste an ed2k, magnet or web link', 'Pega un enlace ed2k, magnet o web'),
    'download': ('Download', 'Descargar'),
    'connectedTo': ('Connected to Pegaso $1', 'Conectado a Pegaso $1'),
    'notConnected': ('Not connected to Pegaso', 'Sin conexión con Pegaso'),
    'downloads': ('$1 downloads in Pegaso', '$1 descargas en Pegaso'),
    'noDownloads': ('No downloads in Pegaso', 'No hay descargas en Pegaso'),
    'noLinks': ('There is no ed2k, magnet or web link in that.', 'Ahí no hay ningún enlace ed2k, magnet o web.'),
    'sent': ('Sent to Pegaso: $1', 'Enviado a Pegaso: $1'),
    'sentSome': ('Sent to Pegaso: $1. Not taken: $2', 'Enviado a Pegaso: $1. No aceptados: $2'),
    'notSent': ('Pegaso did not take it', 'Pegaso no lo ha recibido'),
    'menuLink': ('Download with Pegaso', 'Descargar con Pegaso'),
    'menuSelection': ('Download the links of the selection with Pegaso', 'Descargar con Pegaso los enlaces de la selección'),
    'menuMedia': ('Download this file with Pegaso', 'Descargar este archivo con Pegaso'),
    'errNeedKey': ('The key of Pegaso is missing. Open the options of the extension and paste it.',
                   'Falta la clave de Pegaso. Abre las opciones de la extensión y pégala.'),
    'errNotRunning': ('Pegaso does not answer. Is it open, with its local API turned on (Tools > Local API)?',
                      'Pegaso no responde. ¿Está abierto y con la API local activada (Herramientas > API local)?'),
    'errBadKey': ('Pegaso does not accept the key. Copy it again from Pegaso (Tools > Local API).',
                  'Pegaso no acepta la clave. Cópiala de nuevo desde Pegaso (Herramientas > API local).'),
    'errRefused': ('This Pegaso does not accept browser extensions yet: update it to version 0.6 or later.',
                   'Este Pegaso aún no acepta extensiones de navegador: actualízalo a la versión 0.6 o posterior.'),
    'errFailed': ('Pegaso could not do it.', 'Pegaso no ha podido hacerlo.'),
    'stepsTitle': ('Connect the extension to Pegaso', 'Conectar la extensión con Pegaso'),
    'step1': ('Open Pegaso and choose Tools > Local API (REST and MCP)...', 'Abre Pegaso y elige Herramientas > API local (REST y MCP)...'),
    'step2': ('Pegaso turns the API on, shows its key and copies it to the clipboard.', 'Pegaso activa la API, muestra su clave y la copia al portapapeles.'),
    'step3': ('Paste the key here and press "Save and test".', 'Pega la clave aquí y pulsa «Guardar y probar».'),
    'key': ('Key of Pegaso', 'Clave de Pegaso'),
    'port': ('Port (4780 unless you changed it)', 'Puerto (4780 si no lo has cambiado)'),
    'capture': ('Let Pegaso take the downloads of the browser', 'Que Pegaso se quede con las descargas del navegador'),
    'captureHint': ('When the browser starts a download, it is handed to Pegaso and the one of the browser is cancelled. Downloads which need you to be signed in on the site may fail in Pegaso; if Pegaso does not take one, the browser goes on with it.',
                    'Cuando el navegador empieza una descarga, se le pasa a Pegaso y la del navegador se cancela. Las descargas que exigen tener la sesión iniciada en la web pueden fallar en Pegaso; si Pegaso no acepta una, el navegador sigue con ella.'),
    'saveAndTest': ('Save and test', 'Guardar y probar'),
    'testOk': ('Connected to Pegaso $1. Everything is ready.', 'Conectado a Pegaso $1. Todo listo.'),
    'useTitle': ('How to use it', 'Cómo se usa'),
    'use1': ('Right-click a link and choose "Download with Pegaso".', 'Clic derecho sobre un enlace y «Descargar con Pegaso».'),
    'use2': ('Select a text with several links, right-click it and send them all at once.', 'Selecciona un texto con varios enlaces, clic derecho, y envíalos todos de una vez.'),
    'use3': ('The button of the extension shows the downloads of Pegaso and takes a pasted link.', 'El botón de la extensión muestra las descargas de Pegaso y admite un enlace pegado.'),
    'privacy': ('The extension only talks to Pegaso on this computer (127.0.0.1). It sends nothing to anyone else and keeps no record of the pages you visit.',
                'La extensión solo habla con Pegaso en este equipo (127.0.0.1). No envía nada a nadie más ni guarda registro de las páginas que visitas.'),
}


def version():
    text = open(os.path.join(ROOT, 'tools', 'installer', 'pegaso.iss'), encoding='utf-8-sig').read()
    return re.search(r'#define AppVersion "([^"]+)"', text).group(1)


def manifest(firefox):
    m = {
        'manifest_version': 3,
        'name': '__MSG_name__',
        'description': '__MSG_description__',
        'version': version(),
        'default_locale': 'en',
        'icons': {size: 'icons/icon-%s.png' % size for size in ('16', '32', '48', '128')},
        'action': {'default_popup': 'popup.html', 'default_title': 'Pegaso',
                   'default_icon': {size: 'icons/icon-%s.png' % size for size in ('16', '32')}},
        'options_ui': {'page': 'options.html', 'open_in_tab': True},
        'permissions': ['contextMenus', 'storage', 'notifications', 'downloads'],
        # only this computer: any port, since the user may have changed the one of the API
        'host_permissions': ['http://127.0.0.1/*'],
    }
    if firefox:
        m['background'] = {'scripts': ['background.js'], 'type': 'module'}
        m['browser_specific_settings'] = {'gecko': {'id': 'pegaso@draev-developer', 'strict_min_version': '128.0'}}
    else:
        m['background'] = {'service_worker': 'background.js', 'type': 'module'}
        m['minimum_chrome_version'] = '116'
    return m


def build(out, name, firefox):
    target = os.path.join(out, name)
    # an earlier build goes first; its folders may be marked read-only, which Windows minds
    for folder, dirs, files in os.walk(target, topdown=False):
        for f in files:
            os.chmod(os.path.join(folder, f), stat.S_IWRITE)
            os.remove(os.path.join(folder, f))
        for d in dirs:
            os.chmod(os.path.join(folder, d), stat.S_IWRITE)
            os.rmdir(os.path.join(folder, d))
    shutil.copytree(SRC, target, dirs_exist_ok=True)
    json.dump(manifest(firefox), open(os.path.join(target, 'manifest.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    for index, language in enumerate(('en', 'es')):
        folder = os.path.join(target, '_locales', language)
        os.makedirs(folder, exist_ok=True)
        messages = {key: {'message': texts[index]} for key, texts in MESSAGES.items()}
        json.dump(messages, open(os.path.join(folder, 'messages.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    archive = target + '.zip'
    if os.path.exists(archive):
        os.remove(archive)
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for folder, _, files in os.walk(target):
            for f in sorted(files):
                path = os.path.join(folder, f)
                z.write(path, os.path.relpath(path, target).replace('\\', '/'))
    print('written', target, 'and', archive)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'dist')
    os.makedirs(out, exist_ok=True)
    build(out, 'pegaso-chromium', False)
    build(out, 'pegaso-firefox', True)


if __name__ == '__main__':
    main()
