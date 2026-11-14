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
    ('PS_RATE_MENU', ['Rate this file...', 'Valorar este archivo...', 'Évaluer ce fichier...', 'Valuta questo file...',
                      'Оценить файл...', 'Avaliar este ficheiro...', 'Datei bewerten...']),
    ('PS_RATE_TITLE', ['Rate file', 'Valorar archivo', 'Évaluer le fichier', 'Valuta il file', 'Оценка файла', 'Avaliar ficheiro',
                       'Datei bewerten']),
    ('PS_RATE_OK', ['Correct: it is what it says and it plays well', 'Correcto: es lo que dice y se ve bien',
                    "Correct : c'est bien ce qui est annoncé et la qualité est bonne", 'Corretto: è ciò che dice e si vede bene',
                    'Всё верно: файл соответствует названию, качество хорошее', 'Correto: é o que diz e vê-se bem',
                    'Korrekt: Inhalt stimmt und die Qualität ist gut']),
    ('PS_RATE_BAD', ['Poor quality: it looks or sounds bad', 'Mala calidad: se ve o se oye mal',
                     "Mauvaise qualité : l'image ou le son sont mauvais", 'Scarsa qualità: si vede o si sente male',
                     'Плохое качество изображения или звука', 'Má qualidade: vê-se ou ouve-se mal',
                     'Schlechte Qualität: Bild oder Ton sind schlecht']),
    ('PS_RATE_FAKE', ['Fake: it is not what the name says', 'Falso (fake): no es lo que dice el nombre',
                      "Faux (fake) : ce n'est pas ce que dit le nom", 'Falso (fake): non è ciò che dice il nome',
                      'Фейк: содержимое не соответствует названию', 'Falso (fake): não é o que o nome diz',
                      'Fake: Inhalt entspricht nicht dem Namen']),
    ('PS_RATE_MISMATCH', ['It does not match its description', 'No cumple con la descripción', 'Ne correspond pas à sa description',
                          'Non corrisponde alla descrizione', 'Не соответствует описанию', 'Não corresponde à descrição',
                          'Entspricht nicht der Beschreibung']),
    ('PS_RATE_NONE', ['No rating', 'Sin valoración', 'Aucune évaluation', 'Nessuna valutazione', 'Без оценки', 'Sem avaliação',
                      'Keine Bewertung']),
    ('PS_RATE_WHAT', ['What does not match', 'Qué no coincide', 'Ce qui ne correspond pas', 'Cosa non corrisponde',
                      'Что не соответствует', 'O que não corresponde', 'Was nicht stimmt']),
    ('PS_RATE_WHAT_LANG', ['The language', 'El idioma', 'La langue', 'La lingua', 'Язык', 'O idioma', 'Die Sprache']),
    ('PS_RATE_WHAT_CONTENT', ['The content', 'El contenido', 'Le contenu', 'Il contenuto', 'Содержимое', 'O conteúdo', 'Der Inhalt']),
    ('PS_RATE_WHAT_QUALITY', ['The advertised quality', 'La calidad anunciada', 'La qualité annoncée', 'La qualità dichiarata',
                              'Заявленное качество', 'A qualidade anunciada', 'Die angegebene Qualität']),
    ('PS_RATE_LANG', ['Real language', 'Idioma real', 'Langue réelle', 'Lingua reale', 'Настоящий язык', 'Idioma real',
                      'Tatsächliche Sprache']),
    ('PS_RATE_TEXT', ['Comment (optional)', 'Comentario (opcional)', 'Commentaire (facultatif)', 'Commento (facoltativo)',
                      'Комментарий (необязательно)', 'Comentário (opcional)', 'Kommentar (optional)']),
    ('PS_RATE_HINT', ['Your rating is shared with the other users who download this file.',
                      'Tu valoración se comparte con los demás usuarios que descargan este archivo.',
                      'Votre évaluation est partagée avec les autres utilisateurs qui téléchargent ce fichier.',
                      'La tua valutazione viene condivisa con gli altri utenti che scaricano questo file.',
                      'Ваша оценка будет видна другим пользователям, загружающим этот файл.',
                      'A sua avaliação é partilhada com os outros utilizadores que transferem este ficheiro.',
                      'Ihre Bewertung wird mit den anderen Nutzern geteilt, die diese Datei laden.']),
    ('PS_RATE_OTHERS', ['Other users: %s', 'Otros usuarios: %s', 'Autres utilisateurs : %s', 'Altri utenti: %s',
                        'Другие пользователи: %s', 'Outros utilizadores: %s', 'Andere Nutzer: %s']),
    ('PS_RATE_OTHERS_NONE', ['No ratings from other users yet.', 'Todavía no hay valoraciones de otros usuarios.',
                             "Pas encore d'évaluations d'autres utilisateurs.", 'Ancora nessuna valutazione di altri utenti.',
                             'Оценок других пользователей пока нет.', 'Ainda não há avaliações de outros utilizadores.',
                             'Noch keine Bewertungen anderer Nutzer.']),
    ('PS_RATE_SUM_OK', ['%u correct', '%u correcto', '%u correct', '%u corretto', '%u верно', '%u correto', '%u korrekt']),
    ('PS_RATE_SUM_BAD', ['%u poor quality', '%u mala calidad', '%u mauvaise qualité', '%u scarsa qualità', '%u плохое качество',
                         '%u má qualidade', '%u schlechte Qualität']),
    ('PS_RATE_SUM_FAKE', ['%u fake', '%u falso', '%u faux', '%u falso', '%u фейк', '%u falso', '%u Fake']),
    ('PS_RATE_SUM_MISMATCH', ['%u wrong description', '%u no coincide', '%u non conforme', '%u non corrisponde',
                              '%u не соответствует', '%u não corresponde', '%u abweichend']),
    ('PS_RATE_WARN', ['Warning: %u users report "%s" as fake or wrongly described',
                      'Aviso: %u usuarios indican que "%s" es falso o no cumple con su descripción',
                      'Attention : %u utilisateurs signalent « %s » comme faux ou mal décrit',
                      'Attenzione: %u utenti segnalano "%s" come falso o descritto male',
                      'Внимание: %u пользователей сообщают, что "%s" - фейк или не соответствует описанию',
                      'Aviso: %u utilizadores indicam que "%s" é falso ou não corresponde à descrição',
                      'Achtung: %u Nutzer melden "%s" als Fake oder falsch beschrieben']),
    ('PS_RATE_FLAG', ['SUSPICIOUS', 'SOSPECHOSO', 'SUSPECT', 'SOSPETTO', 'ПОДОЗРИТЕЛЬНО', 'SUSPEITO', 'VERDÄCHTIG']),
    ('PS_AV_DETECTED', ['Antivirus: "%s" was reported as malicious', 'Antivirus: "%s" ha sido detectado como malicioso',
                        'Antivirus : « %s » a été détecté comme malveillant', 'Antivirus: "%s" è stato rilevato come dannoso',
                        'Антивирус: файл "%s" определён как вредоносный', 'Antivírus: "%s" foi detetado como malicioso',
                        'Virenschutz: "%s" wurde als schädlich gemeldet']),
    ('PS_WS_METHOD', ['Web (torrents)', 'Web (torrents)', 'Web (torrents)', 'Web (torrent)', 'Веб (торренты)', 'Web (torrents)',
                      'Web (Torrents)']),
    ('PS_SRC_ADD_WEB', ['Add web source...', 'Añadir fuente web...', 'Ajouter une source web...', 'Aggiungi fonte web...',
                        'Добавить веб-источник...', 'Adicionar fonte web...', 'Web-Quelle hinzufügen...']),
    ('PS_SRC_ADD_WEB_HINT', ['Address (http://) where this same file can be downloaded',
                             'Dirección (http://) donde se puede descargar este mismo archivo',
                             'Adresse (http://) où ce même fichier peut être téléchargé',
                             'Indirizzo (http://) da cui scaricare questo stesso file',
                             'Адрес (http://), по которому доступен этот же файл',
                             'Endereço (http://) onde este mesmo ficheiro pode ser transferido',
                             'Adresse (http://), unter der dieselbe Datei geladen werden kann']),
    ('PS_TORRENT_UPLOAD', ['upload %s/s', 'subida %s/s', 'envoi %s/s', 'invio %s/s', 'отдача %s/с', 'envio %s/s', 'Upload %s/s']),
    ('PS_HYB_ADD', ['Add a torrent of this same file...', 'Añadir un torrent de este mismo archivo...',
                    'Ajouter un torrent de ce même fichier...', 'Aggiungi un torrent di questo stesso file...',
                    'Добавить торрент этого же файла...', 'Adicionar um torrent deste mesmo ficheiro...',
                    'Torrent derselben Datei hinzufügen...']),
    ('PS_HYB_HINT', ['Magnet link, or path of a .torrent file, which contains this same file',
                     'Enlace magnet, o ruta de un archivo .torrent, que contenga este mismo archivo',
                     "Lien magnet, ou chemin d'un fichier .torrent, contenant ce même fichier",
                     'Link magnet, o percorso di un file .torrent, che contiene questo stesso file',
                     'Magnet-ссылка или путь к файлу .torrent, содержащему этот же файл',
                     'Ligação magnet, ou caminho de um ficheiro .torrent, que contenha este mesmo ficheiro',
                     'Magnet-Link oder Pfad einer .torrent-Datei, die dieselbe Datei enthält']),
    ('PS_ADD_LINKS', ['Add download links', 'Añadir enlaces de descarga', 'Ajouter des liens de téléchargement',
                      'Aggiungi link di download', 'Добавить ссылки для загрузки', 'Adicionar ligações de transferência',
                      'Download-Links hinzufügen']),
    ('PS_ADD_LINKS_HINT', ['Links, one per line (web addresses, magnet, ed2k):',
                           'Enlaces, uno por línea (direcciones web, magnet, ed2k):',
                           'Liens, un par ligne (adresses web, magnet, ed2k) :',
                           'Link, uno per riga (indirizzi web, magnet, ed2k):',
                           'Ссылки, по одной в строке (веб-адреса, magnet, ed2k):',
                           'Ligações, uma por linha (endereços web, magnet, ed2k):',
                           'Links, einer pro Zeile (Web-Adressen, Magnet, ed2k):']),
    ('PS_LINK_HOME',['Pegaso on GitHub', 'Pegaso en GitHub', 'Pegaso sur GitHub', 'Pegaso su GitHub', 'Pegaso на GitHub',
                      'Pegaso no GitHub', 'Pegaso auf GitHub']),
    ('PS_LINK_HELP', ['Help and problems', 'Ayuda y problemas', 'Aide et problèmes', 'Aiuto e problemi', 'Помощь и проблемы',
                      'Ajuda e problemas', 'Hilfe und Probleme']),
    ('PS_LINK_VC', ['Check for new versions of Pegaso', 'Comprobar si hay versiones nuevas de Pegaso',
                    'Rechercher de nouvelles versions de Pegaso', 'Controlla se ci sono nuove versioni di Pegaso',
                    'Проверить наличие новых версий Pegaso', 'Verificar se há novas versões do Pegaso',
                    'Nach neuen Versionen von Pegaso suchen']),
    ('PS_TORRENT_SHARED',['shared %s', 'compartido %s', 'partagé %s', 'condiviso %s', 'отдано %s', 'partilhado %s', 'verteilt %s']),
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
