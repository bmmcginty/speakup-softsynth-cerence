#!/usr/bin/env python3
"""List and install voices for speakup-cerence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import sys
import tempfile
import time
import zipfile
from pathlib import Path, PurePosixPath

import requests

CATALOG_URL = "https://files.accessmind.io/ve-voices/voices.json"
MAX_CATALOG_AGE = 24 * 60 * 60
QUALITY_ALIASES = {
    "compact": "lowest", "embedded-compact": "lowest", "lowest": "lowest",
    "pro": "intermediate", "embedded-pro": "intermediate", "intermediate": "intermediate",
    "high": "enhanced", "embedded-high": "enhanced", "enhanced": "enhanced",
    "premium": "highest", "embedded-premium": "highest", "premium-high": "highest",
    "highest": "highest",
}
QUALITY_RANK = {"lowest": 0, "intermediate": 1, "enhanced": 2, "highest": 3}
REQUIRED_FIELDS = (
    "voice_name", "language", "lang_code", "voice_type", "quality",
    "package_name", "package_hash", "size_in_bytes", "download_url",
)


def data_home() -> Path:
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))


def cache_home() -> Path:
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))


def voice_store() -> Path:
    return Path(os.environ.get(
        "SPEAKUP_CERENCE_VOICE_STORE", data_home() / "speakup-cerence/voices"
    )).expanduser()


def catalog_path() -> Path:
    return Path(os.environ.get(
        "SPEAKUP_CERENCE_CATALOG", cache_home() / "speakup-cerence/voices.json"
    )).expanduser()


def normalize_quality(value: str) -> str:
    return QUALITY_ALIASES.get(value.strip().lower(), value.strip().lower())


def validate_catalog(value: object) -> list[dict]:
    if not isinstance(value, list):
        raise ValueError("catalogue root is not a list")
    for number, voice in enumerate(value, 1):
        if not isinstance(voice, dict):
            raise ValueError(f"catalogue entry {number} is not an object")
        for field in REQUIRED_FIELDS:
            if field not in voice:
                raise ValueError(f"voice data is missing required attribute {field}")
        for field in REQUIRED_FIELDS:
            expected = int if field == "size_in_bytes" else str
            if not isinstance(voice[field], expected):
                raise ValueError(f"voice data has invalid attribute {field}")
    return value


def parse_catalog(data: bytes) -> list[dict]:
    return validate_catalog(json.loads(data.decode("utf-8")))


def download_catalog(path: Path) -> list[dict]:
    url = os.environ.get("SPEAKUP_CERENCE_CATALOG_URL", CATALOG_URL)
    with requests.get(url, timeout=30, headers={"User-Agent": None}) as response:
        response.raise_for_status()
        data = response.content
    catalog = parse_catalog(data)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix="voices.json.", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise
    return catalog


def load_catalog() -> list[dict]:
    path = catalog_path()
    fresh = False
    try:
        fresh = time.time() - path.stat().st_mtime <= MAX_CATALOG_AGE
    except FileNotFoundError:
        pass
    if fresh:
        return parse_catalog(path.read_bytes())
    try:
        return download_catalog(path)
    except (OSError, ValueError, json.JSONDecodeError, requests.RequestException) as error:
        if not path.is_file():
            raise SystemExit(f"cannot download voice catalogue: {error}") from error
        print(f"warning: cannot refresh voice catalogue: {error}; using stale cache", file=sys.stderr)
        try:
            return parse_catalog(path.read_bytes())
        except (OSError, ValueError, json.JSONDecodeError) as cache_error:
            raise SystemExit(f"cached voice catalogue is invalid: {cache_error}") from cache_error


def assert_filter_values(catalog: list[dict], lang: str | None,
                         quality: str | None, name: str | None = None) -> None:
    if lang and not any(v["lang_code"].lower() == lang.lower() for v in catalog):
        raise SystemExit(f"voice data has no --lang value {lang}")
    if quality and not any(normalize_quality(v["quality"]) == normalize_quality(quality)
                           for v in catalog):
        raise SystemExit(f"voice data has no --quality value {quality}")
    if name and not any(v["voice_name"].lower() == name.lower() for v in catalog):
        raise SystemExit(f"voice data has no --get-voice value {name}")


def matching(catalog: list[dict], lang: str | None, quality: str | None,
             name: str | None = None) -> list[dict]:
    assert_filter_values(catalog, lang, quality, name)
    result = [v for v in catalog
              if (not lang or v["lang_code"].lower() == lang.lower())
              and (not quality or normalize_quality(v["quality"]) == normalize_quality(quality))
              and (not name or v["voice_name"].lower() == name.lower())]
    if not result:
        raise SystemExit("no single voice has all requested attributes")
    return result


def list_voices(catalog: list[dict], args: argparse.Namespace) -> int:
    voices = matching(catalog, args.lang, args.quality)
    print("LANG\tVOICE\tQUALITY\tGENDER\tSIZE")
    for voice in sorted(voices, key=lambda v: (
            v["language"], v["voice_name"], QUALITY_RANK.get(normalize_quality(v["quality"]), -1))):
        print("\t".join((voice["lang_code"], voice["voice_name"],
                         normalize_quality(voice["quality"]), voice.get("gender") or "",
                         str(voice["size_in_bytes"]))))
    return 0


def download_package(voice: dict, directory: Path) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix="voice.", suffix=".zip", dir=directory)
    digest = hashlib.sha1()
    try:
        with os.fdopen(fd, "wb") as output, requests.get(
                voice["download_url"], stream=True, timeout=120,
                headers={"User-Agent": None}) as response:
            response.raise_for_status()
            for block in response.iter_content(1024 * 1024):
                if block:
                    digest.update(block)
                    output.write(block)
        if digest.hexdigest().lower() != voice["package_hash"].lower():
            raise ValueError("downloaded package failed SHA-1 verification")
        return Path(temporary)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def safe_extract(package: Path, store: Path) -> None:
    store.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="install.", dir=store.parent) as temporary:
        stage = Path(temporary)
        with zipfile.ZipFile(package) as archive:
            files = []
            for info in archive.infolist():
                path = PurePosixPath(info.filename)
                mode = info.external_attr >> 16
                if path.is_absolute() or ".." in path.parts or stat.S_ISLNK(mode):
                    raise ValueError(f"unsafe path in voice package: {info.filename}")
                if info.is_dir():
                    continue
                if len(path.parts) < 4 or len(path.parts[0]) != 3 or path.parts[1] != "speech" or path.parts[2] not in {"components", "ve"}:
                    raise ValueError(f"unexpected path in voice package: {info.filename}")
                files.append((info, path))
            if not any(path.suffix == ".hdr" for _, path in files) or not any(path.suffix == ".dat" for _, path in files):
                raise ValueError("voice package contains no engine data")
            for info, path in files:
                target = stage.joinpath(*path.parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
        for language in stage.iterdir():
            shutil.copytree(language, store / language.name, dirs_exist_ok=True)


def get_voice(catalog: list[dict], args: argparse.Namespace) -> int:
    choices = matching(catalog, args.lang, args.quality, args.get_voice)
    choice = max(enumerate(choices), key=lambda item: (
        QUALITY_RANK.get(normalize_quality(item[1]["quality"]), -1), item[0]))[1]
    package = download_package(choice, cache_home() / "speakup-cerence/packages")
    try:
        safe_extract(package, voice_store())
    finally:
        package.unlink(missing_ok=True)
    print(f"Installed {choice['voice_name']} ({choice['lang_code']}, "
          f"{normalize_quality(choice['quality'])}) in {voice_store()}")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(prog="speakup-cerence-voice-manager")
    action = result.add_mutually_exclusive_group(required=True)
    action.add_argument("--list-voices", action="store_true",
                        help="list available voices")
    action.add_argument("--get-voice", metavar="NAME",
                        help="download and install one voice")
    result.add_argument("--lang", help="engine language code, e.g. MNC")
    result.add_argument("--quality", help="lowest, intermediate, enhanced, or highest")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    catalog = load_catalog()
    return list_voices(catalog, args) if args.list_voices else get_voice(catalog, args)


if __name__ == "__main__":
    raise SystemExit(main())
