# Renaming videos

Tools → "Rename videos…" gives finished videos the name of the film or series they are and, if you wish, takes them to another folder. They do not cease to be shared on eD2K or seeded on BitTorrent. · [Español](../renombrar-videos.md)

- Films: `Name (year).ext`
- Series: `Name - 1x05.ext` (season x episode)

## How to use it

1. Choose a database for the series and another for the films.
2. Press "Look the names up". The videos which were found are ticked by themselves.
3. Check the "New name" column. A name is corrected by hand with F2 or a slow click on it.
4. If you want them moved, type or choose a folder for the films and another for the series. Empty: they stay where they are.
5. Press "Rename". You can go on using Pegaso while they are moved.

What each file is, is read from its name: `S01E05`, `1x05`, `Cap.105`, "Temporada 1 Capítulo 5" or, for a film, the year. With names that say nothing there is nothing to look up.

## Which databases ask for a key

| Database | Good for | Key |
|---|---|---|
| TVmaze | Series | None |
| AniDB | Anime | None (see below) |
| TheMovieDB | Series and films | Free |
| OMDb | Films | Free (1,000 lookups a day) |
| TheTVDB | Series | May ask for a paid subscription |

Pegaso ships with no key: everyone uses their own. They are typed in the "Keys…" button, which also opens the website of each service, and are stored encrypted for your Windows account. To start without signing up for anything, use TVmaze for the series; for the films one of the two free keys is needed.

## How to get each key

### TheMovieDB

1. Make a free account at <https://www.themoviedb.org/signup> and confirm the e-mail.
2. Logged in, go to <https://www.themoviedb.org/settings/api> (Settings → API).
3. Ask for a key of the "Developer" kind (personal use). It asks you to accept the terms and fill in a short form: for the use, it is enough to say it is to name your own files.
4. Copy the "API Key" (32 letters and digits) or the "API Read Access Token" (much longer). Pegaso takes either.
5. Paste it in "Keys…", in the TheMovieDB box.

### OMDb

1. Go to <https://www.omdbapi.com/apikey.aspx>.
2. Choose the "FREE" plan (1,000 lookups a day), give your e-mail and a name, and describe the use in one line.
3. You get an e-mail with the key (8 letters and digits) and an activation link. **That link has to be opened**: without activating, the key does not work.
4. Paste it in "Keys…", in the OMDb box.

### TheTVDB

1. Make an account at <https://thetvdb.com>.
2. Go to <https://thetvdb.com/api-information> and ask for a key of version 4 of their API.
3. TheTVDB may ask for a paid subscription to give it. If you do not want to pay, TVmaze and TheMovieDB do the same job for the series.
4. Paste it in "Keys…", in the TheTVDB box.

### AniDB

AniDB has no search for programs: it publishes the list of all its titles, and Pegaso fetches it once a day at most and looks it up on your computer. On some connections AniDB puts a check for browsers in front, which Pegaso does not get round. If the window warns you of it:

1. Open <https://anidb.net/api/anime-titles.dat.gz> in your browser and save the file, without unpacking it.
2. Leave it in the configuration folder of Pegaso (the window tells you which one it is; normally `%LOCALAPPDATA%\Pegaso\config`), under the name `anidb-titles.dat.gz`.
3. Press "Look the names up" again.

## If a lookup fails

- **"a valid key is missing"**: the box is empty, the key was copied wrong or, with OMDb, the activation link has not been opened.
- **"not found"**: the database does not know that title as it is written in the file name. Try another database or correct the name by hand.

## The folder the video came from

With the box "When a video is moved away, remove the folder it was in…" ticked, when a video is taken out of a folder which is inside your download folders:

- if another video is left in the folder, it is not touched;
- what a torrent of the list still offers (an `.nfo`, some subtitles) is kept apart, with the state of the engine, so that the torrent stays complete;
- the rest goes to the Recycle Bin along with the folder.

The download folders themselves are never removed.
