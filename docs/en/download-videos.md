# Downloading a video from a web page

Pegaso can fetch a video published on a website, choosing its quality, its language and the name of the file. · [Español](../descargar-videos.md)

## How to use it

1. Paste the address into the usual link field, or open **Tools → Download a video from a web page…** and type it there. The address of the page or that of the video list (`.m3u8`, `.mpd`) will do.
2. Pegaso looks at the address and shows the **qualities** and **languages** the video is published in. The best quality comes chosen and, if there is one, the language of Pegaso.
3. Type the **file name** (the title of the page is offered) and press **Download**.

You can go on using Pegaso while it downloads. The file is left in the download folder, or in the folder of the videos if you have set one in "Automatic folders".

## Which videos it takes

Those the site publishes as an open list: a text file (HLS, `.m3u8`, or DASH, `.mpd`) which anybody can read and which names the pieces of the video in each quality and language. It is the format of many public broadcasters, courses, conferences and self-hosted servers.

## Which videos it does not take

- **Encrypted ones.** Pegaso says so and asks for neither the key nor the pieces.
- **Live broadcasts**, which have no end to download up to.
- **Those of sites which do not publish the list.** Many platforms do not put the address of the video in the page: their player asks a private service for it, with a login, access credentials or copy protection. There Pegaso finds nothing and says so. Nothing in Pegaso is written for one site in particular, nor to get round those controls.

Download only videos you have the right to keep.

## Video and sound: ffmpeg

When a video is offered in several languages, picture and sound travel in separate files. Joining them into one takes **ffmpeg**, a free program which Pegaso does not include but uses if it is on your computer:

- next to `Pegaso.exe`, or
- in the PATH of Windows, or
- where `FfmpegPath` says in the `[eMule]` section of `preferences.ini`.

With ffmpeg, Pegaso joins picture and sound into an `.mp4` without re-encoding anything. Without it, it leaves two files with the same name — the video and the sound, with the language in the name — and tells you before starting. Many players (VLC among them) open the video with the sound of a separate file.

If the video carries its sound inside (a single language), ffmpeg is not needed.

## Limits

- The pieces are fetched one after another: a one-hour video may take several minutes even on a fast connection.
- It does not resume: if you close Pegaso halfway, it has to start again.
- It does not show in the download list; the progress is shown in the window itself, of which there is only one.
- It does not count towards the speed limit of the preferences.
- Subtitles are not downloaded.
