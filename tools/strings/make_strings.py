"""Generates srchybrid/PegasoStrings.h and .cpp from the table below.

Pegaso's own interface texts are not part of the eMule language DLLs, so they are
compiled into the executable in every shipped language and picked by the language
selected in the options. Non-ASCII characters are written as \\uXXXX escapes, which
keeps the generated source independent of code pages.

    python tools/strings/make_strings.py
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'srchybrid')

LANGS = ['en', 'es', 'fr', 'it', 'ru', 'pt', 'de']
# Windows primary language identifiers, in the order of LANGS (English is the fallback)
PRIMARY = {'es': 0x0A, 'fr': 0x0C, 'it': 0x10, 'ru': 0x19, 'pt': 0x16, 'de': 0x07}

STRINGS = [
    ('PS_OPT_TITLE', ['Web and BitTorrent', 'Web y BitTorrent', 'Web et BitTorrent', 'Web e BitTorrent',
                      'Веб и BitTorrent', 'Web e BitTorrent', 'Web und BitTorrent']),
    ('PS_OPT_BT_ENABLE', ['Enable BitTorrent', 'Activar BitTorrent', 'Activer BitTorrent', 'Attiva BitTorrent',
                          'Включить BitTorrent', 'Ativar BitTorrent', 'BitTorrent aktivieren']),
    ('PS_OPT_BT_PORT', ['Port for incoming connections', 'Puerto para conexiones entrantes', 'Port des connexions entrantes',
                        'Porta per le connessioni in entrata', 'Порт для входящих соединений',
                        'Porta para ligações de entrada', 'Port für eingehende Verbindungen']),
    ('PS_OPT_BT_ENCRYPT', ['Accept only encrypted connections', 'Aceptar solo conexiones cifradas',
                           'Accepter uniquement les connexions chiffrées', 'Accetta solo connessioni cifrate',
                           'Только зашифрованные соединения', 'Aceitar apenas ligações cifradas',
                           'Nur verschlüsselte Verbindungen zulassen']),
    ('PS_OPT_WEB_GROUP', ['Web downloads (HTTP and HTTPS)', 'Descargas web (HTTP y HTTPS)', 'Téléchargements web (HTTP et HTTPS)',
                          'Download web (HTTP e HTTPS)', 'Веб-загрузки (HTTP и HTTPS)', 'Transferências web (HTTP e HTTPS)',
                          'Web-Downloads (HTTP und HTTPS)']),
    ('PS_OPT_WEB_CONN', ['Connections per file (1 - 16)', 'Conexiones por archivo (1 - 16)', 'Connexions par fichier (1 - 16)',
                         'Connessioni per file (1 - 16)', 'Соединений на файл (1 - 16)', 'Ligações por ficheiro (1 - 16)',
                         'Verbindungen pro Datei (1 - 16)']),
    ('PS_TORRENT_FILES', ['Choose files...', 'Elegir archivos...', 'Choisir les fichiers...', 'Scegli i file...',
                          'Выбрать файлы...', 'Escolher ficheiros...', 'Dateien auswählen...']),
    ('PS_TORRENT_FILES_TITLE', ['Files of the torrent', 'Archivos del torrent', 'Fichiers du torrent', 'File del torrent',
                                'Файлы торрента', 'Ficheiros do torrent', 'Dateien des Torrents']),
    ('PS_TORRENT_FILES_HINT', ['Untick the files you do not want to download.', 'Desmarca los archivos que no quieras descargar.',
                               'Décochez les fichiers que vous ne voulez pas télécharger.',
                               'Deseleziona i file che non vuoi scaricare.', 'Снимите отметку с файлов, которые не нужно загружать.',
                               'Desmarque os ficheiros que não pretende transferir.',
                               'Entfernen Sie die Häkchen bei Dateien, die nicht geladen werden sollen.']),
    ('PS_TORRENT_NO_METADATA', ['The list of files is not known yet. Try again in a moment.',
                                'Todavía no se conoce la lista de archivos. Inténtalo de nuevo en un momento.',
                                "La liste des fichiers n'est pas encore connue. Réessayez dans un instant.",
                                "L'elenco dei file non è ancora noto. Riprova tra un momento.",
                                'Список файлов пока неизвестен. Повторите попытку чуть позже.',
                                'A lista de ficheiros ainda não é conhecida. Tente novamente dentro de momentos.',
                                'Die Dateiliste ist noch nicht bekannt. Bitte gleich noch einmal versuchen.']),
    ('PS_TORRENT_METADATA', ['Getting information', 'Obteniendo información', 'Récupération des informations', 'Recupero informazioni',
                             'Получение сведений', 'A obter informação', 'Informationen werden geladen']),
    ('PS_TORRENT_SEEDING', ['Complete (sharing)', 'Completo (compartiendo)', 'Terminé (partage)', 'Completo (in condivisione)',
                            'Завершено (раздача)', 'Completo (a partilhar)', 'Fertig (wird verteilt)']),
    ('PS_SELECT_ALL', ['Select all', 'Marcar todos', 'Tout cocher', 'Seleziona tutti', 'Отметить все', 'Marcar todos', 'Alle auswählen']),
    ('PS_SELECT_NONE', ['Select none', 'Desmarcar todos', 'Tout décocher', 'Deseleziona tutti', 'Снять все', 'Desmarcar todos', 'Keine auswählen']),
]


def esc(text):
    out = []
    for ch in text:
        if ch in '"\\':
            out.append('\\' + ch)
        elif ord(ch) < 127:
            out.append(ch)
        else:
            out.append('\\u%04x' % ord(ch))
    return ''.join(out)


def main():
    header = ['// Pegaso: interface texts which are not part of the eMule language DLLs.',
              '// Generated by tools/strings/make_strings.py - edit the table there.',
              '#pragma once', '', 'enum EPegasoString', '{']
    header += ['\t%s,' % name for name, _ in STRINGS]
    header += ['\tPS_COUNT', '};', '',
               '// Text in the language selected in the options; English if there is no translation.',
               'LPCTSTR GetPegasoString(EPegasoString id);', '']
    source = ['// Pegaso: interface texts. Generated by tools/strings/make_strings.py - do not edit.',
              '#include "stdafx.h"', '#include "emule.h"', '#include "Preferences.h"', '#include "PegasoStrings.h"', '',
              'namespace', '{',
              '\tconst int LANG_COUNT = %d;\t// %s' % (len(LANGS), ', '.join(LANGS)),
              '\tconst WORD s_awPrimary[LANG_COUNT] = {LANG_ENGLISH, %s};' % ', '.join('0x%02x' % PRIMARY[l] for l in LANGS[1:]),
              '',
              '\tconst LPCWSTR s_apszText[PS_COUNT][LANG_COUNT] = {']
    for name, texts in STRINGS:
        assert len(texts) == len(LANGS), name
        source.append('\t\t// ' + name)
        source.append('\t\t{' + ', '.join('L"%s"' % esc(t) for t in texts) + '},')
    source += ['\t};', '}', '',
               'LPCTSTR GetPegasoString(EPegasoString id)', '{',
               '\tif (id < 0 || id >= PS_COUNT)', '\t\treturn _T("");',
               '\tconst WORD wPrimary = PRIMARYLANGID(thePrefs.GetLanguageID());',
               '\tfor (int i = 1; i < LANG_COUNT; ++i)',
               '\t\tif (s_awPrimary[i] == wPrimary)', '\t\t\treturn s_apszText[id][i];',
               '\treturn s_apszText[id][0];', '}', '']
    for name, lines in (('PegasoStrings.h', header), ('PegasoStrings.cpp', source)):
        with open(os.path.join(OUT, name), 'w', encoding='ascii', newline='\r\n') as f:
            f.write('\n'.join(lines))
    print('written PegasoStrings.h/.cpp with %d strings in %d languages' % (len(STRINGS), len(LANGS)))


if __name__ == '__main__':
    main()
