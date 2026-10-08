# Döda Kvarter's songs

Provided by JorreFog for 0.3, built into the game (`src/music.c` includes them in the program with `.incbin`, so an
update that brings a new program brings its songs too) and decoded as they play by minimp3 (`src/third_party`, CC0).

| File | Plays | |
|---|---|---|
| `menu.mp3` | the title, the settings, the high scores | goes round |
| `gameplay.mp3` | under the play | goes round; after a boss, the box's tune or a found song it picks up where it was |
| `boss.mp3` | a boss fight | goes round, from the start for each boss |
| `gameover.mp3` | game over | once |

They're re-encoded from the originals at 112 kbps, 48 kHz stereo, without tags or cover art, to keep the game small:
`ffmpeg -i <original> -map 0:a -c:a libmp3lame -b:a 112k -ar 48000 -ac 2 -map_metadata -1 -id3v2_version 0 -write_xing 0 <file>`.
To change a song, replace its file and build again. The Mystery Box's tune, the perk jingles and the three gnomes'
song stay synthesized (`src/audio.c`).
