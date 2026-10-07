#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

file "$root/bin/speakup-cerence" | grep -q 'ELF 64-bit'
file "$root/bin/wine-bridge-speakup-cerence.exe" | grep -q 'PE32+'
test -x "$root/bin/speakup-cerence-voice-manager"
"$root/bin/speakup-cerence" --help | grep -q -- '--device PATH'
"$root/bin/speakup-cerence" --help | grep -q -- '--foreground'
"$root/bin/speakup-cerence" --help | grep -q -- '--log FILE'
"$root/bin/speakup-cerence" --help | grep -q -- '--debug LEVEL'
"$root/bin/speakup-cerence" --help | grep -q -- '--debug-file PATH'

if "$root/bin/speakup-cerence" --debug 0 >/dev/null 2>&1; then
    echo "--debug accepted level 0" >&2
    exit 1
fi
if "$root/bin/speakup-cerence" --debug 4 >/dev/null 2>&1; then
    echo "--debug accepted level 4" >&2
    exit 1
fi
if "$root/bin/speakup-cerence" --debug invalid >/dev/null 2>&1; then
    echo "--debug accepted a non-numeric level" >&2
    exit 1
fi
cat >"$work/voices.json" <<'JSON'
[
 {"voice_name":"Alpha","lang_code":"AAA","language":"A","gender":"Female","voice_type":"embedded-compact","quality":"Lowest","package_name":"a.zip","package_hash":"0000000000000000000000000000000000000000","size_in_bytes":10,"download_url":"https://example.invalid/a.zip"},
 {"voice_name":"Beta","lang_code":"BBB","language":"B","gender":"Male","voice_type":"embedded-high","quality":"enhanced","package_name":"b.zip","package_hash":"0000000000000000000000000000000000000000","size_in_bytes":20,"download_url":"https://example.invalid/b.zip"}
]
JSON
manager="$root/bin/speakup-cerence-voice-manager"
SPEAKUP_CERENCE_CATALOG="$work/voices.json" "$manager" --list-voices --lang AAA >"$work/out"
grep -q $'AAA\tAlpha\tlowest' "$work/out"
if SPEAKUP_CERENCE_CATALOG="$work/voices.json" "$manager" --list-voices --lang ZZZ >"$work/out" 2>"$work/err"; then exit 1; fi
grep -q 'voice data has no --lang value ZZZ' "$work/err"
if SPEAKUP_CERENCE_CATALOG="$work/voices.json" "$manager" --get-voice Missing >"$work/out" 2>"$work/err"; then exit 1; fi
grep -q 'voice data has no --get-voice value Missing' "$work/err"
if SPEAKUP_CERENCE_CATALOG="$work/voices.json" "$manager" --list-voices --lang AAA --quality enhanced >"$work/out" 2>"$work/err"; then exit 1; fi
grep -q 'no single voice has all requested attributes' "$work/err"
python3 "$root/tests/voice-manager.py"
printf 'CLI tests passed\n'
