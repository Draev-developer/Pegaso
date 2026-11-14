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
    ('PS_OPT_VPN_GROUP', ['VPN protection', 'Protección VPN', 'Protection VPN', 'Protezione VPN', 'Защита VPN', 'Proteção VPN', 'VPN-Schutz']),
    ('PS_OPT_VPN_ADAPTER', ['Use only this network adapter', 'Usar solo este adaptador de red', 'Utiliser uniquement cette carte réseau',
                            'Usa solo questa scheda di rete', 'Использовать только этот сетевой адаптер',
                            'Usar apenas este adaptador de rede', 'Nur diesen Netzwerkadapter verwenden']),
    ('PS_OPT_VPN_NONE', ['(any adapter - no protection)', '(cualquier adaptador - sin protección)', "(n'importe quelle carte - sans protection)",
                         '(qualsiasi scheda - nessuna protezione)', '(любой адаптер - без защиты)',
                         '(qualquer adaptador - sem proteção)', '(beliebiger Adapter - kein Schutz)']),
    ('PS_OPT_VPN_KILL', ['Stop all transfers while it is disconnected', 'Detener todas las transferencias mientras esté desconectado',
                         'Arrêter tous les transferts tant qu\'elle est déconnectée', 'Ferma tutti i trasferimenti mentre è disconnessa',
                         'Останавливать все передачи, пока он отключён', 'Parar todas as transferências enquanto estiver desligado',
                         'Alle Übertragungen anhalten, solange er getrennt ist']),
    ('PS_OPT_VPN_DOWN', ['not connected', 'no conectado', 'non connectée', 'non connessa', 'не подключён', 'não ligado', 'nicht verbunden']),
    # web search (Torznab providers configured by the user)
    ('PS_WS_MENU', ['Search torrents on the web...', 'Buscar torrents en la web...', 'Rechercher des torrents sur le web...',
                    'Cerca torrent sul web...', 'Поиск торрентов в интернете...', 'Procurar torrents na web...', 'Torrents im Web suchen...']),
    ('PS_WS_TITLE', ['Web search', 'Búsqueda web', 'Recherche web', 'Ricerca web', 'Поиск в интернете', 'Pesquisa web', 'Websuche']),
    ('PS_WS_SEARCH', ['Search', 'Buscar', 'Rechercher', 'Cerca', 'Искать', 'Procurar', 'Suchen']),
    ('PS_WS_DOWNLOAD', ['Download', 'Descargar', 'Télécharger', 'Scarica', 'Загрузить', 'Transferir', 'Herunterladen']),
    ('PS_WS_CLOSE', ['Close', 'Cerrar', 'Fermer', 'Chiudi', 'Закрыть', 'Fechar', 'Schließen']),
    ('PS_WS_PROVIDERS', ['Providers...', 'Proveedores...', 'Fournisseurs...', 'Provider...', 'Источники...', 'Fornecedores...', 'Anbieter...']),
    ('PS_WS_NO_PROVIDERS', ['No search provider is configured. Add a Torznab provider first (for example your own Jackett or Prowlarr).',
                            'No hay ningún proveedor de búsqueda configurado. Añade antes un proveedor Torznab (por ejemplo tu propio Jackett o Prowlarr).',
                            "Aucun fournisseur de recherche n'est configuré. Ajoutez d'abord un fournisseur Torznab (par exemple votre propre Jackett ou Prowlarr).",
                            'Nessun provider di ricerca configurato. Aggiungi prima un provider Torznab (ad esempio il tuo Jackett o Prowlarr).',
                            'Источники поиска не настроены. Сначала добавьте источник Torznab (например, свой Jackett или Prowlarr).',
                            'Não há nenhum fornecedor de pesquisa configurado. Adicione primeiro um fornecedor Torznab (por exemplo o seu Jackett ou Prowlarr).',
                            'Es ist kein Suchanbieter eingerichtet. Bitte zuerst einen Torznab-Anbieter hinzufügen (zum Beispiel den eigenen Jackett oder Prowlarr).']),
    ('PS_WS_SEARCHING', ['Searching...', 'Buscando...', 'Recherche en cours...', 'Ricerca in corso...', 'Идёт поиск...', 'A procurar...', 'Suche läuft...']),
    ('PS_WS_RESULTS', ['%d results', '%d resultados', '%d résultats', '%d risultati', 'Результатов: %d', '%d resultados', '%d Ergebnisse']),
    ('PS_WS_BLOCKED', ['VPN protection is holding all network activity.', 'La protección VPN mantiene detenida toda la actividad de red.',
                       'La protection VPN bloque toute activité réseau.', 'La protezione VPN blocca tutta l\'attività di rete.',
                       'Защита VPN остановила всю сетевую активность.', 'A proteção VPN está a reter toda a atividade de rede.',
                       'Der VPN-Schutz hält alle Netzwerkaktivitäten an.']),
    ('PS_WS_COL_SEEDS', ['Seeds', 'Semillas', 'Sources', 'Seed', 'Сиды', 'Sementes', 'Seeds']),
    ('PS_WS_COL_PEERS', ['Peers', 'Pares', 'Pairs', 'Peer', 'Пиры', 'Pares', 'Peers']),
    ('PS_WS_COL_PROVIDER', ['Provider', 'Proveedor', 'Fournisseur', 'Provider', 'Источник', 'Fornecedor', 'Anbieter']),
    ('PS_WS_PROV_TITLE', ['Search providers (Torznab)', 'Proveedores de búsqueda (Torznab)', 'Fournisseurs de recherche (Torznab)',
                          'Provider di ricerca (Torznab)', 'Источники поиска (Torznab)', 'Fornecedores de pesquisa (Torznab)',
                          'Suchanbieter (Torznab)']),
    ('PS_WS_PROV_NAME', ['Name', 'Nombre', 'Nom', 'Nome', 'Название', 'Nome', 'Name']),
    ('PS_WS_PROV_URL', ['Address of the Torznab API', 'Dirección de la API Torznab', "Adresse de l'API Torznab", "Indirizzo dell'API Torznab",
                        'Адрес API Torznab', 'Endereço da API Torznab', 'Adresse der Torznab-API']),
    ('PS_WS_PROV_KEY', ['API key', 'Clave de API', 'Clé API', 'Chiave API', 'Ключ API', 'Chave de API', 'API-Schlüssel']),
    ('PS_WS_PROV_SAVE', ['Add / update', 'Añadir / actualizar', 'Ajouter / mettre à jour', 'Aggiungi / aggiorna', 'Добавить / обновить',
                         'Adicionar / atualizar', 'Hinzufügen / ändern']),
    ('PS_WS_PROV_REMOVE', ['Remove', 'Quitar', 'Supprimer', 'Rimuovi', 'Удалить', 'Remover', 'Entfernen']),
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
