# speakup-cerence

`speakup-cerence` reads Speakup's Unicode software-synthesizer device and
plays Cerence Embedded TTS directly through PipeWire. It replaces the old
espeakup/`LD_PRELOAD`/`pw-cat` chain with one native process and one small Wine
bridge. The bridge remains resident, so Wine and the voice are not reopened
for each utterance.

The repository contains the 64-bit Cerence runtime in `lib/` and common
engine data in `lib/data/`. Installed voices are user data rather than part of
the source tree.

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
`--store` options.

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

With no filters it selects the best installed voice. Speakup's flush, index,
rate, pitch, volume, and pause commands are handled directly. Indexes are
written back when PipeWire reaches their audio position.

The Cerence engine cannot be opened safely by two Wine bridge processes at
once. A second bridge now exits immediately with an "engine is already in use"
error instead of hanging during initialization. Use
`speakup-cerence-voice-manager --list-voices` to inspect the downloadable
catalogue while the speech runtime is active.

The engine licence belongs to the Wine prefix. Existing Cerence activation can
be selected with `WINEPREFIX`; the bridge reports a licensing error if the
engine cannot open a voice.

The patches under `patches/speakup/` add the full-Unicode `/dev/softsynthu`
path needed for supplementary code points and Unicode screen review.

## Test

```sh
make test
```

The test is offline and checks voice filtering, including the distinction
between a missing filter value and an empty intersection.
