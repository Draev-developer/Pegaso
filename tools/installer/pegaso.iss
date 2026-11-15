; Pegaso installer (Inno Setup 6).
;
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" tools\installer\pegaso.iss
;
; Takes Pegaso.exe and the language DLLs from the Release build
; (srchybrid\x64\Release) and writes tools\installer\output\Pegaso-<version>-setup.exe.
; Installs per user by default (no administrator rights needed); "install for all
; users" can be chosen when the installer is started as administrator.
; Settings and downloads live outside the program folder (%LOCALAPPDATA%\Pegaso and
; Downloads\Pegaso), so removing or updating the program does not touch them.

#define AppName "Pegaso"
#define AppVersion "0.3"
#define Release "..\..\srchybrid\x64\Release"

[Setup]
AppId={{14D11134-ABAA-4D8E-B3CE-9993BD325C7A}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Pegaso
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\..\license.txt
SetupIconFile=..\..\srchybrid\res\Mule_Vista.ico
UninstallDisplayIcon={app}\Pegaso.exe
; another folder: ISCC /DOutDir=<folder> pegaso.iss
#ifndef OutDir
  #define OutDir "output"
#endif
OutputDir={#OutDir}
OutputBaseFilename=Pegaso-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
ChangesAssociations=yes
; the installer starts in the language of Windows without asking, if it has it
ShowLanguageDialog=auto

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "pt"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"

[CustomMessages]
en.AssocTorrent=Open .torrent files with Pegaso
es.AssocTorrent=Abrir los archivos .torrent con Pegaso
fr.AssocTorrent=Ouvrir les fichiers .torrent avec Pegaso
it.AssocTorrent=Apri i file .torrent con Pegaso
ru.AssocTorrent=Открывать файлы .torrent в Pegaso
pt.AssocTorrent=Abrir os ficheiros .torrent com o Pegaso
ptbr.AssocTorrent=Abrir os arquivos .torrent com o Pegaso
de.AssocTorrent=.torrent-Dateien mit Pegaso öffnen
en.AssocMagnet=Open magnet links with Pegaso
es.AssocMagnet=Abrir los enlaces magnet con Pegaso
fr.AssocMagnet=Ouvrir les liens magnet avec Pegaso
it.AssocMagnet=Apri i link magnet con Pegaso
ru.AssocMagnet=Открывать magnet-ссылки в Pegaso
pt.AssocMagnet=Abrir as ligações magnet com o Pegaso
ptbr.AssocMagnet=Abrir os links magnet com o Pegaso
de.AssocMagnet=Magnet-Links mit Pegaso öffnen
en.AssocEd2k=Open ed2k links with Pegaso
es.AssocEd2k=Abrir los enlaces ed2k con Pegaso
fr.AssocEd2k=Ouvrir les liens ed2k avec Pegaso
it.AssocEd2k=Apri i link ed2k con Pegaso
ru.AssocEd2k=Открывать ed2k-ссылки в Pegaso
pt.AssocEd2k=Abrir as ligações ed2k com o Pegaso
ptbr.AssocEd2k=Abrir os links ed2k com o Pegaso
de.AssocEd2k=ed2k-Links mit Pegaso öffnen
en.Links=Links and files:
es.Links=Enlaces y archivos:
fr.Links=Liens et fichiers :
it.Links=Link e file:
ru.Links=Ссылки и файлы:
pt.Links=Ligações e ficheiros:
ptbr.Links=Links e arquivos:
de.Links=Links und Dateien:

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "ed2k"; Description: "{cm:AssocEd2k}"; GroupDescription: "{cm:Links}"
Name: "magnet"; Description: "{cm:AssocMagnet}"; GroupDescription: "{cm:Links}"
Name: "torrent"; Description: "{cm:AssocTorrent}"; GroupDescription: "{cm:Links}"

[Files]
Source: "{#Release}\Pegaso.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Release}\lang\*.dll"; DestDir: "{app}\lang"; Flags: ignoreversion
Source: "..\..\geo\ip-country.dat"; DestDir: "{app}\geo"; Flags: ignoreversion
Source: "..\..\geo\flags.bmp"; DestDir: "{app}\geo"; Flags: ignoreversion
Source: "..\..\geo\README.txt"; DestDir: "{app}\geo"; Flags: ignoreversion
Source: "..\..\license*.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "LEEME.txt"; DestDir: "{app}"; Flags: ignoreversion

[INI]
; Pegaso starts in the language the installer ran in (which follows the language of Windows).
; Only for a configuration which has no language yet: an existing choice is kept. Other users
; of the computer get the language of their own Windows the first time they start Pegaso.
; The values are the language identifiers of the translations shipped in the lang folder.
Filename: "{localappdata}\Pegaso\config\preferences.ini"; Section: "eMule"; Key: "Language"; String: "1033"; Flags: createkeyifdoesntexist; Languages: en
Filename: "{localappdata}\Pegaso\config\preferences.ini"; Section: "eMule"; Key: "Language"; String: "1034"; Flags: createkeyifdoesntexist; Languages: es
Filename: "{localappdata}\Pegaso\config\preferences.ini"; Section: "eMule"; Key: "Language"; String: "1036"; Flags: createkeyifdoesntexist; Languages: fr
Filename: "{localappdata}\Pegaso\config\preferences.ini"; Section: "eMule"; Key: "Language"; String: "1040"; Flags: createkeyifdoesntexist; Languages: it
Filename: "{localappdata}\Pegaso\config\preferences.ini"; Section: "eMule"; Key: "Language"; String: "1049"; Flags: createkeyifdoesntexist; Languages: ru
Filename: "{localappdata}\Pegaso\config\preferences.ini"; Section: "eMule"; Key: "Language"; String: "2070"; Flags: createkeyifdoesntexist; Languages: pt
Filename: "{localappdata}\Pegaso\config\preferences.ini"; Section: "eMule"; Key: "Language"; String: "1046"; Flags: createkeyifdoesntexist; Languages: ptbr
Filename: "{localappdata}\Pegaso\config\preferences.ini"; Section: "eMule"; Key: "Language"; String: "1031"; Flags: createkeyifdoesntexist; Languages: de

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Pegaso.exe"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Pegaso.exe"; Tasks: desktopicon

[Registry]
; ed2k:// links
Root: HKA; Subkey: "Software\Classes\ed2k"; ValueType: string; ValueName: ""; ValueData: "URL:ed2k Protocol"; Flags: uninsdeletekey; Tasks: ed2k
Root: HKA; Subkey: "Software\Classes\ed2k"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""; Tasks: ed2k
Root: HKA; Subkey: "Software\Classes\ed2k\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Pegaso.exe,0"; Tasks: ed2k
Root: HKA; Subkey: "Software\Classes\ed2k\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Pegaso.exe"" ""%1"""; Tasks: ed2k
; magnet: links
Root: HKA; Subkey: "Software\Classes\magnet"; ValueType: string; ValueName: ""; ValueData: "URL:magnet Protocol"; Flags: uninsdeletekey; Tasks: magnet
Root: HKA; Subkey: "Software\Classes\magnet"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""; Tasks: magnet
Root: HKA; Subkey: "Software\Classes\magnet\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Pegaso.exe,0"; Tasks: magnet
Root: HKA; Subkey: "Software\Classes\magnet\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Pegaso.exe"" ""%1"""; Tasks: magnet
; .torrent files
Root: HKA; Subkey: "Software\Classes\.torrent"; ValueType: string; ValueName: ""; ValueData: "Pegaso.torrent"; Flags: uninsdeletevalue; Tasks: torrent
Root: HKA; Subkey: "Software\Classes\Pegaso.torrent"; ValueType: string; ValueName: ""; ValueData: "BitTorrent"; Flags: uninsdeletekey; Tasks: torrent
Root: HKA; Subkey: "Software\Classes\Pegaso.torrent\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Pegaso.exe,0"; Tasks: torrent
Root: HKA; Subkey: "Software\Classes\Pegaso.torrent\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Pegaso.exe"" ""%1"""; Tasks: torrent

[Run]
Filename: "{app}\Pegaso.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
