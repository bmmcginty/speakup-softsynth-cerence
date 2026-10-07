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
that path. `SPEAKUP_CERENCE_LOG` selects the log file.

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

Because a background process has no terminal to complain on, the driver also
appends diagnostics to
`${XDG_STATE_HOME:-~/.local/state}/speakup-cerence/speakup-cerence.log`. Use
`--log FILE` or `SPEAKUP_CERENCE_LOG` to choose another file, or `--log -` for
standard error only. `--foreground` writes to the log and to the terminal.
Wine bridge errors are captured in the same log.

Use `--debug LEVEL` for additional Linux, bridge, and Wine diagnostics. Levels
1 through 3 are supported, and higher levels include everything below them:

- Level 1 records startup, resolved components, voice selection, requested
  parameters, and the effective parameters read back from Cerence. Wine's own
  debug channels remain disabled.
- Level 2 adds synthesis timing, audio and marker totals, data paths, DLL-load
  messages, and structured-exception diagnostics from Wine.
- Level 3 records every bridge command, engine callback, and marker, and also
  enables Wine warnings. It can produce a large log and is intended for short
  reproductions of text-processing and prosody problems.

`--debug-file PATH` sends the combined driver, bridge, and Wine diagnostics to
that path, taking precedence over `--log` and `SPEAKUP_CERENCE_LOG`. For
example:

```sh
printf 'A sentence with falling intonation.\n' > /tmp/softsynth-input
bin/speakup-cerence --debug 3 --debug-file /tmp/cerence-debug.log \
    --foreground --device /tmp/softsynth-input --lang ENU
```

Debug logs include installation paths, voice names, parameter values, and text
lengths, but do not include synthesized text or licence keys.

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
Speakup sends rate, pitch and volume as 0–9 digits. The engine's rate and
pitch are percentages relative to each voice's neutral value of 100, so the
driver anchors Speakup's default rate (2) and pitch (5) at that neutral value
and moves a fixed step per digit; volume follows espeakup's (digit + 1) * 11
scale, clamped to 100, so Speakup volume 0 is quiet rather than silent.
Anchoring the defaults prevents the fast, chipmunk-like speech that an even
spread across the engine's full 50–400 and 50–200 ranges produced.

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

A separate live test exercises the licensed engine and every installed
voice/operating-point pair. It tests each adjustable Speakup setting supported
by this driver—rate, pitch, and volume—independently at its lowest (0), middle
(5), default, and highest (9) values. The other two settings remain at
Speakup's defaults while one is varied: rate defaults to 2, while pitch and
volume default to 5. Consequently, the middle and default samples for pitch
and volume intentionally use the same values.

Voice selection is the outer dimension of the matrix, so all twelve setting
cases are synthesized for every installed voice and operating point. Index,
flush, and pause are event commands rather than scalar settings. Other
soft-synth variables are not mapped to Cerence by this driver and cannot be
included as adjustable cases until they are supported.

The test verifies the scaled engine parameters, WAV format, non-empty frame
count, and non-silent audio. It leaves the samples and a tab-separated manifest
in `test-output/installed-voices` for listening and comparison.

```sh
make test-installed-voices

# Optional locations and Wine executable:
VOICE_TEST_OUTPUT=/tmp/cerence-voice-tests \
SPEAKUP_CERENCE_VOICE_STORE=/path/to/voices \
WINE=/path/to/wine make test-installed-voices
```

This live test is intentionally not part of `make test`: it requires the
Cerence DLLs, a valid engine licence, Wine, and at least one installed voice.
