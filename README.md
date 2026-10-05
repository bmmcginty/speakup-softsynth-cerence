# speakup-cerence

`speakup-cerence` reads Speakup's Unicode software-synthesizer device and
plays Cerence Embedded TTS directly through PipeWire. It replaces the old
espeakup/`LD_PRELOAD`/`pw-cat` chain with one native process and one small Wine
bridge. The bridge remains resident, so Wine and the voice are not reopened
for each utterance.

The 64-bit Cerence runtime and data is not included in this repository. It should be extracted to the `./lib` directory.
It can be found in any NVDA addon that provides these voices.
Installed voices are user data rather than part of the source tree.

## Build

Dependencies are a 64-bit C compiler, 64-bit MinGW-w64, 64-bit Wine, PipeWire
development files, Python 3, and Requests. The Visual C++ runtime used by the
engine is supplied by Wine or the selected Wine prefix.

```sh
make
```

This creates exactly these executables:

```text
bin/speakup-cerence
bin/speakup-cerence-voice-manager
bin/wine-bridge-speakup-cerence.exe
```

The runtime locates `../lib`, `../lib/data`, and the Wine bridge relative to
its executable, so the built tree can move after compilation. Voices default
to `${XDG_DATA_HOME:-~/.local/share}/speakup-cerence/voices`.

The locations can be overridden with `SPEAKUP_CERENCE_LIB`, `SPEAKUP_CERENCE_DATA`,
`SPEAKUP_CERENCE_VOICE_STORE`, and `SPEAKUP_CERENCE_BRIDGE`. `WINE` selects the Wine
executable. The bridge also accepts explicit `--lib-dir`, `--data-dir`, and
`--store` options. Per-voice rate and volume are kept in
`voice-settings` beside the voice store; `SPEAKUP_CERENCE_SETTINGS` overrides
that path.

## Voices

```sh
bin/speakup-cerence-voice-manager --list-voices
bin/speakup-cerence-voice-manager --list-voices --lang MNC --quality enhanced
bin/speakup-cerence-voice-manager --get-voice Tian-Tian
bin/speakup-cerence-voice-manager --get-voice Tian-Tian --quality lowest
```

Quality accepts catalogue names (`lowest`, `intermediate`, `enhanced`, and
`highest`) and engine names such as `embedded-high`.

Filters are validated independently. For example, an unknown `--lang` is
reported as a value missing from the voice data. If both requested values
exist in the catalogue but no one voice has both, the command instead reports
that no single voice has all requested attributes. Catalogue records are also
schema-checked when their attributes are used.

The manager downloads the catalogue to
`${XDG_CACHE_HOME:-~/.cache}/speakup-cerence/voices.json` when it is absent or
more than one day old. If refresh fails, a stale cache remains usable with a
warning. `SPEAKUP_CERENCE_CATALOG`, `SPEAKUP_CERENCE_CATALOG_URL`, and `SPEAKUP_CERENCE_VOICE_STORE`
override these locations. HTTP requests omit the User-Agent header.

`--get-voice` verifies the package's catalogue SHA-1 before extracting it and
rejects absolute, parent-relative, and symbolic-link archive paths. If several
qualities match a voice name, the best one is installed.

## Run

Stop any existing espeakup process first because the soft-synth device has one
reader. The user running this program needs read/write access to
`/dev/softsynthu` and access to the user's PipeWire session.

```sh
bin/speakup-cerence --lang MNC --quality enhanced
```

The driver detaches from the terminal and keeps running in the background. Add
`--foreground` to keep it attached to the current terminal, for example while
debugging or when a service manager supervises it.

For end-to-end testing without a Speakup device, `--device` accepts a regular
file or named pipe containing the same text and control-byte stream that
`/dev/softsynthu` would provide. The process waits for queued speech and
PipeWire playback to drain after end-of-file.

```sh
printf 'Testing Cerence speech.\n' > /tmp/softsynth-input
bin/speakup-cerence --foreground --device /tmp/softsynth-input --lang ENU

mkfifo /tmp/softsynth-input.fifo
bin/speakup-cerence --foreground --device /tmp/softsynth-input.fifo --lang ENU &
printf 'Testing through a pipe.\n' > /tmp/softsynth-input.fifo
```

Installed voices matching the filters are ordered alphabetically by name, with
the best installed quality retained when a name has several qualities. Voice 1
is active at startup. Speakup values 1 through 6 select voices on the current
six-voice page, 7 advances to the next page, and 0 returns to the previous
page. Selection is clamped at both ends: 0 returns to the startup voice on the
first page, and unavailable positions on the final page select the last voice.
This uses Speakup's existing 0–7 voice-control range without a kernel change.
The installed voice list is read from the voice store on disk rather than
from the engine. When a set-voice command finds that the store has changed,
the driver restarts the resident Wine bridge so the engine picks up the new
packages. A reload can also be requested at any time by sending `SIGHUP` to
the driver, for example `pkill -HUP speakup-cerence`.

Rate and volume are remembered per voice. Selecting a voice restores the last
rate and volume used with it, and the table is saved beside the voice store so
it survives a restart. Voices with no saved entry inherit the values in effect
when they are first selected.

Speakup's flush, index, rate, pitch, volume, and pause commands are also handled
directly. Indexes are written back when PipeWire reaches their audio position.
Speakup sends rate, pitch and volume as 0–9 digits; the driver maps them onto
the engine's rate (50–400), pitch (50–200) and volume (0–100) ranges.

The Cerence engine cannot be opened safely by two Wine bridge processes at
once. A second bridge now exits immediately with an "engine is already in use"
error instead of hanging during initialization. Use
`speakup-cerence-voice-manager --list-voices` to inspect the downloadable
catalogue while the speech runtime is active.

The engine licence belongs to the Wine prefix. Existing Cerence activation can
be selected with `WINEPREFIX`; the bridge reports a licensing error if the
engine cannot open a voice.

## Test

```sh
make test
```

The test is offline and checks voice filtering, including the distinction
between a missing filter value and an empty intersection.
