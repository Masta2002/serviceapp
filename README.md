<h1 align="center">ServiceApp</h1>
<p align="center">Media service for Enigma2 that plays through external players: exteplayer3 and gstplayer</p>

<p align="center">
  <a href="https://github.com/oe-mirrors/serviceapp"><img alt="Repository size" src="https://img.shields.io/github/repo-size/oe-mirrors/serviceapp.svg"></a>
  <a href="https://visitorbadge.io/status?path=https://github.com/oe-mirrors/serviceapp"><img alt="Views" src="https://api.visitorbadge.io/api/visitors?path=https://github.com/oe-mirrors/serviceapp&label=Views&countColor=blue&style=flat"></a>
</p>

<div align="center">
<table>
  <tr>
    <td><b>🏗️ Build</b></td>
    <td>
      <a href="https://github.com/oe-mirrors/serviceapp/actions/workflows/compile.yml"><img alt="Compile check" src="https://github.com/oe-mirrors/serviceapp/actions/workflows/compile.yml/badge.svg"></a>
    </td>
  </tr>
  <tr>
    <td><b>📈 Activity</b></td>
    <td>
      <a href="https://github.com/oe-mirrors/serviceapp/commits"><img alt="Last commit" src="https://img.shields.io/github/last-commit/oe-mirrors/serviceapp"></a>
      <a href="https://github.com/oe-mirrors/serviceapp/commits"><img alt="Commits per year" src="https://img.shields.io/github/commit-activity/y/oe-mirrors/serviceapp.svg?label=commits%2Fyear"></a>
      <a href="https://github.com/oe-mirrors/serviceapp/graphs/contributors"><img alt="Contributors" src="https://img.shields.io/github/contributors/oe-mirrors/serviceapp?style=flat"></a>
    </td>
  </tr>
  <tr>
    <td><b>🤝 Community</b></td>
    <td>
      <a href="https://github.com/oe-mirrors/serviceapp/stargazers"><img alt="Stars" src="https://img.shields.io/github/stars/oe-mirrors/serviceapp?style=flat"></a>
      <a href="https://github.com/oe-mirrors/serviceapp/forks"><img alt="Forks" src="https://img.shields.io/github/forks/oe-mirrors/serviceapp?style=flat"></a>
      <a href="https://github.com/oe-mirrors/serviceapp/issues"><img alt="Issues" src="https://img.shields.io/github/issues/oe-mirrors/serviceapp?color=blue&style=flat"></a>
      <a href="https://github.com/oe-mirrors/serviceapp/pulls"><img alt="Pull requests welcome" src="https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat"></a>
    </td>
  </tr>
  <tr>
    <td><b>🧾 Project</b></td>
    <td>
      <a href="#-license"><img alt="License GPL-2.0" src="https://img.shields.io/badge/License-GPL--2.0-blue.svg"></a>
      <img alt="Platform Enigma2" src="https://img.shields.io/badge/Platform-Enigma2-orange.svg">
      <img alt="Top language" src="https://img.shields.io/github/languages/top/oe-mirrors/serviceapp?style=flat">
    </td>
  </tr>
</table>
</div>

---

## 📑 Contents

- [Overview](#-overview)
- [Key features](#-key-features)
- [exteplayer3 support](#-exteplayer3-support)
- [Settings](#%EF%B8%8F-settings)
- [Service references and url options](#-service-references-and-url-options)
- [Live streams: reconnect and errors](#-live-streams-reconnect-and-errors)
- [Resume, cut list and chapters](#%EF%B8%8F-resume-cut-list-and-chapters)
- [Recording](#-recording)
- [Translations](#-translations)
- [Build](#%EF%B8%8F-build)
- [Debug and log](#-debug-and-log)
- [Credits](#-credits)
- [License](#-license)

## 📦 Overview

ServiceApp is an Enigma2 service (C++ library plus a Python plugin) that hands
playback to an external player process and talks to it over stdin and JSON
messages on stderr:

| Service type | Player |
|---|---|
| `5001` | gstplayer (GStreamer) |
| `5002` | exteplayer3 (FFmpeg) |
| `4097` | optional: ServiceApp replaces servicemp3 and plays with the player chosen in the settings |

The original ServiceApp is [mx3L/serviceapp](https://github.com/mx3L/serviceapp).
This tree is the openATV branch of [oe-mirrors/serviceapp](https://github.com/oe-mirrors/serviceapp),
with the fixes and features collected from the other ServiceApp forks and the
support for the current exteplayer3 ([skyjet18/exteplayer3](https://github.com/skyjet18/exteplayer3), version 187).

## 🚀 Key features

- HTTP(S), HLS, DASH, RTMP, RTSP, local files; HTTP headers in the url
- HLS explorer: the variants of a HLS stream as subservices, picked by connection speed
- `resolve://` urls resolved by a script before playback
- audio track selection with codec, channel layout and title; dual mono left / stereo / right
- embedded, closed caption and external `.srt` subtitles, "Auto turn on subtitles" with a forced-only mode
- audio transcoding (DTS, TrueHD to Dolby Digital / Dolby Digital +, HE-AAC to AAC-LC) and HLS quality settings
- resume position, cut list and chapters for local files (`.cuts`)
- picture size, frame rate, HDR type, video and audio codec details for the info screens
- title and artist of web radio streams
- reconnect of live streams that end by themselves, player errors as messages
- recording of 5001/5002 streams with the native 4097 recorder

## 🎬 exteplayer3 support

ServiceApp reads the first line exteplayer3 prints, `EPLAYER3_EXTENDED`, and
passes an option only to a player that reports it: a player stops with its
usage text on an option it does not know. So an older exteplayer3 (for example
the one of an image) keeps working, it just offers less.

| Feature | Player (key in `EPLAYER3_EXTENDED`) | ServiceApp |
|---|---|---|
| DTS / TrueHD to Dolby Digital | `xcode` & 1: `--dts-ac3`, `--truehd-ac3` | setting "DTS / DTS-HD transcoding", "Dolby TrueHD transcoding" |
| ... to Dolby Digital + | `xcode` & 4: `--dts-eac3`, `--truehd-eac3` | choice "Dolby Digital +" in the same settings |
| HE-AAC to AAC-LC | `xcode` & 2: `--heaac-aaclc` | setting "HE-AAC transcoding" |
| HLS / DASH quality, default audio only | version 185: `-Q 0\|1\|2`, `-D 1` | settings "HLS quality", "HLS default audio only" |
| dual mono | `achannel`: stdin `am<n>`, `a_m` | iAudioChannelSelection (left, stereo, right) |
| codec, channels, layout, title of audio tracks | `a_l`/`a_c`: `d`, `c`, `cl`, `t`, `sr`, `br` | audio selection, `sTagAudioCodec`, `sTagBitrate` |
| forced and titled subtitle tracks, closed captions | `s_l`: `f`, `t`; CC1 sent by itself | subtitle selection (`forced`, `SDH`, `CC1`), list refreshed when captions start |
| subtitle flush | `s_f` | cues of the old position dropped after a seek |
| HDR type | `v_c` `hdr` | `sGamma`, evVideoGammaChanged |
| video profile, bit depth, Dolby Vision | `v_c` `pr`, `bd`, `dv` | `sTagVideoCodec` ("HEVC Main 10@5.1 10 bit DV8") |
| picture size from the decoder | `v_e` | sVideoWidth/Height, frame rate, progressive |
| web radio title / artist | `m_t` | `sTagTitle`, `sTagArtist`, evUpdatedInfo |
| errors | `FF_ERROR`, exit codes | `sUser+12` text, evUser+12; no reconnect on permanent errors |
| chapters | `c_l` (after 187) | marks of the cut list |
| seeks in ms | `seekms` (after 187): `gf<s.ms>` | every seek; older players get `gc<seconds>` |
| embedded cover | `cover` (after 187): `--cover` | `/tmp/.id3coverart` and evUser+13, as servicemp3 |
| CENC keys per KID | `drmkey` & 1: `--key` | url option `#sapp_key=` |
| keyframe winding | `trick` (DreamNextGen) | fast forward / rewind by the player |

## ⚙️ Settings

Menu → Setup → Recordings → **ServiceApp**, separately for servicemp3
(4097, when replaced), 5001 and 5002:

- player for servicemp3, replace servicemp3 (needs a GUI restart)
- **ServiceApp**: auto turn on subtitles (No / Yes / Forced only), HLS explorer, auto select stream, connection speed
- **exteplayer3**: AAC, EAC3, AC3, DTS, MP3, WMA software decoding, stereo downmix, LPCM injection, RTMP implementation; and, as far as the installed player reports them, the transcoding and HLS settings above
- **gstplayer**: sink, subtitles, buffer size and duration
- AC3+ passthrough fix (Vu+ Ultimo 4K / Duo 4K SE, only for E-AC-3 tracks)
- the versions of the installed players

From the movie list a file can also be played once with ServiceExtEplayer3 or
ServiceGstPlayer.

## 🔗 Service references and url options

```
5002:0:1:0:0:0:0:0:0:0:http%3a//example.com/live.m3u8#User-Agent=Mozilla&Referer=https%3a//example.com/:Channel name
```

Everything after `#` of an `http`/`rtsp` url is a list of `name=value` pairs
joined by `&`. Names are HTTP headers, except the player options that start with
`sapp_`:

| Option | Meaning |
|---|---|
| `sapp_audio_id=<n>` | audio track at start (also stops the audio cache of the channel) |
| `sapp_subtitle_id=<n>` | subtitle track at start |
| `sapp_live_ts=1` | live TS mode of exteplayer3 |
| `sapp_key=<kid>:<key>[,<kid>:<key>]` | CENC keys per KID (exteplayer3 187, `drmkey`) |
| `sapp_ffmpeg_option=<name>=<value>` | an FFmpeg option (`-f`) |
| `sapp_args=-<opt>=<value>-<opt>...` | more exteplayer3 options; another player only as an installed `/usr/bin/exteplayer3*` |
| `sapp_<setting>=<value>` | any exteplayer3 setting by its name (`aac_swdec`, `dts_swdec`, `hls_quality`, ...) |

`resolve://<url>` runs the resolver script first; `&suburi=<url>` adds an
external subtitle or audio url, the OE-A form `url.mpd&5=<key>` is taken by
exteplayer3 187 itself.

## 🔁 Live streams: reconnect and errors

A network stream without a length whose player stops by itself (server or proxy
closed the connection) is started again with a new player: after 0.5 s, then
every 3 s, at most 5 times in a row; the count starts again after 30 s of
playback. Audio track, dual mono and subtitle track are kept, `resolve://` urls
are resolved again. Then evEOF, as before.

No reconnect for errors a new attempt cannot fix: the player did not accept its
options (exit code 1), file or page not found, access denied, no key for the
video, HTTP 400, 401, 403, 404 and other 4xx. The error of the player
(`FF_ERROR`) is the text of `sUser+12` with evUser+12, also when the stream
could not be opened at all.

## 🗂️ Resume, cut list and chapters

Local files keep their marks, the last position and the length in the `.cuts`
file next to them, as recordings do: resume works, the length is known before
the player reports it, and a last position within 10 s of the end starts the
file from the beginning. Chapters of the file (Matroska, MP4/M4B, Ogg) are shown
as marks but never written to the `.cuts` file, so marks of the user stay apart.

## 📼 Recording

enigma2 records only its own reference types. ServiceApp lets 5001/5002 streams
be recorded and hands them, without the `sapp_` options, to the native 4097
recorder of the image.

## 🌍 Translations

Languages: de, en, es, fi, fr, hu, it, lv, nl, pl, pt, ru, tr, uk, zh_CN
(`LANGS` in `src/locale/Makefile.am`). After a change of the texts, run in
`src/locale`:

```sh
./updateallpo.sh
```

It rebuilds `ServiceApp.pot`, merges it into every `.po` of `LANGS` (no fuzzy
guesses, nothing rewritten when only the date changed) and checks them with
`msgfmt -c`. A new language needs its entry in `LANGS`. Corrections and new
languages are welcome as pull requests.

## 🛠️ Build

ServiceApp is built by the image (OE recipe `enigma2-plugin-systemplugins-serviceapp`)
with autotools against the enigma2 headers of the image:

```sh
autoreconf -i
./configure PYTHON=python3
make && make install
```

The GitHub workflow `compile.yml` compiles every source against the headers of
openATV enigma2 (`g++ -fsyntax-only`) and checks the Python files.

## 🧪 Debug and log

The setting "Debug" (`config.plugins.serviceapp.debug`) logs the
messages of the player and of ServiceApp to the enigma2 debug log.

## 🙏 Credits

- [mx3L](https://github.com/mx3L/serviceapp): ServiceApp
- [oe-mirrors / openATV](https://github.com/oe-mirrors/serviceapp): the openATV branch
- [skyjet18](https://github.com/skyjet18/exteplayer3): exteplayer3
- fixes and features taken from the forks: [Haxe18](https://github.com/Haxe18/serviceapp) (crash fixes),
  [Blacker91](https://github.com/Blacker91/serviceapp-dreamos) (live reconnect, dual mono, errors),
  [boingbasti](https://github.com/boingbasti/e2-serviceapp-vti) (track selection, cue sheet ideas),
  [OpenBH](https://github.com/BlackHole/serviceapp) / [OpenViX](https://github.com/OpenViX/serviceapp) (transcoding, HDR, live audio),
  [j00zek](https://github.com/j00zek/serviceapp) (`sapp_args`), [monea-software](https://github.com/monea-software/hdsatelit-enigma2) (stream subtitle scan)
- translations: the translators of all forks, among them [audi06](https://github.com/audi06/serviceapp) (tr),
  [OpenLD](https://github.com/OpenLD/serviceapp_OLD) (es), [Alexwilmac](https://github.com/Alexwilmac/serviceapp) (lv, uk),
  Orlandoxx (fi), jackgee2021 (zh_CN)

## 📜 License

GNU General Public License version 2, see [COPYING](COPYING).
