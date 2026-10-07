#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
bridge="$root/bin/wine-bridge-speakup-cerence.exe"
wine=${WINE:-wine}
lib=${SPEAKUP_CERENCE_LIB:-"$root/lib"}
data=${SPEAKUP_CERENCE_DATA:-"$lib/data"}
store=${SPEAKUP_CERENCE_VOICE_STORE:-"${XDG_DATA_HOME:-$HOME/.local/share}/speakup-cerence/voices"}
output=${VOICE_TEST_OUTPUT:-"$root/test-output/installed-voices"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail()
{
    printf 'installed voice test: %s\n' "$*" >&2
    exit 1
}

sanitize()
{
    printf '%s' "$1" | LC_ALL=C tr -c '[:alnum:]_.-' '_'
}

[[ -x $bridge ]] || fail "missing bridge executable: $bridge"
[[ -d $lib ]] || fail "missing engine directory: $lib"
[[ -d $data ]] || fail "missing engine data directory: $data"
[[ -d $store ]] || fail "missing voice store: $store"
command -v "$wine" >/dev/null 2>&1 || fail "Wine executable not found: $wine"

mkdir -p "$output"
voices="$work/voices.tsv"
: >"$voices"

while IFS=$'\t' read -r language_code _; do
    [[ -n ${language_code:-} ]] || continue
    while IFS=$'\t' read -r listed_language voice operating_point _; do
        [[ -n ${voice:-} && -n ${operating_point:-} ]] || continue
        printf '%s\t%s\t%s\t%s\n' \
            "$language_code" "$listed_language" "$voice" "$operating_point" \
            >>"$voices"
    done < <(
        WINEDEBUG=-all "$wine" "$bridge" \
            --lib-dir "$lib" --data-dir "$data" --store "$store" \
            --list-voices "$language_code"
    )
done < <(
    WINEDEBUG=-all "$wine" "$bridge" \
        --lib-dir "$lib" --data-dir "$data" --store "$store" \
        --list-languages
)

sort -u -o "$voices" "$voices"
[[ -s $voices ]] || fail "the engine found no installed voices in $store"

# setting, level, Speakup rate/pitch/volume, engine rate/pitch/volume.  Change
# one setting at a time while the other two stay at speakup_soft's defaults
# (rate 2, pitch 5 and volume 5).  Pitch and volume have default digit 5, so
# their middle and default cases intentionally use the same values.
cases=(
    $'rate\tlowest\t0\t5\t5\t50\t100\t66'
    $'rate\tmiddle\t5\t5\t5\t175\t100\t66'
    $'rate\tdefault\t2\t5\t5\t100\t100\t66'
    $'rate\thighest\t9\t5\t5\t275\t100\t66'
    $'pitch\tlowest\t2\t0\t5\t100\t50\t66'
    $'pitch\tmiddle\t2\t5\t5\t100\t100\t66'
    $'pitch\tdefault\t2\t5\t5\t100\t100\t66'
    $'pitch\thighest\t2\t9\t5\t100\t140\t66'
    $'volume\tlowest\t2\t5\t0\t100\t100\t11'
    $'volume\tmiddle\t2\t5\t5\t100\t100\t66'
    $'volume\tdefault\t2\t5\t5\t100\t100\t66'
    $'volume\thighest\t2\t5\t9\t100\t100\t100'
)

manifest="$output/manifest.tsv"
printf 'language_code\tlanguage\tvoice\toperating_point\tsetting\tlevel\tspeakup_rate\tspeakup_pitch\tspeakup_volume\tengine_rate\tengine_pitch\tengine_volume\twav\tframes\n' >"$manifest"

voice_count=0
sample_count=0
while IFS=$'\t' read -r language_code language voice operating_point; do
    voice_count=$((voice_count + 1))
    voice_slug=$(sanitize "$voice")
    operating_point_slug=$(sanitize "$operating_point")
    for specification in "${cases[@]}"; do
        IFS=$'\t' read -r setting level speakup_rate speakup_pitch \
            speakup_volume engine_rate engine_pitch engine_volume \
            <<<"$specification"
        case_name="$setting-$level"
        wav_name=$(printf '%03d-%s-%s-%s.wav' "$voice_count" "$voice_slug" \
            "$operating_point_slug" "$case_name")
        wav="$output/$wav_name"
        log="$work/$wav_name.log"
        # Keep the spoken text identical across setting cases so listening and
        # duration comparisons isolate the parameter being varied.
        text="This is a Speakup settings test for $voice."

        if ! WINEDEBUG=-all "$wine" "$bridge" --debug 1 \
                --lib-dir "$lib" --data-dir "$data" --store "$store" \
                --voice "$voice" --vop "$operating_point" \
                --language "$language" --rate "$engine_rate" \
                --pitch "$engine_pitch" --volume "$engine_volume" \
                --wav "$wav" --speak "$text" >"$log" 2>&1; then
            cp "$log" "$output/$wav_name.log"
            fail "$voice ($operating_point), $case_name failed; see $output/$wav_name.log"
        fi

        expected="params rate=$engine_rate pitch=$engine_pitch volume=$engine_volume"
        grep -Fq "$expected" "$log" || {
            cp "$log" "$output/$wav_name.log"
            fail "$voice ($operating_point), $case_name did not apply $expected"
        }
        frames=$(grep -Eo 'ok frames=[0-9]+' "$log" | tail -1 | cut -d= -f2)
        [[ -n $frames && $frames -gt 0 ]] || {
            cp "$log" "$output/$wav_name.log"
            fail "$voice ($operating_point), $case_name produced no frames"
        }
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$language_code" "$language" "$voice" "$operating_point" \
            "$setting" "$level" "$speakup_rate" "$speakup_pitch" \
            "$speakup_volume" "$engine_rate" "$engine_pitch" \
            "$engine_volume" "$wav_name" "$frames" >>"$manifest"
        sample_count=$((sample_count + 1))
        printf 'ok: %s / %s / %s (%s frames)\n' \
            "$voice" "$operating_point" "$case_name" "$frames"
    done
done <"$voices"

python3 - "$manifest" "$output" <<'PY'
from array import array
from pathlib import Path
import csv
import sys
import wave

manifest = Path(sys.argv[1])
output = Path(sys.argv[2])
with manifest.open(newline="", encoding="utf-8") as stream:
    rows = list(csv.DictReader(stream, delimiter="\t"))
if not rows:
    raise SystemExit("manifest contains no synthesized samples")
for row in rows:
    path = output / row["wav"]
    with wave.open(str(path), "rb") as wav:
        if wav.getnchannels() != 1:
            raise SystemExit(f"{path}: expected mono audio")
        if wav.getsampwidth() != 2:
            raise SystemExit(f"{path}: expected 16-bit audio")
        if wav.getframerate() != 22050:
            raise SystemExit(f"{path}: expected 22050 Hz audio")
        frames = wav.getnframes()
        samples = array("h", wav.readframes(frames))
    if frames <= 0 or not any(samples):
        raise SystemExit(f"{path}: output is empty or silent")
PY

printf 'tested %d setting cases across %d installed voices; WAV files and manifest: %s\n' \
    "$sample_count" "$voice_count" "$output"
